#include <sys/un.h>
#include <bits/types/struct_timespec.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <malloc.h>
#include <errno.h>

#include "ClientAPI.h"
#include "timespecUtils.h"
#include "ParseUtils.h"
#include "defines.h"

ArgsList* openConnections = NULL;

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
		newConnection->name = (char*)sockname;
		newConnection->data = (void*)((long)clientSocketDescriptor);
		newConnection->next = openConnections;
		
		return 0;
	}else{
#ifdef DEBUG
		printf("Connection to server failed\n");
#endif
		errno = ENXIO;
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
		int fd = (int)(connection->data);
		if(close(fd)){
			//Error while closing connection, errno has been set by close()
			return -1;
		}else{
			return 0;
		}
	}
}