#include <getopt.h>
#include <stdio.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "defines.h"
#include "ParseUtils.h"
#include "ion.h"

#define MAX_BACKLOG 10
#define cleanup() \
	unlink(socketPath);\
	free(socketPath);\
	free(logFilePath);

static inline void addToFdSetUpdatingMax(int fd, fd_set* fdSet, int* maxFd){
	FD_SET(fd, fdSet);
	if(fd > *maxFd){
		*maxFd = fd;
	}
}

static inline void removeFromFdSetUpdatingMax(int fd, fd_set* fdSet, int* maxFd){
	FD_CLR(fd, fdSet);
	if(fd == *maxFd){
		for(int newMax = *maxFd - 1; newMax >= -1; newMax--){
			if(newMax == -1 || FD_ISSET(newMax, fdSet)){
				*maxFd = newMax;
				return;
			}
		}
	}
}

int serverMain(int argc, char** argv){
	char* configFilePath = "/mnt/e/Progetti/SOL-Project/config.txt";
	unsigned short nWorkers = 10;
	unsigned int maxFiles = 100;
	unsigned long storageSize = 1024 * 1024 * 1024;
	char* socketPath = NULL;
	char* logFilePath = NULL;
	
	
	//Command line options parsing
	char opt;
	opterr = 0;
	bool finished = false;
	
	while(!finished){
		opt = getopt(argc, argv, "hf:c:");
		switch(opt){
			case 'h':{
				//TODO: Write server help
				printf("%s\n", argv[0]);
				break;
			}
			case 'f':{
				socketPath = optarg;
				break;
			}
			case 'c':{
				configFilePath = optarg;
			}
			case '?':{
				fprintf(stderr, "Unrecognized option: %c\n", optopt);
				return -1;
			}
			case -1:{
				finished = true;
				break;
			}
		}
	}
	
	
	//Config file parsing
	ArgsList* configArgs = readConfigFile(configFilePath);
	nWorkers = getLongValue(configArgs, "nWorkers");
	maxFiles = getLongValue(configArgs, "maxFiles");
	socketPath = getStringValue(configArgs, "socketPath");
	logFilePath = getStringValue(configArgs, "logFile");
	storageSize = getLongValue(configArgs, "storageSize");
	//TODO: Maybe change the config so that you can specify sizes such as "100M", "1G", etc.
	freeArgsListNode(configArgs);
	
	
	//Creating server listen socket
	int serverSocketDescriptor = -1;
	struct sockaddr_un serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sun_family = AF_UNIX;
	strncpy(serverAddress.sun_path, socketPath, strlen(socketPath) + 1);
	
	serverSocketDescriptor = socket(AF_UNIX, SOCK_STREAM, 0);
	if(serverSocketDescriptor < 0){
		perror("Error while creating the server socket");
		cleanup();
		return -1;
	}
	
	if(bind(serverSocketDescriptor, (struct sockaddr *)&serverAddress, sizeof(serverAddress))){
		perror("Error while binding socket");
		cleanup();
		return -1;
	}
	
	if(listen(serverSocketDescriptor, MAX_BACKLOG)){
		perror("Error while setting socket to listen");
		cleanup();
		return -1;
	}
	
	
	//TODO: Init w2m pipe
	int w2mPipeDescriptor = 0;
	
	
	//TODO: Spawn worker threads
	
	
	//Main loop
	int maxFd = -1;
	fd_set selectFdSet;
	fd_set tempFdSet;
	FD_ZERO(&selectFdSet);
	addToFdSetUpdatingMax(serverSocketDescriptor, &selectFdSet, &maxFd);
	
	
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	
	while(true){
		tempFdSet = selectFdSet;
		if(select(maxFd + 1, &tempFdSet, NULL, NULL, &tv) != -1){
			for(int currentFd = 0; currentFd <= maxFd; currentFd ++){
				if(FD_ISSET(currentFd, &tempFdSet)){
					if(currentFd == serverSocketDescriptor){
						//New connection received, add client descriptor to set
						int newClientDescriptor = accept(serverSocketDescriptor, NULL, NULL);
						if(newClientDescriptor < 0){
							perror("Error while accepting a new connection");
							cleanup();
							return -1;
						}
#ifdef DEBUG
						printf("Incoming connection received, client descriptor: %d\n", newClientDescriptor);
#endif
						addToFdSetUpdatingMax(newClientDescriptor, &selectFdSet, &maxFd);
						
					}else if(currentFd == w2mPipeDescriptor){
						//TODO: Message received from worker, add back client descriptor to set
					}else{
						//TODO: Data received from already connected client, remove client descriptor from set and pass message to worker
						
						//Client disconnection code
						char buffer[512];
						ssize_t bytesRead = readn(currentFd, &buffer, 512);
						if(bytesRead == 0){
							if(close(currentFd)){
								//TODO: Handle error
							}else{
#ifdef DEBUG
								printf("Connection with client on file descriptor %d has been closed\n", currentFd);
#endif
								removeFromFdSetUpdatingMax(currentFd, &selectFdSet, &maxFd);
							}
						}
					}
				}
			}
		}else{
			//TODO: Handle error
			perror("Error during select()");
		}
	}
	
	
	cleanup();
	return 0;
}