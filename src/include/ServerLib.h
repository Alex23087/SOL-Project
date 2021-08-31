#ifndef SOL_PROJECT_SERVERLIB_H
#define SOL_PROJECT_SERVERLIB_H

#define MAX_BACKLOG 10
#define LOG_BUFFER_SIZE 256
#define LOG_TERMINATE 0x42

#include <pthread.h>
#include <sys/select.h>

#include "../include/FileCache.h"
#include "../include/Queue.h"



extern ClientList* clientList;
extern pthread_rwlock_t clientListLock;
extern unsigned int clientsConnected;
extern FileCache* fileCache;
extern pthread_rwlock_t fileCacheLock;
extern pthread_mutex_t incomingConnectionsLock;
extern pthread_cond_t incomingConnectionsCond;
extern int logPipeDescriptors[2];
extern bool workersShouldTerminate;



void addToFdSetUpdatingMax(int fd, fd_set* fdSet, int* maxFd);

void closeAllClientDescriptors();

bool fileExistsL(const char* filename);

int getClientWaitingForLockL(const char* filename);

CachedFile* getFileL(const char* filename);

bool isFileOpenedByClientL(const char* filename, int descriptor);

void pthread_cond_broadcast_error(pthread_cond_t* cond, const char* msg);

void pthread_cond_signal_error(pthread_cond_t* cond, const char* msg);

void pthread_cond_wait_error(pthread_cond_t* cond, pthread_mutex_t* lock, const char* msg);

void pthread_join_error(pthread_t thread, const char* msg);

void pthread_mutex_lock_error(pthread_mutex_t* lock, const char* msg);

void pthread_mutex_unlock_error(pthread_mutex_t* lock, const char* msg);

void pthread_rwlock_rdlock_error(pthread_rwlock_t* lock, const char* msg);

void pthread_rwlock_unlock_error(pthread_rwlock_t* lock, const char* msg);

void pthread_rwlock_wrlock_error(pthread_rwlock_t* lock, const char* msg);

void removeFromFdSetUpdatingMax(int fd, fd_set* fdSet, int* maxFd);

void serverDisconnectClientL(int clientFd, bool lockClientList);

int serverEvictFile(const char* fileToExclude, const char* operation, int fdToServe, int workerID);

bool serverLockFileL(int workerID, int fdToServe, const char* filename, CachedFile *file, bool sendAck);

void serverLog(const char* format, ...);

void serverRemoveFile(const char* filename, int workerID);

void serverRemoveFileL(const char* filename, int workerID);

void serverSignalFileUnlockL(CachedFile* file, int workerID, int desc);

void terminateServer(short *running);

void unlockAllFilesLockedByClient(FileCache* fileCache, int clientFd);

void updateClientStatusL(ClientOperation op, int messageLength, const char* filename, int fdToServe);

#endif //SOL_PROJECT_SERVERLIB_H
