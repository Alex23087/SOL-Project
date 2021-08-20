#include <sys/un.h>
#include <bits/types/struct_timespec.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/stat.h>

#include "ClientAPI.h"
#include "timespecUtils.h"
#include "ParseUtils.h"
#include "defines.h"
#include "FileCachingProtocol.h"
#include "ion.h"

//Open connections key value list, useful if the API is extended to handle more connections
static ArgsList* openConnections = NULL;
static int activeConnectionFD = -1;

int openConnection(const char* sockname, int msec, const struct timespec abstime){
	if(getNodeForKey(openConnections, sockname) != NULL){
		//There already is a connection open for that address
		errno = EADDRINUSE;
		return -1;
	}
	
	int clientSocketDescriptor = -1;
	struct sockaddr_un serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sun_family = AF_UNIX;
	strncpy(serverAddress.sun_path, sockname, strlen(sockname) + 1);
	
	clientSocketDescriptor = socket(AF_UNIX, SOCK_STREAM, 0);
	if(clientSocketDescriptor < 0){
		perror("Error while creating socket");
		return -1;
	}
	
	struct timespec currentTime;
	clock_gettime(CLOCK_REALTIME, &currentTime);
	
	struct timespec deadlineTime = addTimes(currentTime, abstime);
	struct timespec endTime = addTimes(currentTime, doubleToTimespec(msec * 1e-3));
	
	bool connectionSucceeded = false;
	do{
		connectionSucceeded = !connect(clientSocketDescriptor, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
		if(!connectionSucceeded){
			perror("Error while connecting to socket");
			if(compareTimes(deadlineTime, endTime) >= 0){
				usleep(msec * 1e3L);
			}else{
				break;
			}
			clock_gettime(CLOCK_REALTIME, &currentTime);
			endTime = addTimes(currentTime, doubleToTimespec(msec * 1e-3));
		}else{
			break;
		}
	}while(compareTimes(deadlineTime, currentTime) > 0);
	
	if(connectionSucceeded){
#ifdef DEBUG
		printf("Connected to server\n");
#endif
		
		//Adding the connection to the key value array of open connections
		ArgsList* newConnection = initArgsListNode();
		newConnection->type = Long;
		newConnection->name = malloc(strlen(sockname) + 1);
		strcpy(newConnection->name, sockname);
		newConnection->data = (void*)((long)clientSocketDescriptor);
		newConnection->next = openConnections;
		openConnections = newConnection;
		
		activeConnectionFD = clientSocketDescriptor;
		return 0;
	}else{
#ifdef DEBUG
		printf("Connection to server failed\n");
#endif
		errno = ENXIO; //ETIMEDOUT maybe?
		return -1;
	}
}

int closeConnection(const char* sockname){
	ArgsList* connection = getNodeForKey(openConnections, sockname);
	if(connection == NULL){
		//Trying to close a connection that is not open
		errno = ENOTCONN;
		return -1;
	}else{
		int fd = (int)(long)(connection->data);
		if(close(fd)){
			//Error while closing connection, errno has been set by close()
			return -1;
		}else{
			freeArgsListNode(connection);
			return 0;
		}
	}
}

int openFile(const char* pathname, int flags){
	if(activeConnectionFD == -1){
		//Function called without an active connection
		errno = ENOTCONN;
		return -1;
	}
	
	if(strlen(pathname) > FCP_MESSAGE_LENGTH - 5){
		errno = ENAMETOOLONG;
		return -1;
	}
	
	printf("Sending open request to server\n");
	fcpSend(FCP_OPEN, flags, (char*)pathname, activeConnectionFD);
	printf("Open request sent\n");
	
	char fcpBuffer[FCP_MESSAGE_LENGTH];
	readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
	FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);
	
	bool success = true;
	switch(message->op){
		case FCP_ACK:{
			printf("File opened correctly\n");
			break;
		}
		case FCP_ERROR:{
			errno =	message->control;
			success = false;
			break;
		}
		default:{
			success = false;
		}
	}
	
	free(message);
	return success ? 0 : -1;
}

int writeFile(const char* pathname, const char* dirname){
	if(activeConnectionFD == -1){
		//Function called without an active connection
		errno = ENOTCONN;
		return -1;
	}
	
	if(strlen(pathname) > FCP_MESSAGE_LENGTH - 5){
		errno = ENAMETOOLONG;
		return -1;
	}
	
	int fileDescriptor = open(pathname, O_RDONLY);
	if(fileDescriptor == -1){
		//Open failed, errno has already been set by it
		return -1;
	}
	struct stat fileStat;
	if(fstat(fileDescriptor, &fileStat)){
		//Fstat failed, errno has already been set by it
		return -1;
	}
	off_t fileSize = fileStat.st_size;
	
	printf("Sending write request to server\n");
	fcpSend(FCP_WRITE, (int)fileSize, (char*)pathname, activeConnectionFD);
	printf("Write request sent\n");
	
	char fcpBuffer[FCP_MESSAGE_LENGTH];
	readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
	FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);
	
	bool success = true;
	switch(message->op){
		case FCP_ACK:{
			printf("Server has sent ack back, starting transfer\n");
			char* fileBuffer = malloc(fileSize);
			readn(fileDescriptor, fileBuffer, fileSize);
			writen(activeConnectionFD, fileBuffer, fileSize);
			free(fileBuffer);
			printf("File transfer complete, waiting for ack\n");
			fcpBuffer[4] = 0;
			if(readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH) == FCP_MESSAGE_LENGTH){
				free(message);
				message = fcpMessageFromBuffer(fcpBuffer);
			
				switch(message->op) {
					case FCP_ACK:{
						printf("Received ack from server, operation completed successfully\n");
						break;
					}
					default:{
						printf("Server sent an invalid reply, operation failed\n");
						errno = EPROTO; //EBADMSG EPROTO EMSGSIZE EILSEQ
						success = false;
						break;
					}
				}
			}else{
				printf("Server sent an invalid reply, operation failed\n");
				errno = EPROTO;
				success = false;
			}
			break;
		}
		case FCP_ERROR:{
			errno = message->control;
			success = false;
			break;
		}
		default:{
			success = false;
			break;
		}
	}
	
	free(message);
	return success ? 0 : -1;
}