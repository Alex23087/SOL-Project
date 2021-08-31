#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>

#include "../include/ClientAPI.h"
#include "../include/FileCache.h"
#include "../include/ion.h"
#include "../include/ServerLib.h"
#include "../include/W2M.h"



ClientList* clientList = NULL;
pthread_rwlock_t clientListLock = PTHREAD_RWLOCK_INITIALIZER;
unsigned int clientsConnected = 0;
FileCache* fileCache = NULL;
pthread_rwlock_t fileCacheLock = PTHREAD_RWLOCK_INITIALIZER; //Needed to add and remove files
pthread_mutex_t incomingConnectionsLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t incomingConnectionsCond = PTHREAD_COND_INITIALIZER;
int logPipeDescriptors[2];
bool workersShouldTerminate = false;



static void sendErrorToAllClientsWaitingForLock(ClientList* clientList, const char* filename, int workerID){
	ClientList* current = clientList;
	if(current == NULL){
		return;
	}
	
	ConnectionStatus connectedStatus;
	connectedStatus.op = Connected;
	connectedStatus.data.filesToRead = 0;
	connectedStatus.data.messageLength = 0;
	connectedStatus.data.filename = NULL;
	
	while(current != NULL){
		if(current->status.op == WaitingForLock && strcmp(current->status.data.filename, filename) == 0){
			serverLog("[Worker #%d]: Client %d was waiting for lock, sending error\n", workerID, current->descriptor);
			clientListUpdateStatus(clientList, current->descriptor, connectedStatus);
			fcpSend(FCP_ERROR, ENOENT, NULL, current->descriptor);
			w2mSend(W2M_CLIENT_SERVED, current->descriptor);
		}
		current = current->next;
	}
}



void addToFdSetUpdatingMax(int fd, fd_set* fdSet, int* maxFd){
    FD_SET(fd, fdSet);
    if(fd > *maxFd){
        *maxFd = fd;
    }
}

void closeAllClientDescriptors(){
	pthread_rwlock_wrlock_error(&clientListLock, "Error while locking client list");
	while(clientList != NULL){
        int desc = clientList->descriptor;
		serverDisconnectClientL(desc, false);
	}
	pthread_rwlock_unlock_error(&clientListLock, "Error while unlocking client list");
}

bool fileExistsL(const char* filename){
    pthread_rwlock_rdlock_error(&fileCacheLock, "Error while locking on file cache");
    bool exists = fileExists(fileCache, filename);
    pthread_rwlock_unlock_error(&fileCacheLock, "Error while unlocking on file cache");
    return exists;
}

int getClientWaitingForLockL(const char* filename){
	int out = -1;
	pthread_rwlock_wrlock_error(&clientListLock, "Error while locking on client list");
	ClientList* current = clientList;
	while(current != NULL){
		if(current->status.op == WaitingForLock && strcmp(current->status.data.filename, filename) == 0){
			out = current->descriptor;
			break;
		}
		current = current->next;
	}
	pthread_rwlock_unlock_error(&clientListLock, "Error while unlocking on client list");
	return out;
}

CachedFile* getFileL(const char* filename){
    pthread_rwlock_rdlock_error(&fileCacheLock, "Error while locking file cache");
    CachedFile* file = getFile(fileCache, filename);
    pthread_rwlock_unlock_error(&fileCacheLock, "Error while unlocking file cache");
    return file;
}

bool isFileOpenedByClientL(const char* filename, int descriptor){
    pthread_rwlock_rdlock_error(&clientListLock, "Error while locking on client list");
    bool isOpen = isFileOpenedByClient(clientList, filename, descriptor);
    pthread_rwlock_unlock_error(&clientListLock, "Error while unlocking on client list");
    return isOpen;
}

void pthread_cond_broadcast_error(pthread_cond_t* cond, const char* msg){
    if(pthread_cond_broadcast(cond)){
        perror(msg);
    }
}

void pthread_cond_signal_error(pthread_cond_t* cond, const char* msg){
    if(pthread_cond_signal(cond)){
        perror(msg);
    }
}

void pthread_cond_wait_error(pthread_cond_t* cond, pthread_mutex_t* lock, const char* msg){
    if(pthread_cond_wait(cond, lock)){
        perror(msg);
    }
}

void pthread_join_error(pthread_t thread, const char* msg){
    if(pthread_join(thread, NULL)){
        perror(msg);
    }
}

void pthread_mutex_lock_error(pthread_mutex_t* lock, const char* msg){
	if(pthread_mutex_lock(lock)){
		perror(msg);
	}
}

void pthread_mutex_unlock_error(pthread_mutex_t* lock, const char* msg){
	if(pthread_mutex_unlock(lock)){
		perror(msg);
	}
}

void pthread_rwlock_rdlock_error(pthread_rwlock_t* lock, const char* msg){
	if(pthread_rwlock_rdlock(lock)){
		perror(msg);
	}
}

void pthread_rwlock_unlock_error(pthread_rwlock_t* lock, const char* msg){
    if(pthread_rwlock_unlock(lock)){
        perror(msg);
    }
}

void pthread_rwlock_wrlock_error(pthread_rwlock_t* lock, const char* msg){
	if(pthread_rwlock_wrlock(lock)){
		perror(msg);
	}
}

void removeFromFdSetUpdatingMax(int fd, fd_set* fdSet, int* maxFd){
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

void serverDisconnectClientL(int clientFd, bool lockClientList){
	clientsConnected--;
	
	if(lockClientList){
		pthread_rwlock_wrlock_error(&clientListLock, "Error while locking client list");
	}
	clientListRemove(&clientList, clientFd);
	if(lockClientList){
		pthread_rwlock_unlock_error(&clientListLock, "Error while unlocking client list");
	}
	serverLog("[Master]: Client %d disconnected\n",clientFd);
	
	//Unlock files locked by the client
	pthread_rwlock_wrlock_error(&fileCacheLock, "Error while locking file cache");
	unlockAllFilesLockedByClient(fileCache, clientFd);
	pthread_rwlock_unlock_error(&fileCacheLock, "Error while unlocking file cache");
}

int serverEvictFile(const char* fileToExclude, const char* operation, int fdToServe, int workerID){
	const char* evictedFileName = getFileToEvict(fileCache, fileToExclude);
	if(evictedFileName == NULL){
		serverLog("[Worker #%d]: %s request can't be fulfilled because of a capacity fault, no file can be evicted to fulfill it\n", workerID, operation);
		return -1;
	}else{
		serverLog("[Worker #%d]: %s request can't be fulfilled because of a capacity fault, evicting file \"%s\"\n", workerID, operation, evictedFileName);
		CachedFile* evictedFile = getFile(fileCache, evictedFileName);
        if(evictedFile == NULL  ){
            serverLog("[Worker #%d]: %s request can't be fulfilled because of a capacity fault, no file can be evicted to fulfill it\n", workerID, operation);
            return -1;
        }
		pthread_mutex_lock_error(evictedFile->lock, "Error while locking on file");
		char* evictedFileBuffer = NULL;
		size_t evictedFileSize = 0;
		readCachedFile(evictedFile, &evictedFileBuffer, &evictedFileSize);
		fcpSend(FCP_WRITE, (int32_t)evictedFileSize, (char*)evictedFileName, fdToServe);
		ssize_t bytesSent = writen(fdToServe, evictedFileBuffer, evictedFileSize);
		free(evictedFileBuffer);
		serverLog("[Worker #%d]: Sent file to client %d, %ld bytes transferred\n", workerID, fdToServe, bytesSent);
		pthread_mutex_unlock_error(evictedFile->lock, "Error while unlocking file");
		serverRemoveFile(evictedFileName, workerID);
		return 0;
	}
}

void serverLog(const char* format, ...){
    va_list args;
    va_start(args, format);

    char* buffer = malloc(LOG_BUFFER_SIZE);
    memset(buffer, 0, LOG_BUFFER_SIZE);
    vsnprintf(buffer, LOG_BUFFER_SIZE, format, args);
    writen(logPipeDescriptors[1], buffer, LOG_BUFFER_SIZE);
    free(buffer);

    va_end(args);
}

void serverRemoveFile(const char* filename, int workerID){
	pthread_rwlock_wrlock_error(&clientListLock, "Error while locking on client list");
	closeFileForEveryone(clientList, filename);
	sendErrorToAllClientsWaitingForLock(clientList, filename, workerID);
	pthread_rwlock_unlock_error(&clientListLock, "Error while unlocking on client list");
	removeFileFromCache(fileCache, filename);
}

void serverRemoveFileL(const char* filename, int workerID){
    pthread_rwlock_wrlock_error(&clientListLock, "Error while locking on client list");
    closeFileForEveryone(clientList, filename);
    sendErrorToAllClientsWaitingForLock(clientList, filename, workerID);
    pthread_rwlock_unlock_error(&clientListLock, "Error while unlocking on client list");
    pthread_rwlock_wrlock_error(&fileCacheLock, "Error while locking on file cache");
    removeFileFromCache(fileCache, filename);
    pthread_rwlock_unlock_error(&fileCacheLock, "Error while unlocking on file cache");
}

void serverSignalFileUnlockL(CachedFile* file, int workerID, int desc){
	serverLog("[Worker #%d]: Passing lock to client %d\n", workerID, desc);
	pthread_mutex_lock_error(file->lock, "Error while locking file");
	if(file->lockedBy == -1){
		file->lockedBy = desc;
	}
	pthread_mutex_unlock_error(file->lock, "Error while unlocking file");
	updateClientStatusL(Connected, 0, NULL, desc);
	fcpSend(FCP_ACK, 0, NULL, desc);
	serverLog("[Worker #%d]: Client %d successfully locked the file\n", workerID, desc);
	w2mSend(W2M_CLIENT_SERVED, desc);
}

void terminateServer(short *running){
	*running = false;
	workersShouldTerminate = true;
	
	closeAllClientDescriptors();
	
	pthread_mutex_lock_error(&incomingConnectionsLock, "Error while locking incoming connections");
	pthread_cond_broadcast_error(&incomingConnectionsCond, "Error while broadcasting stop message");
	pthread_mutex_unlock_error(&incomingConnectionsLock, "Error while unlocking incoming connections");
}

void unlockAllFilesLockedByClient(FileCache* fileCache, int clientFd){
    CachedFile* current = getFileLockedByClient(fileCache, clientFd);
    while(current != NULL){
        pthread_mutex_lock_error(current->lock, "Error while locking file");
        current->lockedBy = -1;
        pthread_mutex_unlock_error(current->lock, "Error while unlocking file");
        current = getFileLockedByClient(fileCache, clientFd);
    }
}

void updateClientStatusL(ClientOperation op, int messageLength, const char* filename, int fdToServe){
    ConnectionStatus newStatus;
    newStatus.op = op;
    newStatus.data.messageLength = messageLength;
    if(filename != NULL){
        size_t filenameLength = strlen(filename) + 1;
        newStatus.data.filename = malloc(filenameLength);
        strncpy(newStatus.data.filename, filename, filenameLength);
    }else{
        newStatus.data.filename = NULL;
    }
    pthread_rwlock_wrlock_error(&clientListLock, "Error while locking client list");
    clientListUpdateStatus(clientList, fdToServe, newStatus);
    pthread_rwlock_unlock_error(&clientListLock, "Error while unlocking client list");
}
