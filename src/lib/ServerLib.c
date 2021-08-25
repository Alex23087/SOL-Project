#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../include/ClientAPI.h"
#include "../include/defines.h"
#include "../include/FileCache.h"
#include "../include/FileCachingProtocol.h"
#include "../include/ion.h"
#include "../include/ParseUtils.h"
#include "../include/Queue.h"
#include "../include/ServerLib.h"
#include "../include/W2M.h"



ClientList* clientList = NULL;
pthread_rwlock_t clientListLock = PTHREAD_RWLOCK_INITIALIZER;
FileCache* fileCache = NULL;
pthread_rwlock_t fileCacheLock = PTHREAD_RWLOCK_INITIALIZER; //Needed to add and remove files
int logPipeDescriptors[2];



void addToFdSetUpdatingMax(int fd, fd_set* fdSet, int* maxFd){
    FD_SET(fd, fdSet);
    if(fd > *maxFd){
        *maxFd = fd;
    }
}

bool fileExistsL(const char* filename){
    pthread_rwlock_rdlock_error(&fileCacheLock, "Error while locking on file cache");
    bool exists = fileExists(fileCache, filename);
    pthread_rwlock_unlock_error(&fileCacheLock, "Error while unlocking on file cache");
    return exists;
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
        //TODO: Handle error
    }
}

void pthread_cond_signal_error(pthread_cond_t* cond, const char* msg){
    if(pthread_cond_signal(cond)){
        perror(msg);
        //TODO: Handle error, maybe send message to master
    }
}

void pthread_cond_wait_error(pthread_cond_t* cond, pthread_mutex_t* lock, const char* msg){
    if(pthread_cond_wait(cond, lock)){
        perror(msg);
        //TODO: Handle error, maybe send message to master
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
		//TODO: Handle error, maybe send message to master
	}
}

void pthread_mutex_unlock_error(pthread_mutex_t* lock, const char* msg){
	if(pthread_mutex_unlock(lock)){
		perror(msg);
		//TODO: Handle error, maybe send message to master
	}
}

void pthread_rwlock_rdlock_error(pthread_rwlock_t* lock, const char* msg){
	if(pthread_rwlock_rdlock(lock)){
		perror(msg);
		//TODO: Handle error, maybe send message to master
	}
}

void pthread_rwlock_unlock_error(pthread_rwlock_t* lock, const char* msg){
    if(pthread_rwlock_unlock(lock)){
        perror(msg);
        //TODO: Handle error, maybe send message to master
    }
}

void pthread_rwlock_wrlock_error(pthread_rwlock_t* lock, const char* msg){
	if(pthread_rwlock_wrlock(lock)){
		perror(msg);
		//TODO: Handle error, maybe send message to master
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

void serverRemoveFile(const char* filename){
	pthread_rwlock_wrlock_error(&clientListLock, "Error while locking on client list");
	closeFileForEveryone(clientList, filename);
	pthread_rwlock_unlock_error(&clientListLock, "Error while unlocking on client list");
	removeFileFromCache(fileCache, filename);
}

void serverRemoveFileL(const char* filename){
    pthread_rwlock_wrlock_error(&clientListLock, "Error while locking on client list");
    closeFileForEveryone(clientList, filename);
    pthread_rwlock_unlock_error(&clientListLock, "Error while unlocking on client list");
    pthread_rwlock_wrlock_error(&fileCacheLock, "Error while locking on file cache");
    removeFileFromCache(fileCache, filename);
    pthread_rwlock_unlock_error(&fileCacheLock, "Error while unlocking on file cache");
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
