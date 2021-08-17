#include <getopt.h>
#include <stdio.h>
#include <malloc.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "defines.h"
#include "ParseUtils.h"
#include "ion.h"

#define MAX_BACKLOG 10
#define W2M_MESSAGE_LENGTH 5
#define W2M_CLIENT_SERVED 'F'
#define W2M_CLIENT_DISCONNECTED 'D'
#define W2M_SIGNAL_TERM 'T'
#define W2M_SIGNAL_HANG 'H'
#define cleanup() \
	unlink(socketPath);\
	free(socketPath);\
	free(logFilePath);

int w2mPipeDescriptors[2];

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

static inline int getIntFromW2MMessage(char message[W2M_MESSAGE_LENGTH]){
	return message[4] + (message[3] << 8) + (message[2] << 16) + (message[1] << 24);
}

static inline char* makeW2MMessage(char message, int32_t data, char out[W2M_MESSAGE_LENGTH]){
	out[0] = message;
	switch(message){
		case W2M_CLIENT_SERVED: case W2M_CLIENT_DISCONNECTED:{
			out[1] = (data >> 24) & 0xFF;
			out[2] = (data >> 16) & 0xFF;
			out[3] = (data >> 8) & 0xFF;
			out[4] = data & 0xFF;
			break;
		}
		default:{
			for(size_t i = 1; i < W2M_MESSAGE_LENGTH; i++){
				out[i] = 0;
			}
		}
	}
	
	return out;
}

void* signalHandlerThread(void* arg){
	//Masking the signals we'll listen to
	sigset_t listenSet;
	sigaddset(&listenSet, SIGINT);
	sigaddset(&listenSet, SIGQUIT);
	sigaddset(&listenSet, SIGHUP);
	pthread_sigmask(SIG_SETMASK, &listenSet, NULL);
	
	int signalReceived;
	if(sigwait(&listenSet, &signalReceived)){
		perror("Error while calling sigwait");
		//TODO: Handle error
		return (void *) -1;
	}
	
	char messageBuffer[W2M_MESSAGE_LENGTH];
	switch(signalReceived){
		case SIGINT: case SIGQUIT: default:{
			writen(w2mPipeDescriptors[1], makeW2MMessage(W2M_SIGNAL_TERM, 0, messageBuffer), W2M_MESSAGE_LENGTH);
			break;
		}
		case SIGHUP:{
			writen(w2mPipeDescriptors[1], makeW2MMessage(W2M_SIGNAL_HANG, 0, messageBuffer), W2M_MESSAGE_LENGTH);
			break;
		}
	}
	
	return 0;
}

#ifdef IDE
int serverMain(int argc, char** argv){
#else
int main(int argc, char** argv){
#endif
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
			case -1: default:{
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
	
	
	//Initialize worker-to-master pipe
	if(pipe(w2mPipeDescriptors)){
		perror("Error while creating worker-to-master pipe");
		return -1;
	}
	
	//Spawn signal handler thread
	pthread_t signalHandlerThreadID;
	if(pthread_create(&signalHandlerThreadID, NULL, signalHandlerThread, NULL)){
		perror("Error while creating signal handler thread");
		return -1;
	}
	
	//Mask all signals, they will be handled by the signal handler thread
	sigset_t signalMask;
	sigfillset(&signalMask);
	pthread_sigmask(SIG_SETMASK, &signalMask, NULL);
	
	
	//TODO: Spawn worker threads
	
	
	//Main loop
	int maxFd = -1;
	fd_set selectFdSet;
	fd_set tempFdSet;
	FD_ZERO(&selectFdSet);
	addToFdSetUpdatingMax(serverSocketDescriptor, &selectFdSet, &maxFd);
	addToFdSetUpdatingMax(w2mPipeDescriptors[0], &selectFdSet, &maxFd);
	
	
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	
	bool running = true;
	while(running){
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
						
					}else if(currentFd == w2mPipeDescriptors[0]){
						//Message received from worker or signal thread
						char buffer[W2M_MESSAGE_LENGTH];
						ssize_t bytesRead = readn(w2mPipeDescriptors[0], &buffer, W2M_MESSAGE_LENGTH);
						if(bytesRead < W2M_MESSAGE_LENGTH){
							//TODO: Handle error
							perror("Invalid message length on worker-to-master pipe");
							return -1;
						}
						switch(buffer[0]){
							case W2M_CLIENT_SERVED:{
								//A worker has served the client's request, add back client to the set of fds to listen to
								addToFdSetUpdatingMax(getIntFromW2MMessage(buffer), &selectFdSet, &maxFd);
								break;
							}
							case W2M_SIGNAL_TERM:{
								//Stop listening to incoming connections, close all connections, terminate
								FD_ZERO(&selectFdSet);
#ifdef DEBUG
								printf("Received termination signal\n");
#endif
								running = false;
								break;
							}
							case W2M_SIGNAL_HANG:{
								//Stop listening to incoming connections, serve all requests, terminate
								FD_CLR(serverSocketDescriptor, &selectFdSet);
#ifdef DEBUG
								printf("Received hangup signal\n");
#endif
								running = false;
								break;
							}
							//TODO: Handle other cases
						}
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