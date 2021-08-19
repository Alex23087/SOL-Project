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
#include "queue.h"
#include "FileCachingProtocol.h"

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


static int w2mPipeDescriptors[2];
static Queue* incomingConnectionsQueue = NULL;
static pthread_mutex_t incomingConnectionsLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t incomingConnectionsCond = PTHREAD_COND_INITIALIZER;
static bool workersShouldTerminate = false;
static pthread_mutex_t clientListLock = PTHREAD_MUTEX_INITIALIZER;
static ClientList* clientList = NULL;


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

static inline void pthread_mutex_lock_error(pthread_mutex_t* lock, const char* msg){
	if(pthread_mutex_lock(lock)){
		perror(msg);
		//TODO: Handle error, maybe send message to master
	}
}

static inline void pthread_mutex_unlock_error(pthread_mutex_t* lock, const char* msg){
	if(pthread_mutex_unlock(lock)){
		perror(msg);
		//TODO: Handle error, maybe send message to master
	}
}

static inline void pthread_cond_signal_error(pthread_cond_t* cond, const char* msg){
	if(pthread_cond_signal(cond)){
		perror(msg);
		//TODO: Handle error, maybe send message to master
	}
}

static inline void pthread_cond_wait_error(pthread_cond_t* cond, pthread_mutex_t* lock, const char* msg){
	if(pthread_cond_wait(cond, lock)){
		perror(msg);
		//TODO: Handle error, maybe send message to master
	}
}

static inline void pthread_cond_broadcast_error(pthread_cond_t* cond, const char* msg){
	if(pthread_cond_broadcast(cond)){
		perror(msg);
		//TODO: Handle error
	}
}

static inline void pthread_join_error(pthread_t thread, const char* msg){
	if(pthread_join(thread, NULL)){
		perror(msg);
	}
}

static inline void w2mSend(char message, int32_t data){
	char buffer[W2M_MESSAGE_LENGTH];
	writen(w2mPipeDescriptors[1], makeW2MMessage(message, data, buffer), W2M_MESSAGE_LENGTH);
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
	
	switch(signalReceived){
		case SIGINT: case SIGQUIT: default:{
			printf("[Signal]: Received signal %s\n", signalReceived == SIGINT ? "SIGINT" : "SIGQUIT");
			w2mSend(W2M_SIGNAL_TERM, 0);
			break;
		}
		case SIGHUP:{
			printf("[Signal]: Received signal SIGHUP\n");
			w2mSend(W2M_SIGNAL_TERM, 0);
			break;
		}
	}
	
	return 0;
}

int workerDisconnectClient(int workerN, int fdToServe){
	if(close(fdToServe)){
		return -1;
	}else{
#ifdef DEBUG
		printf("[Worker #%d]: Connection with client on file descriptor %d has been closed\n", workerN, fdToServe);
#endif
		//Warn the master thread
		w2mSend(W2M_CLIENT_DISCONNECTED, fdToServe);
		return 0;
	}
}

void* workerThread(void* arg){
	printf("[Worker #%d]: Up and running\n", (int)arg);
	while(!workersShouldTerminate){
		//Wait for connection to serve
		pthread_mutex_lock_error(&incomingConnectionsLock, "Error while locking on incoming connection from worker");
		while(queueIsEmpty(incomingConnectionsQueue) && !workersShouldTerminate){
			pthread_cond_wait_error(&incomingConnectionsCond, &incomingConnectionsLock, "Error while waiting on incoming connection condition variable from worker thread");
		}
		int fdToServe;
		if(!workersShouldTerminate){
			fdToServe = (int)(long)queuePop(&incomingConnectionsQueue);
		}
		pthread_mutex_unlock_error(&incomingConnectionsLock, "Error while unlocking on incoming connection from worker");
		if(workersShouldTerminate){
			break;
		}
		
		//Serve connection
		printf("[Worker #%d]: Serving client on descriptor %d\n", (int)arg, fdToServe);
		pthread_mutex_lock_error(&clientListLock, "Error while locking client list");
		ConnectionStatus status = clientListGetStatus(clientList, fdToServe);
		pthread_mutex_unlock_error(&clientListLock, "Error while unlocking client list");
		switch(status.op){
			case Connected:{
				char fcpBuffer[FCP_MESSAGE_LENGTH];
				ssize_t fcpBytesRead = readn(fdToServe, fcpBuffer, FCP_MESSAGE_LENGTH);
				
				//TODO: Handle wrong number of bytes read
				if(fcpBytesRead == 0){
					//Client disconnected
					if(workerDisconnectClient((int)arg, fdToServe)){
						//TODO: Handle error
					}
				}else{
					FCPMessage* fcpMessage = fcpMessageFromBuffer(fcpBuffer);
					switch(fcpMessage->op){
						case FCP_WRITE:{
							//Client has issued a write request: update client status, send ack, warn master
							printf("[Worker #%d]: Client %d issued op: %d (FCP_WRITE), size: %d, filename: \"%s\"\n", (int)arg, fdToServe, fcpMessage->op, fcpMessage->size, fcpMessage->filename);
							ConnectionStatus newStatus;
							newStatus.op = SendingFile;
							newStatus.data.messageLength = fcpMessage->size;
							newStatus.data.filename = malloc(FCP_MESSAGE_LENGTH - 5);
							strncpy(newStatus.data.filename, fcpMessage->filename, FCP_MESSAGE_LENGTH - 5);
							pthread_mutex_lock_error(&clientListLock, "Error while locking client list");
							clientListUpdateStatus(clientList, fdToServe, newStatus);
							pthread_mutex_unlock_error(&clientListLock, "Error while unlocking client list");
							
							fcpSend(FCP_ACK, 0, NULL, fdToServe);
							
							w2mSend(W2M_CLIENT_SERVED, fdToServe);
							break;
						}
						default:{
							//Client has requested an invalid operation, forcibly disconnect client
							printf("[Worker #%d]: Client %d has requested an invalid operation with opcode %d\n", (int)arg, fdToServe, fcpMessage->op);
							workerDisconnectClient((int)arg, fdToServe);
						}
					}
					free(fcpMessage);
				}
				break;
			}
			case SendingFile:{
				int32_t fileSize = status.data.messageLength;
				
				char* buffer = malloc(fileSize);
				size_t bytesRead = readn(fdToServe, buffer, fileSize);
				if(bytesRead != fileSize){
					//Client sent an ill-formed packet, disconnecting it
					printf("[Worker #%d]: Client %d sent a different amount of bytes than advertised (%d vs %d), disconnecting it\n", (int)arg, fdToServe, status.data.messageLength, bytesRead);
					workerDisconnectClient((int)arg, fdToServe);
				}else{
					//TODO: Check if the message was longer than advertised and throw error
					
					//Transfer completed successfully, send ack to client, set client status to connected, and send client served message to master
					printf("[Worker #%d]: Received file from client %d, %d bytes transferred\n", (int)arg, fdToServe, bytesRead);
					printf("[Worker #%d]: Sending ack to client %d\n", (int)arg, fdToServe);
					
					ConnectionStatus newStatus;
					newStatus.op = Connected;
					newStatus.data.messageLength = 0;
					newStatus.data.filename = NULL;
					pthread_mutex_lock_error(&clientListLock, "Error while locking client list");
					clientListUpdateStatus(clientList, fdToServe, newStatus);
					pthread_mutex_unlock_error(&clientListLock, "Error while unlocking client list");
					
					fcpSend(FCP_ACK, 0, NULL, fdToServe);
					
					w2mSend(W2M_CLIENT_SERVED, fdToServe);
				}
				free(buffer);
				break;
			}
			default:{
				//Invalid status
				printf("[Worker #%d]: Client %d has sent a message while in an invalid status, disconnecting it\n", (int)arg, fdToServe);
				workerDisconnectClient((int)arg, fdToServe);
				break;
			}
		}
	}
	
	printf("[Worker #%d]: Terminating\n", (int)arg);
	return (void*)0;
}

int onNewConnectionReceived(int serverSocketDescriptor, fd_set* selectFdSet, int* maxFd);
int onW2MMessageReceived(int serverSocketDescriptor, fd_set* selectFdSet, int* maxFd, bool* running);
int onConnectedClientMessage(int currentFd, fd_set* selectFdSet, int* maxFd);

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
	
	
	//Spawn worker threads
	pthread_t workers[nWorkers];
	for(size_t i = 0; i < nWorkers; i++){
		if(pthread_create(&(workers[i]), NULL, workerThread, (void*)i)){
			perror("Error while creating worker thread");
			return -1;
		}
	}
	
	
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
						if(onNewConnectionReceived(serverSocketDescriptor, &selectFdSet, &maxFd)){
							cleanup();
							return -1;
						}
					}else if(currentFd == w2mPipeDescriptors[0]){
						//Message received from worker or signal thread
						if(onW2MMessageReceived(serverSocketDescriptor, &selectFdSet, &maxFd, &running)){
							return -1;
						}
					}else{
						//Data received from already connected client, remove client descriptor from select set and pass it to worker
						if(onConnectedClientMessage(currentFd, &selectFdSet, &maxFd)){
							return -1;
						}
					}
				}
			}
		}else{
			//TODO: Handle error
			perror("Error during select()");
			break;
		}
	}
	
	
	//Cleanup
	
	//It's safe to join on the signal handler, as the only way to terminate the server is for a signal to happen
	//and in that case the signal handler terminates
	pthread_join_error(signalHandlerThreadID, "Error while joining on signal handler thread");
	for(size_t i = 0; i < nWorkers; i++){
		pthread_join_error(workers[i], "Error while joining worker thread");
	}
	
	
	if(close(w2mPipeDescriptors[0])){
		perror("Error while closing w2m pipe read endpoint");
	}
	if(close(w2mPipeDescriptors[1])){
		perror("Error while closing w2m pipe write endpoint");
	}
	
	cleanup();
	return 0;
}



int onNewConnectionReceived(int serverSocketDescriptor, fd_set* selectFdSet, int* maxFd){
	int newClientDescriptor = accept(serverSocketDescriptor, NULL, NULL);
	if(newClientDescriptor < 0){
		perror("Error while accepting a new connection");
		return -1;
	}
#ifdef DEBUG
	printf("[Master]: Incoming connection received, client descriptor: %d\n", newClientDescriptor);
#endif
	addToFdSetUpdatingMax(newClientDescriptor, selectFdSet, maxFd);
	
	pthread_mutex_lock_error(&clientListLock, "Error while locking client list");
	clientListAdd(&clientList, newClientDescriptor);
	pthread_mutex_unlock_error(&clientListLock, "Error while unlocking client list");
#ifdef DEBUG
	printf("[Master]: Client %d added to list of connected clients\n", newClientDescriptor);
#endif
	return 0;
}


int onW2MMessageReceived(int serverSocketDescriptor, fd_set* selectFdSet, int* maxFd, bool* running){
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
			int clientFd = getIntFromW2MMessage(buffer);
			printf("[Master]: Client %d has been served, adding it back to select set\n", clientFd);
			addToFdSetUpdatingMax(clientFd, selectFdSet, maxFd);
			break;
		}
		case W2M_SIGNAL_TERM:{
			//Stop listening to incoming connections, close all connections, terminate
			FD_ZERO(selectFdSet);
#ifdef DEBUG
			printf("[Master]: Received termination signal\n");
#endif
			*running = false;
			workersShouldTerminate = true;
			
			pthread_mutex_lock_error(&incomingConnectionsLock, "Error while locking incoming connections");
			pthread_cond_broadcast_error(&incomingConnectionsCond, "Error while broadcasting stop message");
			pthread_mutex_unlock_error(&incomingConnectionsLock, "Error while unlocking incoming connections");
			
			break;
		}
		case W2M_SIGNAL_HANG:{
			//Stop listening to incoming connections, serve all requests, terminate
			removeFromFdSetUpdatingMax(serverSocketDescriptor, selectFdSet, maxFd);
#ifdef DEBUG
			printf("[Master]: Received hangup signal\n");
#endif
			*running = false;
			break;
		}
		case W2M_CLIENT_DISCONNECTED:{
			int clientFd = getIntFromW2MMessage(buffer);
			
			pthread_mutex_lock_error(&clientListLock, "Error while locking client list");
			clientListRemove(&clientList, clientFd);
			pthread_mutex_unlock_error(&clientListLock, "Error while unlocking client list");
#ifdef DEBUG
			printf("[Master]: Client %d removed from list of connected clients\n",clientFd);
#endif
			break;
		}
		//TODO: Handle other cases
	}
	return 0;
}


int onConnectedClientMessage(int currentFd, fd_set* selectFdSet, int* maxFd){
	removeFromFdSetUpdatingMax(currentFd, selectFdSet, maxFd);
	
	pthread_mutex_lock_error(&incomingConnectionsLock, "Error while locking on incoming connections queue");
	queuePush(&incomingConnectionsQueue, currentFd);
	pthread_cond_signal_error(&incomingConnectionsCond, "Error while signaling incoming connection");
	pthread_mutex_unlock_error(&incomingConnectionsLock, "Error while locking on incoming connections queue");
	return 0;
}