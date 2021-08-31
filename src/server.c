#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "include/ClientAPI.h"
#include "include/FileCache.h"
#include "include/ion.h"
#include "include/ParseUtils.h"
#include "include/Queue.h"
#include "include/ServerLib.h"
#include "include/TimespecUtils.h"
#include "include/W2M.h"

#define TIME_STRING_SIZE 20

#define cleanup() \
	unlink(socketPath);\
	free(socketPath);\
	free(logFilePath);

typedef enum{
	NoTime,
	Timestamp,
    TimestampMicro,
	Formatted
} LogTimeFormat;

static unsigned int clientsConnectedMax = 0;
static Queue* incomingConnectionsQueue = NULL;
static char* logFilePath = NULL;
static short logMode = O_APPEND;
static LogTimeFormat logTimeFormat = Timestamp;
static unsigned int* requestsServed;



static int workerDisconnectClient(int workerN, int fdToServe);
static int onNewConnectionReceived(int serverSocketDescriptor, fd_set* selectFdSet, int* maxFd);
static int onW2MMessageReceived(int serverSocketDescriptor, fd_set* selectFdSet, int* maxFd, bool* running, bool* hangup);
static int onConnectedClientMessage(int currentFd, fd_set* selectFdSet, int* maxFd);



//Signal handler thread
static void* signalHandlerThread(void* arg){
	//Masking the signals we'll listen to
	sigset_t listenSet;
	sigemptyset(&listenSet);
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
			serverLog("[Signal]: Received signal %s\n", signalReceived == SIGINT ? "SIGINT" : "SIGQUIT");
			w2mSend(W2M_SIGNAL_TERM, 0);
			break;
		}
		case SIGHUP:{
			serverLog("[Signal]: Received signal SIGHUP\n");
			w2mSend(W2M_SIGNAL_HANG, 0);
			break;
		}
	}
	
	return 0;
}


bool serverLockFileL(int workerID, int fdToServe, const char* filename, CachedFile *file, bool sendAck) {
    bool locked = false;
    pthread_mutex_lock_error(file->lock, "Error while locking file");
    if(file->lockedBy == -1 || file->lockedBy == fdToServe){
        locked = true;
        file->lockedBy = fdToServe;
    }
    pthread_mutex_unlock_error(file->lock, "Error while unlocking file");

    if(locked){
        serverLog("[Worker #%d]: Client %d successfully locked the file\n", workerID, fdToServe);
        if(sendAck) {
            fcpSend(FCP_ACK, 0, NULL, fdToServe);
        }
    }else{
        serverLog("[Worker #%d]: Client %d has to wait for lock\n", workerID, fdToServe);
        updateClientStatusL(WaitingForLock, 0, filename, fdToServe);
    }
    return locked;
}

//Worker thread
static void* workerThread(void* arg){
    int workerID = (int)(long)arg;
    serverLog("[Worker #%d]: Up and running\n", workerID);
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
#ifdef DEBUG
        serverLog("[Worker #%d]: Serving client on descriptor %d\n", workerID, fdToServe);
#endif
        pthread_rwlock_rdlock_error(&clientListLock, "Error while locking client list");
        ConnectionStatus status = clientListGetStatus(clientList, fdToServe);
        pthread_rwlock_unlock_error(&clientListLock, "Error while unlocking client list");
        switch(status.op){
            case Connected:{
                char fcpBuffer[FCP_MESSAGE_LENGTH];
                ssize_t fcpBytesRead = readn(fdToServe, fcpBuffer, FCP_MESSAGE_LENGTH);

                //TODO: Handle wrong number of bytes read
                if(fcpBytesRead == 0){
                    //Client disconnected
                    if(workerDisconnectClient(workerID, fdToServe)){
                        //TODO: Handle error
                    }
                }else{
                    requestsServed[workerID]++;
                    FCPMessage* fcpMessage = fcpMessageFromBuffer(fcpBuffer);
                    switch(fcpMessage->op){
                        case FCP_WRITE:
                        case FCP_APPEND:{
                            //Client has issued a write or append request: update client status, send ack, warn master
                            bool append = (fcpMessage->op) == FCP_APPEND;
                            serverLog("[Worker #%d]: Client %d issued op: %d (%s), size: %d, filename: \"%s\"\n", workerID, fdToServe, fcpMessage->op, append ? "FCP_APPEND" : "FCP_WRITE", fcpMessage->control, fcpMessage->filename);

                            //Check if the client can write on this file
                            int error = 0;
                            CachedFile* file = getFileL(fcpMessage->filename);

                            if(file != NULL){
                                bool isOpen = isFileOpenedByClientL(fcpMessage->filename, fdToServe);

                                if(isOpen){
                                    pthread_mutex_lock_error(file->lock, "Error while locking file");
                                    if(file->lockedBy != fdToServe){
                                        //File is not locked by this client
                                        error = EPERM;
                                    }
                                    pthread_mutex_unlock_error(file->lock, "Error while unlocking file");
                                }else{
                                    //File is not opened by this client
                                    error = EBADF;
                                }
                            }else{
                                //File does not exist
                                error = ENOENT;
                            }

                            if(error == 0){
                                //Client can write to file, check for capacity faults
                                bool capacityError = false;
                                pthread_rwlock_wrlock_error(&fileCacheLock, "Error while locking file cache");
                                pthread_mutex_lock_error(file->lock, "Error while locking on file");
                                while(!canFitNewData(fileCache, fcpMessage->filename, fcpMessage->control, append)){
                                	if(serverEvictFile(fcpMessage->filename, append ? "Append" : "Write", fdToServe, workerID)){
                                		capacityError = true;
                                        break;
                                	}
                                }
                                pthread_mutex_unlock_error(file->lock, "Error while unlocking on file");
                                if(capacityError && getFileSize(file) == 0){
                                    serverRemoveFile(file->filename, fdToServe);
                                }
                                pthread_rwlock_unlock_error(&fileCacheLock, "Error while unlocking file cache");

                                if(capacityError){
                                    fcpSend(FCP_ERROR, EFBIG, NULL, fdToServe);
                                }else{
                                    //Enough capacity for the file, send ack to the client
                                    updateClientStatusL(append ? AppendingToFile : SendingFile, fcpMessage->control, fcpMessage->filename, fdToServe);

                                    fcpSend(FCP_ACK, 0, NULL, fdToServe);
                                }
                            }else{
                                //Invalid request, warn client
                                serverLog("[Worker #%d]: Client %d tried to %s to a file %s\n", workerID, fdToServe, append ? "append" : "write", errno == ENOENT ? "that didn't exist" : "that wasn't locked by it");
                                fcpSend(FCP_ERROR, error, NULL, fdToServe);
                            }
                            w2mSend(W2M_CLIENT_SERVED, fdToServe);
                            break;
                        }
                        case FCP_OPEN:{
                            //Client has issued an open request: check legitimacy of the request, send error or open and send ack, warn master
                            serverLog("[Worker #%d]: Client %d issued op: %d (FCP_OPEN), flags: %d, filename: \"%s\"\n", workerID, fdToServe, fcpMessage->op, fcpMessage->control, fcpMessage->filename);

                            if(fcpMessage->control != 0 && fcpMessage->control != 1 && fcpMessage->control != 2 && fcpMessage->control != 3){
                                //Invalid flags
                                serverLog("[Worker #%d]: Client %d has requested an invalid open with flags %d\n", workerID, fdToServe, fcpMessage->control);
                                workerDisconnectClient(workerID, fdToServe);
                                break;
                            }

                            int error = 0;
                            bool exists = fileExistsL(fcpMessage->filename);

                            bool createIsSet = FCP_OPEN_FLAG_ISSET(fcpMessage->control, O_CREATE);
                            if(exists && createIsSet){
                                error = EEXIST;
                            }else if(!exists && !createIsSet){
                                error = ENOENT;
                            }

                            if(error){
                                serverLog("[Worker #%d]: Client %d tried to %s\n", workerID, fdToServe, error == EEXIST ? "create a file that already exists" : "open a file that doesn't exist");
                                fcpSend(FCP_ERROR, error, NULL, fdToServe);
                            }else{
                            	bool capacityError = false;
                                CachedFile* file = NULL;
                                pthread_rwlock_wrlock_error(&fileCacheLock, "Error while locking on file cache");
                                if(createIsSet){
                                	if(!canFitNewFile(fileCache)){
                                	    if(serverEvictFile(" ", "Open", fdToServe, workerID)){
                                	    	capacityError = true;
                                	    }
                                	}
                                	if(!capacityError){
                                        char* fn = malloc(MAX_FILENAME_SIZE);
                                        strncpy(fn, fcpMessage->filename, MAX_FILENAME_SIZE-1);
                                		file = createFile(fileCache, fn);
                                        free(fn);
                                	}
                                }else{
                                    char* fn = malloc(MAX_FILENAME_SIZE);
                                    strncpy(fn, fcpMessage->filename, MAX_FILENAME_SIZE-1);
                                    file = getFile(fileCache, fn);
                                    free(fn);
                                }
                                pthread_rwlock_unlock_error(&fileCacheLock, "Error while unlocking on file cache");
								
                                if(capacityError || file == NULL){
                                	fcpSend(FCP_ERROR, EMFILE, NULL, fdToServe);
                                }else{
                                    pthread_rwlock_wrlock_error(&fileCacheLock, "Error while locking on file cache");
                                    setFileOpened(clientList, fdToServe, fcpMessage->filename);
                                    pthread_rwlock_unlock_error(&fileCacheLock, "Error while unlocking on file cache");

                                	bool lockIsSet = FCP_OPEN_FLAG_ISSET(fcpMessage->control, O_LOCK);
                                	if(lockIsSet){
                                		bool locked = serverLockFileL(workerID, fdToServe, fcpMessage->filename, file, false);
                                        if(!locked){
                                            break;
                                        }
                                	}

                                	serverLog("[Worker #%d]: Client %d successfully %s the file\n", workerID, fdToServe, lockIsSet ? "opened and locked" : "opened");
                                	fcpSend(FCP_ACK, 0, NULL, fdToServe);
                                }
                            }

                            w2mSend(W2M_CLIENT_SERVED, fdToServe);
                            break;
                        }
                        case FCP_READ:{
                            //Client has issued a read request: check legitimacy of the request, send file, warn master
                            serverLog("[Worker #%d]: Client %d issued op: %d (FCP_READ), filename: \"%s\"\n", workerID, fdToServe, fcpMessage->op, fcpMessage->filename);

                            //Check if the client can read on this file
                            int error = 0;
                            CachedFile* file = getFileL(fcpMessage->filename);

                            if(file != NULL){
                                bool isOpen = isFileOpenedByClientL(fcpMessage->filename, fdToServe);

                                if(isOpen){
                                    pthread_mutex_lock_error(file->lock, "Error while locking file");
                                    if(file->lockedBy != fdToServe){
                                        //File is not locked by this client
                                        error = EPERM;
                                    }
                                    pthread_mutex_unlock_error(file->lock, "Error while unlocking file");
                                }else{
                                    //File is not opened by this client
                                    error = EBADF;
                                }
                            }else{
                                //File does not exist
                                error = ENOENT;
                            }

                            if(error == 0){
                                //Set client status and send file info to client
                                pthread_mutex_lock_error(file->lock, "Error while locking file");
                                size_t fileSize = getUncompressedSize(file);
                                pthread_mutex_unlock_error(file->lock, "Error while unlocking file");

                                updateClientStatusL(ReceivingFile, (int)fileSize, fcpMessage->filename, fdToServe);

                                fcpSend(FCP_WRITE, (int)fileSize, fcpMessage->filename, fdToServe);
                            }else{
                                //Invalid request, warn client
                                serverLog("[Worker #%d]: Client %d tried to read a file %s\n", workerID, fdToServe, errno == ENOENT ? "that didn't exist" : "that wasn't locked by it");
                                fcpSend(FCP_ERROR, error, NULL, fdToServe);
                            }
                            w2mSend(W2M_CLIENT_SERVED, fdToServe);
                            break;
                        }
                        case FCP_CLOSE:{
                            //Client has issued a close request, check if file exists, if it's open
                            serverLog("[Worker #%d]: Client %d issued op: %d (FCP_CLOSE), filename: \"%s\"\n", workerID, fdToServe, fcpMessage->op, fcpMessage->filename);

                            int error = 0;
                            bool exists = fileExistsL(fcpMessage->filename);

                            if(exists){
                                bool isOpen = isFileOpenedByClientL(fcpMessage->filename, fdToServe);

                                if(isOpen){
                                    CachedFile* file = getFileL(fcpMessage->filename);

                                    //Unlocking file if it was locked by client
                                    int desc = -1;
                                    pthread_mutex_lock_error(file->lock, "Error while locking file");
                                    if(file->lockedBy == fdToServe){
                                        file -> lockedBy = -1;
                                        desc = getClientWaitingForLockL(file->filename);
                                    }
                                    pthread_mutex_unlock_error(file->lock, "Error while unlocking file");
                                    
                                    //Pass lock to next client
                                    if(desc != -1){
                                    	serverSignalFileUnlockL(file, workerID, desc);
                                    }
                                    
                                    //Closing file
                                    pthread_rwlock_rdlock_error(&clientListLock, "Error while locking on client list");
                                    setFileClosed(clientList, fdToServe, fcpMessage->filename);
                                    pthread_rwlock_unlock_error(&clientListLock, "Error while unlocking on client list");

                                    //Everything went well, file is closed, warn client and master
                                    serverLog("[Worker #%d]: Client %d successfully closed the file\n", workerID, fdToServe);
                                    fcpSend(FCP_ACK, 0, NULL, fdToServe);
                                    w2mSend(W2M_CLIENT_SERVED, fdToServe);
                                }else{
                                    //Trying to close a file that's not open, send error
                                    serverLog("[Worker #%d]: Client %d tried to close file \"%s\", which isn't open by it\n", workerID, fdToServe, fcpMessage->filename);
                                    error = EBADF;
                                    fcpSend(FCP_ERROR, error, NULL, fdToServe);
                                    w2mSend(W2M_CLIENT_SERVED, fdToServe);
                                }
                            }else{
                                //Trying to close a nonexistent file, send error
                                serverLog("[Worker #%d]: Client %d tried to close file \"%s\", which doesn't exist\n", workerID, fdToServe, fcpMessage->filename);
                                error = ENOENT;
                                fcpSend(FCP_ERROR, error, NULL, fdToServe);
                                w2mSend(W2M_CLIENT_SERVED, fdToServe);
                            }
                            break;
                        }
                        case FCP_LOCK:{
                            //Client has issued a lock request: check legitimacy of the request, send error or lock and send ack, warn master
                            serverLog("[Worker #%d]: Client %d issued op: %d (FCP_LOCK), filename: \"%s\"\n", workerID, fdToServe, fcpMessage->op, fcpMessage->filename);

                            int error = 0;
                            bool exists = fileExistsL(fcpMessage->filename);
                            bool locked = false;
                            
                            if(!exists){
                                error = ENOENT;
                                serverLog("[Worker #%d]: Client %d tried to lock a file that doesn't exist\n", workerID, fdToServe);
                                fcpSend(FCP_ERROR, error, NULL, fdToServe);
                            }else{
                                bool isOpen = isFileOpenedByClientL(fcpMessage->filename, fdToServe);
                                if(isOpen){
                                    CachedFile* file = getFileL(fcpMessage->filename);

                                    locked = serverLockFileL(workerID, fdToServe, fcpMessage->filename, file, true);

                                }else{
                                    error = EBADF;
                                    serverLog("[Worker #%d]: Client %d tried to lock a file that it didn't open\n", workerID, fdToServe);
                                    fcpSend(FCP_ERROR, error, NULL, fdToServe);
                                }
                            }

                            if(locked){
                            	w2mSend(W2M_CLIENT_SERVED, fdToServe);
                            }
                            break;
                        }
                        case FCP_UNLOCK:{
                            //Client has issued an unlock request: check legitimacy of the request, send error or unlock and send ack, warn master
                            serverLog("[Worker #%d]: Client %d issued op: %d (FCP_UNLOCK), filename: \"%s\"\n", workerID, fdToServe, fcpMessage->op, fcpMessage->filename);

                            int error = 0;
                            bool exists = fileExistsL(fcpMessage->filename);

                            if(!exists){
                                error = ENOENT;
                                serverLog("[Worker #%d]: Client %d tried to unlock a file that doesn't exist\n", workerID, fdToServe);
                                fcpSend(FCP_ERROR, error, NULL, fdToServe);
                            }else{
                                CachedFile* file = getFileL(fcpMessage->filename);

                                int desc = -1;
                                pthread_mutex_lock_error(file->lock, "Error while locking file");
                                if(file->lockedBy == fdToServe){
                                    file -> lockedBy = -1;
                                    desc = getClientWaitingForLockL(file->filename);
                                }else{
                                    error = EPERM;
                                }
                                pthread_mutex_unlock_error(file->lock, "Error while unlocking file");

                                if(error == 0){
                                    serverLog("[Worker #%d]: Client %d successfully unlocked the file\n", workerID, fdToServe);
                                    fcpSend(FCP_ACK, 0, NULL, fdToServe);
                                    //Pass lock to next client
                                    if(desc != -1){
                                    	serverSignalFileUnlockL(file, workerID, desc);
                                    }
                                }else{
                                    serverLog("[Worker #%d]: Client %d tried to unlock a file it didn't lock\n", workerID, fdToServe);
                                    fcpSend(FCP_ERROR, error, NULL, fdToServe);
                                }
                            }

                            w2mSend(W2M_CLIENT_SERVED, fdToServe);
                            break;
                        }
                        case FCP_REMOVE:{
                            //Client has issued a remove request: check legitimacy of the request, send error or remove and send ack, warn master
                            serverLog("[Worker #%d]: Client %d issued op: %d (FCP_REMOVE),  filename: \"%s\"\n", workerID, fdToServe, fcpMessage->op, fcpMessage->filename);

                            int error = 0;
                            bool exists = fileExistsL(fcpMessage->filename);

                            if(!exists){
                                //File doesn't exist, send error to client
                                serverLog("[Worker #%d]: Client %d tried to remove a file that doesn't exist\n", workerID, fdToServe);
                                error = ENOENT;
                                fcpSend(FCP_ERROR, error, NULL, fdToServe);
                            }else{
                                if(!isFileOpenedByClientL(fcpMessage->filename, fdToServe)){
                                    //File is not opened by the client, send error message
                                    serverLog("[Worker #%d]: Client %d tried to remove a file that it didn't open\n", workerID, fdToServe);
                                    error = EBADF;
                                    fcpSend(FCP_ERROR, error, NULL, fdToServe);
                                }else{
                                    CachedFile* file = getFileL(fcpMessage->filename);
                                    pthread_mutex_lock_error(file->lock, "Error while locking file");
                                    bool isFileLockedByClient = ((file->lockedBy) == fdToServe);
                                    pthread_mutex_unlock_error(file->lock, "Error while unlocking file");

                                    if(isFileLockedByClient){
                                        serverRemoveFileL(fcpMessage->filename, workerID);
                                        serverLog("[Worker #%d]: Client %d successfully removed file with filename: %s\n", workerID, fdToServe, fcpMessage->filename);
                                        fcpSend(FCP_ACK, 0, NULL, fdToServe);
                                    }else{
                                        //File is not locked by the client, return error
                                        serverLog("[Worker #%d]: Client %d tried to remove a file it didn't hold a lock on\n", workerID, fdToServe);
                                        error = EPERM;
                                        fcpSend(FCP_ERROR, error, NULL, fdToServe);
                                    }
                                }
                            }

                            w2mSend(W2M_CLIENT_SERVED, fdToServe);
                            break;
                        }
                        case FCP_READ_N:{
                            //Client has issued a readN request: send files, warn master
                            serverLog("[Worker #%d]: Client %d issued op: %d (FCP_READ_N), n: %d\n", workerID, fdToServe, fcpMessage->op, fcpMessage->control);
                            int n = fcpMessage->control;
                            if(n > 0){
                                serverLog("[Worker #%d]: Sending %d files\n", workerID, n);
                            }else{
                                serverLog("[Worker #%d]: Sending all files\n", workerID);
                            }
                            int counter = 0;
                            pthread_rwlock_rdlock_error(&fileCacheLock, "Error while locking file cache");
                            FileList* current = fileCache->files;
                            while(((n <= 0) || (counter < n)) && current != NULL){
                                pthread_mutex_lock_error(current->file->lock, "Error while locking file");
                                if((current->file->lockedBy == -1 || current->file->lockedBy == fdToServe) && getFileSize(current->file) != 0) {
                                    size_t fileSize = 0;
                                    char *fileBuffer = NULL;
                                    readCachedFile(current->file, &fileBuffer, &fileSize);

                                    serverLog("[Worker #%d]: Sending file \"%s\" to client %d\n", workerID,
                                              current->file->filename, fdToServe);
                                    fcpSend(FCP_WRITE, fileSize, current->file->filename, fdToServe);
                                    ssize_t bytesTransferred = writen(fdToServe, fileBuffer, fileSize);
                                    serverLog("[Worker #%d]: Sent file \"%s\" to client %d, bytes transferred: %ld\n",
                                              workerID, current->file->filename, fdToServe, bytesTransferred);

                                    free(fileBuffer);
                                    counter++;
                                }
                                pthread_mutex_unlock_error(current->file->lock, "Error while unlocking file");
                                current = current->next;
                            }
                            pthread_rwlock_unlock_error(&fileCacheLock, "Error while unlocking file cache");

                            //Files sent, send ack to client and warn server
                            fcpSend(FCP_ACK, 0, NULL, fdToServe);

                            w2mSend(W2M_CLIENT_SERVED, fdToServe);
                            break;
                        }
                        default:{
                            //Client has requested an invalid operation, forcibly disconnect it
                            serverLog("[Worker #%d]: Client %d has requested an invalid operation with opcode %d\n", workerID, fdToServe, fcpMessage->op);
                            workerDisconnectClient(workerID, fdToServe);
                            break;
                        }
                    }
                    free(fcpMessage);
                }
                break;
            }
            case SendingFile:
            case AppendingToFile:{
                bool append = status.op == AppendingToFile;
                int32_t fileSize = status.data.messageLength;

                int error = 0;
                char* buffer = malloc(fileSize);
                size_t bytesRead = readn(fdToServe, buffer, fileSize);
                if(bytesRead != fileSize){
                    //Client sent an ill-formed packet, disconnecting it
                    serverLog("[Worker #%d]: Client %d sent a different amount of bytes than advertised (%d vs %ld), disconnecting it\n", workerID, fdToServe, status.data.messageLength, bytesRead);
                    workerDisconnectClient(workerID, fdToServe);
                }else{
                    //Transfer completed successfully, send ack to client, set client status to connected, and send client served message to master
                    serverLog("[Worker #%d]: Received %s from client %d, %ld bytes transferred\n", workerID, append ? "data" : "file", fdToServe, bytesRead);


                    //Save file
                    CachedFile* file = getFileL(status.data.filename);
                    if(file != NULL){
                    	size_t storedSize = 0;
                        pthread_mutex_lock_error(file->lock, "Error while locking file");
                        if(file->lockedBy != fdToServe){
                            //File is not locked by this process
                            error = EPERM;
                        }else{
                            //Everything is ok, file can be written
                            if(append){
                                char* fileBuffer;
                                readCachedFile(file, &fileBuffer, (size_t *) &fileSize);
                                free(file->contents);
                                fileBuffer = realloc(fileBuffer, fileSize + bytesRead);
                                memcpy(fileBuffer + fileSize, buffer, bytesRead);
                                storedSize = storeFile(fileCache, file, fileBuffer, fileSize + bytesRead);
                            }else{
                                free(file->contents);
                                storedSize = storeFile(fileCache, file, buffer, fileSize);
                            }
                        }
                        pthread_mutex_unlock_error(file->lock, "Error while unlocking file");
                        if(storedSize == (append ? fileSize + bytesRead : fileSize)){
	                        serverLog("[Worker #%d]: File not compressed, size: %lu bytes\n", workerID, storedSize);
                        }else{
                        	serverLog("[Worker #%d]: File has been compressed, old size: %lu bytes, new size: %lu bytes\n", workerID, (append ? fileSize + bytesRead : fileSize), storedSize);
                        }
                    }else{
                        //File does not exist
                        error = ENOENT;
                    }

                    //Update client status
                    updateClientStatusL(Connected, 0, NULL, fdToServe);

                    //Send messages to client and to master
                    if(error == 0){
                        serverLog("[Worker #%d]: Sending ack to client %d\n", workerID, fdToServe);
                        fcpSend(FCP_ACK, 0, NULL, fdToServe);
                    }else{
                        //Invalid request, warn client
                        serverLog("[Worker #%d]: Client %d tried to %s to a file %s\n", workerID, fdToServe, append ? "append" : "write", errno == ENOENT ? "that didn't exist" : "that wasn't locked by it");
                        fcpSend(FCP_ERROR, error, NULL, fdToServe);
                    }

                    w2mSend(W2M_CLIENT_SERVED, fdToServe);
                }

                if(error != 0 || append){
                    //The buffer shouldn't be deallocated if the operation was successful: to avoid copying potentially
                    // high amounts of data, the buffer is directly assigned to the file, instead of memcpying it
                    free(buffer);
                }
                break;
            }
            case ReceivingFile:{
                char fcpBuffer[FCP_MESSAGE_LENGTH];
                ssize_t fcpBytesRead = readn(fdToServe, fcpBuffer, FCP_MESSAGE_LENGTH);

                if(fcpBytesRead == 0){
                    //Client disconnected
                    if(workerDisconnectClient(workerID, fdToServe)){
                        perror("Error while disconnecting client");
                    }
                }else{
                    FCPMessage* fcpMessage = fcpMessageFromBuffer(fcpBuffer);
                    switch(fcpMessage->op) {
                        case FCP_ACK:{
                            //Send file
                            char* fileBuffer = NULL;
                            size_t fileSize = 0;

                            CachedFile *file = getFileL(status.data.filename);

                            pthread_mutex_lock_error(file->lock, "Error while locking file");
                            readCachedFile(file, &fileBuffer, &fileSize);
                            pthread_mutex_unlock_error(file->lock, "Error while unlocking file");

                            ssize_t bytesSent = writen(fdToServe, fileBuffer, fileSize);
                            free(fileBuffer);

                            serverLog("[Worker #%d]: Sent file to client %d, %ld bytes transferred\n", workerID, fdToServe, bytesSent);

                            updateClientStatusL(Connected, 0, NULL, fdToServe);

                            w2mSend(W2M_CLIENT_SERVED, fdToServe);
                            break;
                        }
                        default:{
                            //Client has sent an invalid response, disconnect it
                            serverLog("[Worker #%d]: Client %d has sent an invalid response\n", workerID, fdToServe);
                            workerDisconnectClient(workerID, fdToServe);
                            break;
                        }
                    }
                    free(fcpMessage);
                }
                break;
            }
            default:{
                //Invalid status
                serverLog("[Worker #%d]: Client %d has sent a message while in an invalid status, disconnecting it\n", workerID, fdToServe);
                workerDisconnectClient(workerID, fdToServe);
                break;
            }
        }
    }

    serverLog("[Worker #%d]: Terminating\n", workerID);
    return (void*)0;
}

static int workerDisconnectClient(int workerN, int fdToServe){
	if(close(fdToServe)){
		return -1;
	}else{
#ifdef DEBUG
		serverLog("[Worker #%d]: Connection with client %d has been closed\n", workerN, fdToServe);
#endif
		//Warn the master thread
		w2mSend(W2M_CLIENT_DISCONNECTED, fdToServe);
		return 0;
	}
}

//Logging thread
static void* loggingThread(void* arg){
	char* logBuffer = malloc(LOG_BUFFER_SIZE);
	char* timeBuffer = NULL;
	if(logTimeFormat != NoTime){
		timeBuffer = malloc(TIME_STRING_SIZE);
	}
	printf("[Logging]: Logging thread started\n");
	int logFileDescriptor = open(logFilePath, O_CREAT | logMode | O_WRONLY, 0644);
	if(logFileDescriptor < 0){
		perror("Error while opening log file");
	}else{
		printf("[Logging]: Opened file %s for logging, with mode %s\n", logFilePath, logMode == O_TRUNC ? "Trunc" : "Append");
        printf("[Logging]: Time format: %s\n", logTimeFormat == NoTime ? "none" : logTimeFormat == Timestamp ? "timestamp" : "formatted");
		while(true){
			memset(logBuffer, 0, LOG_BUFFER_SIZE);
			readn(logPipeDescriptors[0], logBuffer, LOG_BUFFER_SIZE);
			if(logBuffer[0] == LOG_TERMINATE){
				break;
			}
			size_t msgLen =  strlen(logBuffer);

            if(logTimeFormat != NoTime){
                switch(logTimeFormat){
                    default:
                    case Formatted:{
                        time_t now = time(NULL);
                        struct tm *t = localtime(&now);
                        strftime(timeBuffer + 1, TIME_STRING_SIZE - 1, "%y/%m/%d %H:%M:%S", t);
                        break;
                    }
                    case Timestamp:{
                        time_t now = time(NULL);
                        snprintf(timeBuffer + 1, TIME_STRING_SIZE - 1, "%lu", now);
                        break;
                    }
                    case TimestampMicro:{
                        snprintf(timeBuffer + 1, TIME_STRING_SIZE - 1, "%lu", getTimeStamp());
                        break;
                    }
                }
                timeBuffer[0] = '[';
                size_t timelen = strlen(timeBuffer);
                timeBuffer[timelen] = ']';
                timeBuffer[timelen + 1] = 0;

                writen(logFileDescriptor, timeBuffer, timelen + 1);
                writen(1, timeBuffer, timelen + 1);
            }
			
			writen(logFileDescriptor, logBuffer, msgLen);
			writen(1, logBuffer, msgLen + 1);
		}
		if(close(logFileDescriptor)){
			perror("Error while closing logging file");
		}
	}
	if(timeBuffer != NULL){
		free(timeBuffer);
	}
	free(logBuffer);
	printf("[Logging]: Logging thread stopped\n");
	return 0;
}


//Master thread
#ifdef IDE
int serverMain(int argc, char** argv){
#else
int main(int argc, char** argv){
#endif

    CacheAlgorithm cacheAlgorithm = FIFO;
    CompressionAlgorithm compressionAlgorithm = Miniz;
	char* configFilePath = "/mnt/e/Progetti/SOL-Project/config.txt";
	unsigned short nWorkers = 10;
	unsigned int maxFiles = 100;
	unsigned long storageSize = 1024 * 1024 * 1024;
	char* socketPath = NULL;
	
	
	//Command line options parsing
	char opt;
	opterr = 0;
	bool finished = false;
	
	while(!finished){
		opt = getopt(argc, argv, "hf:c:");
		switch(opt){
			case 'h':{
				printf("Usage: %s [OPTIONS...]\n\n"
						"  -h\t\t\tPrints this help message.\n\n"
						"  -c filename\t\tSpecifies the config file path.\n", argv[0]);
				return 0;
			}
			case 'c':{
				configFilePath = optarg;
				break;
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
	bool error = false;
	ArgsList* configArgs = readConfigFile(configFilePath);
	if(configArgs == NULL){
		fprintf(stderr, "Error while reading config file\n");
		error = true;
	}else{
		char* storageString = getStringValue(configArgs, "storageSize");
		do{
			if(storageString == NULL){
			    fprintf(stderr, "No string passed as \"storageSize\"\n");
				error = true;
				break;
			}
			nWorkers = getLongValue(configArgs, "nWorkers");
			if(nWorkers < 1){
			    fprintf(stderr, "\"nWorkers\" can't be less than 1\n");
			    error = true;
			    break;
			}
			maxFiles = getLongValue(configArgs, "maxFiles");
			if(maxFiles < 1){
			    fprintf(stderr, "\"maxFiles\" can't be less than 1\n");
			    error = true;
			    break;
			}
			socketPath = getStringValue(configArgs, "socketPath");
			if(socketPath == NULL){
			    fprintf(stderr, "No string passed as \"socketPath\"\n");
				error = true;
				break;
			}
			logFilePath = getStringValue(configArgs, "logFile");
			if(logFilePath == NULL){
			    fprintf(stderr, "No string passed as \"logFile\"\n");
				error = true;
				free(socketPath);
				break;
			}

			char* logModeParameter = getStringValue(configArgs, "logMode");
			if(logModeParameter != NULL){
			    if(strcmp(logModeParameter, "trunc") == 0){
                    logMode = O_TRUNC;
                }
                free(logModeParameter);
			}

            char* fileCompressionParameter = getStringValue(configArgs, "compression");
            if(fileCompressionParameter != NULL){
                if(strcmp(fileCompressionParameter, "none") == 0){
                    compressionAlgorithm = Uncompressed;
                }
                free(fileCompressionParameter);
            }

            char* cacheAlgorithmParameter = getStringValue(configArgs, "cacheAlgorithm");
            if(cacheAlgorithmParameter != NULL){
                if(strcmp(cacheAlgorithmParameter, "LRU") == 0){
                    cacheAlgorithm = LRU;
                }
                free(cacheAlgorithmParameter);
            }
			
			char* logTimeFormattedParameter = getStringValue(configArgs, "logTimeFormat");
			if(logTimeFormattedParameter != NULL){
				if(strcmp(logTimeFormattedParameter, "none") == 0){
					logTimeFormat = NoTime;
				}else if(strcmp(logTimeFormattedParameter, "formatted") == 0){
					logTimeFormat = Formatted;
				}else if(strcmp(logTimeFormattedParameter, "timestampMicro") == 0) {
                    logTimeFormat = TimestampMicro;
                }
				free(logTimeFormattedParameter);
			}
			
			char* endptr = NULL;
			storageSize = strtoul(storageString, &endptr, 10);
			switch(*endptr){
				case 'k':
				case 'K':{
					storageSize *= 1024;
					break;
				}
			case 'm':
				case 'M':{
					storageSize *= 1024 * 1024;
					break;
				}
				case 'g':
				case 'G':{
					storageSize *= 1024 * 1024 * 1024;
					break;
				}
				case 'b':
				case 'B':
				default:{
					break;
				}
			}

			if(storageSize < 1){
			    fprintf(stderr, "\"storageSize\" can't be less than 1\n");
			    error = true;
			    break;
			}
		}while(false);
		free(storageString);
		freeArgsListNode(configArgs);
	}
	
	if(error){
		return -1;
	}
	
	
	fileCache = initFileCache(maxFiles, storageSize, compressionAlgorithm, cacheAlgorithm);
	
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
		freeFileCache(&fileCache);
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
	
	
	//Initialize logging pipe
	if(pipe(logPipeDescriptors)){
		perror("Error while creating logging pipe");
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
	
	
	//Spawn logging thread
	pthread_t loggingThreadID;
	if(pthread_create(&loggingThreadID, NULL, loggingThread, NULL)){
		perror("Error while creating logging thread");
		return -1;
	}
	
	
	//Spawn worker threads
	requestsServed = calloc(sizeof(unsigned int), nWorkers);
	pthread_t workers[nWorkers];
	for(size_t i = 0; i < nWorkers; i++){
	    requestsServed[i] = 0;
		if(pthread_create(&(workers[i]), NULL, workerThread, (void*)i)){
			perror("Error while creating worker thread");
			return -1;
		}
	}
	
	
	//Main loop
    serverLog("[Master]: Server successfully started with the following parameters:\n");
    serverLog("[Master]: Number of workers: %d\n", nWorkers);
    serverLog("[Master]: Capacity: %d files, %d bytes\n", maxFiles, storageSize);
    serverLog("[Master]: Listening socket path: %s\n", socketPath);
    serverLog("[Master]: Compression algorithm: %s\n", compressionAlgorithm == Miniz ? "zlib" : "none");
    serverLog("[Master]: Caching algorithm: %s\n", cacheAlgorithm == FIFO ? "FIFO" : "LRU");
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
	bool hangup = false;
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
						if(onW2MMessageReceived(serverSocketDescriptor, &selectFdSet, &maxFd, &running, &hangup)){
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
	freeClientList(&clientList);
	
	//It's safe to join on the signal handler, as the only way to terminate the server is for a signal to happen
	//and in that case the signal handler terminates
	pthread_join_error(signalHandlerThreadID, "Error while joining on signal handler thread");
	//Join on the worker threads
	for(size_t i = 0; i < nWorkers; i++){
		pthread_join_error(workers[i], "Error while joining worker thread");
	}


	//Print stats
    serverLog("[Master]: Max storage size reached: %lu bytes\n", fileCache->maxReached.size);
    serverLog("[Master]: Max number of files stored: %u\n", fileCache->maxReached.fileNumber);
    serverLog("[Master]: Max number of clients simultaneously connected: %u\n", clientsConnectedMax);
    serverLog("[Master]: Number of files evicted: %u\n", fileCache->filesEvicted);

    for(size_t i = 0; i < nWorkers; i++){
        serverLog("[Master]: Worker #%u has served %u requests\n", i, requestsServed[i]);
    }
    free(requestsServed);

    //Print list of files in the server
    serverLog("[Master]: Files contained in the server:\n");
    FileList* current = fileCache->files;
    while(current != NULL){
        serverLog("[Master]: \"%s\", %d bytes\n", current->file->filename, getUncompressedSize(current->file));
        current = current->next;
    }

	//Send termination message and join on the log server
	serverLog("%c", LOG_TERMINATE);
	pthread_join_error(loggingThreadID, "Error while joining on logging thread");
	
	if(close(w2mPipeDescriptors[0])){
		perror("Error while closing w2m pipe read endpoint");
	}
	if(close(w2mPipeDescriptors[1])){
		perror("Error while closing w2m pipe write endpoint");
	}
	if(close(logPipeDescriptors[0])){
		perror("Error while closing log pipe read endpoint");
	}
	if(close(logPipeDescriptors[1])){
		perror("Error while closing log pipe write endpoint");
	}
	
	freeFileCache(&fileCache);
	cleanup();
	return 0;
}

static int onConnectedClientMessage(int currentFd, fd_set* selectFdSet, int* maxFd){
    removeFromFdSetUpdatingMax(currentFd, selectFdSet, maxFd);

    pthread_mutex_lock_error(&incomingConnectionsLock, "Error while locking on incoming connections queue");
    queuePush(&incomingConnectionsQueue, (void*)(long)currentFd);
    pthread_cond_signal_error(&incomingConnectionsCond, "Error while signaling incoming connection");
    pthread_mutex_unlock_error(&incomingConnectionsLock, "Error while locking on incoming connections queue");
    return 0;
}

static int onNewConnectionReceived(int serverSocketDescriptor, fd_set* selectFdSet, int* maxFd){    clientsConnected++;
    if(clientsConnected > clientsConnectedMax){
        clientsConnectedMax = clientsConnected;
    }

	int newClientDescriptor = accept(serverSocketDescriptor, NULL, NULL);
	if(newClientDescriptor < 0){
		perror("Error while accepting a new connection");
		return -1;
	}
	serverLog("[Master]: New client connected, client descriptor: %d\n", newClientDescriptor);
	addToFdSetUpdatingMax(newClientDescriptor, selectFdSet, maxFd);

	pthread_rwlock_wrlock_error(&clientListLock, "Error while locking client list");
	clientListAdd(&clientList, newClientDescriptor);
	pthread_rwlock_unlock_error(&clientListLock, "Error while unlocking client list");
#ifdef DEBUG
	serverLog("[Master]: Client %d added to list of connected clients\n", newClientDescriptor);
#endif
	return 0;
}

static int onW2MMessageReceived(int serverSocketDescriptor, fd_set* selectFdSet, int* maxFd, bool* running, bool* hangup){
	char buffer[W2M_MESSAGE_LENGTH];
	ssize_t bytesRead = readn(w2mPipeDescriptors[0], buffer, W2M_MESSAGE_LENGTH);
	if(bytesRead < W2M_MESSAGE_LENGTH){
		//TODO: Handle error
		perror("Invalid message length on worker-to-master pipe");
		return -1;
	}
	switch(buffer[0]){
		case W2M_CLIENT_SERVED:{
			//A worker has served the client's request, add back client to the set of fds to listen to
			int clientFd = getIntFromW2MMessage(buffer);
#ifdef DEBUG
			serverLog("[Master]: Client %d has been served, adding it back to select set\n", clientFd);
#endif
			addToFdSetUpdatingMax(clientFd, selectFdSet, maxFd);
			break;
		}
		case W2M_SIGNAL_TERM:{
			//Stop listening to incoming connections, close all connections, terminate
			FD_ZERO(selectFdSet);
			terminateServer(running);
			break;
		}
		case W2M_SIGNAL_HANG:{
			//Stop listening to incoming connections, serve all requests, terminate
			removeFromFdSetUpdatingMax(serverSocketDescriptor, selectFdSet, maxFd);
			*hangup = true;
			if(clientsConnected == 0){
				terminateServer(running);
			}
			break;
		}
		case W2M_CLIENT_DISCONNECTED:{
			serverDisconnectClientL(getIntFromW2MMessage(buffer), true);
			if(*hangup && clientsConnected == 0){
				terminateServer(running);
			}
			break;
		}
	}
	return 0;
}
