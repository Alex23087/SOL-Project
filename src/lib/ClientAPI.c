#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "../include/ClientAPI.h"
#include "../include/defines.h"
#include "../include/FileCachingProtocol.h"
#include "../include/ion.h"
#include "../include/ParseUtils.h"
#include "../include/PathUtils.h"
#include "../include/TimespecUtils.h"



//Open connections key value list, useful if the API is extended to handle more connections
static ArgsList* openConnections = NULL;
static int activeConnectionFD = -1;



static int receiveAndSaveFileFromServer(size_t filesize, const char* filename, const char* dirname){
    printf("Receiving file from server (filename: \"%s\", bytes: %zu)\n", filename, filesize);
    size_t size = filesize;
    char* fileBuffer = malloc(size);
    ssize_t fileSize = readn(activeConnectionFD, fileBuffer, size);
    printf("Received file from server, (%ld bytes)\n", fileSize);

    if(size != fileSize){
        errno = EPROTO;
        return -1;
    }

    if(dirname != NULL){
        char* newFileName = replaceBasename(dirname, (char*)filename);
        printf("Filename: %s\n", newFileName);
        int fileDescriptor = open(newFileName, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if(fileDescriptor == -1){
            free(newFileName);
            return -1;
        }
        writen(fileDescriptor, fileBuffer, fileSize);
        close(fileDescriptor);
        printf("File saved to: %s\n", newFileName);
        free(newFileName);
    }else{
        printf("Save directory not specified, not saving file\n");
    }
    free(fileBuffer);
    return 0;
}

static int writeOrAppendFile(const char* pathname, void* buf, size_t size, const char* dirname, bool append){
    if(activeConnectionFD == -1){
        //Function called without an active connection
        errno = ENOTCONN;
        return -1;
    }

    bool success = true;
    char* absolutePathname = realpath(pathname, NULL);
    if(absolutePathname == NULL){
        success = false;
    }else{
        if(strlen(absolutePathname) > FCP_MESSAGE_LENGTH - 5){
            errno = ENAMETOOLONG;
            return -1;
        }

        int fileDescriptor = -1;
        if(!append){
            fileDescriptor = open(absolutePathname, O_RDONLY);
            if(fileDescriptor == -1){
                //Open failed, errno has already been set by it
                return -1;
            }
            struct stat fileStat;
            if(fstat(fileDescriptor, &fileStat)){
                //Fstat failed, errno has already been set by it
                return -1;
            }
            size = fileStat.st_size;
        }

        printf("Sending %s request to server\n", append ? "append" : "write");
        fcpSend(append ? FCP_APPEND : FCP_WRITE, (int)size, (char*)absolutePathname, activeConnectionFD);
        printf("%s request sent\n", append ? "Append" : "Write");

        bool receivingCacheMissFiles = false;
        char fcpBuffer[FCP_MESSAGE_LENGTH];
        ssize_t bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
        FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

        while(bytesRead != 0){
            switch(message->op){
                case FCP_ACK:{
                    receivingCacheMissFiles = false;
                    printf("Server has sent ack back, starting transfer\n");
                    if(append){
                        writen(activeConnectionFD, buf, size);
                    }else{
                        char* fileBuffer = malloc(size);
                        readn(fileDescriptor, fileBuffer, size);
                        writen(activeConnectionFD, fileBuffer, size);
                        free(fileBuffer);
                    }
                    printf("File transfer complete, waiting for ack\n");
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
                case FCP_WRITE:{
                    receivingCacheMissFiles = true;
                    //Server will send a file
                    if(receiveAndSaveFileFromServer(message->control, message->filename, dirname)){
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
                    errno = EPROTO;
                    break;
                }
            }
            if(!success || !receivingCacheMissFiles){
                break;
            }
            bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
            free(message);
            message = fcpMessageFromBuffer(fcpBuffer);
        }

        free(message);
        free(absolutePathname);
    }

    return success ? 0 : -1;
}



int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname){
    return writeOrAppendFile(pathname, buf, size, dirname, true);
}

int closeConnection(const char* sockname){
    ArgsList* connection = getNodeForKey(openConnections, sockname);
    if(connection == NULL){
        //Trying to close a connection that is not open
        errno = ENOTCONN;
        return -1;
    }else{
        int fd = getLongValue(connection, sockname);
        if(close(fd)){
            //Error while closing connection, errno has been set by close()
            return -1;
        }else{
            freeArgsListNode(connection);
            return 0;
        }
    }
}

int closeFile(const char* pathname){
    if(activeConnectionFD == -1){
        //Function called without an active connection
        errno = ENOTCONN;
        return -1;
    }

    bool success = true;
    char* absolutePathname = realpath(pathname, NULL);
    if(absolutePathname == NULL){
        success = false;
    }else{
        if(strlen(absolutePathname) > FCP_MESSAGE_LENGTH - 5){
            errno = ENAMETOOLONG;
            return -1;
        }

        printf("Sending close request to server\n");
        fcpSend(FCP_CLOSE, 0, (char*)absolutePathname, activeConnectionFD);
        printf("Close request sent\n");

        char fcpBuffer[FCP_MESSAGE_LENGTH];
        readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
        FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

        switch(message->op){
            case FCP_ACK:{
                printf("File closed correctly\n");
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
        free(absolutePathname);
    }
    return success ? 0 : -1;
}

int lockFile(const char* pathname){
    if(activeConnectionFD == -1){
        //Function called without an active connection
        errno = ENOTCONN;
        return -1;
    }

    bool success = true;
    char* absolutePathname = realpath(pathname, NULL);
    if(absolutePathname == NULL){
        success = false;
    }else{
        if(strlen(absolutePathname) > FCP_MESSAGE_LENGTH - 5){
            errno = ENAMETOOLONG;
            return -1;
        }

        printf("Sending lock request to server\n");
        fcpSend(FCP_LOCK, 0, (char*)absolutePathname, activeConnectionFD);
        printf("Lock request sent\n");

        char fcpBuffer[FCP_MESSAGE_LENGTH];
        ssize_t bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
        FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

        if(bytesRead != FCP_MESSAGE_LENGTH){
            errno = EPROTO;
            success = false;
        }else{
            switch(message->op){
                case FCP_ACK:{
                    printf("File locked successfully\n");
                    break;
                }
                case FCP_ERROR:{
                    errno =	message->control;
                    success = false;
                    break;
                }
                default:{
                    success = false;
                    break;
                }
            }
        }

        free(message);
        free(absolutePathname);
    }
    return success ? 0 : -1;
}

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
		newConnection->data = malloc(sizeof(long));
		*(long*)(newConnection->data) = ((long)clientSocketDescriptor);
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

int openFile(const char* pathname, int flags){
	if(activeConnectionFD == -1){
		//Function called without an active connection
		errno = ENOTCONN;
		return -1;
	}

	bool success = true;
	char* absolutePathname = realpath(pathname, NULL);
	if(absolutePathname == NULL){
		success = false;
	}else{
		if(strlen(absolutePathname) > FCP_MESSAGE_LENGTH - 5){
			errno = ENAMETOOLONG;
			return -1;
		}

		printf("Sending open request to server\n");
		fcpSend(FCP_OPEN, flags, (char*)absolutePathname, activeConnectionFD);
		printf("Open request sent\n");

		char fcpBuffer[FCP_MESSAGE_LENGTH];
		ssize_t bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
		FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

		if(bytesRead != FCP_MESSAGE_LENGTH){
			errno = EPROTO;
			success = false;
		}else{
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
					errno = EPROTO;
					success = false;
					break;
				}
			}
		}

		free(message);
		free(absolutePathname);
	}
	return success ? 0 : -1;
}

int readFile(const char* pathname, void** buf, size_t* size){
	if(activeConnectionFD == -1){
		//Function called without an active connection
		errno = ENOTCONN;
		return -1;
	}

	bool success = true;
	char* absolutePathname = realpath(pathname, NULL);
	if(absolutePathname == NULL){
		success = false;
	}else{
		if(strlen(absolutePathname) > FCP_MESSAGE_LENGTH - 5){
			errno = ENAMETOOLONG;
			return -1;
		}

		printf("Sending read request to server\n");
		fcpSend(FCP_READ, 0, (char*)absolutePathname, activeConnectionFD);
		printf("Read request sent\n");

		char fcpBuffer[FCP_MESSAGE_LENGTH];
		ssize_t bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
		FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

		if(bytesRead != FCP_MESSAGE_LENGTH){
			success = false;
			errno = EPROTO;
		}else{
			switch(message->op){
				case FCP_WRITE:{
					//Server will send the file
					*size = message->control;
					fcpSend(FCP_ACK, 0, NULL, activeConnectionFD);
					*buf = malloc(*size);
					readn(activeConnectionFD, *buf, *size);
					break;
				}
				case FCP_ERROR:{
					//Error while reading the file
					success = false;
					errno = message->control;
					break;
				}
				default:{
					//Invalid message received from server
					success = false;
					errno = EPROTO;
					break;
				}
			}
		}

		free(absolutePathname);
		free(message);
	}
	return success ? 0 : -1;
}

int readNFiles(int N, const char* dirname){
    if(activeConnectionFD == -1){
        //Function called without an active connection
        errno = ENOTCONN;
        return -1;
    }

    printf("Sending readN request to server\n");
    fcpSend(FCP_READ_N, N, NULL, activeConnectionFD);
    printf("ReadN request sent\n");

    bool success = true;
    char fcpBuffer[FCP_MESSAGE_LENGTH];
    ssize_t bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
    FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

    bool finished = false;
    while(bytesRead != 0 && !finished){
        switch(message->op){
            case FCP_WRITE:{
                //Server will send a file
                if(receiveAndSaveFileFromServer(message->control, message->filename, dirname)){
                    success = false;
                }
                break;
            }
            case FCP_ACK:{
                //All files sent
                printf("ReadN operation finished\n");
                finished = true;
                break;
            }
            case FCP_ERROR:{
                //Error while reading the file
                success = false;
                errno = message->control;
                break;
            }
            default:{
                //Invalid message received from server
                success = false;
                errno = EPROTO;
                break;
            }
        }
        if(!success || finished){
            break;
        }
        bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
        free(message);
        message = fcpMessageFromBuffer(fcpBuffer);
    }

    free(message);
    return success ? 0 : -1;
}

int removeFile(const char* pathname){
    if(activeConnectionFD == -1){
        //Function called without an active connection
        errno = ENOTCONN;
        return -1;
    }

    bool success = true;
    char* absolutePathname = realpath(pathname, NULL);
    if(absolutePathname == NULL){
        success = false;
    }else{
        if(strlen(absolutePathname) > FCP_MESSAGE_LENGTH - 5){
            errno = ENAMETOOLONG;
            return -1;
        }

        printf("Sending remove file request to server\n");
        fcpSend(FCP_REMOVE, 0, (char*)absolutePathname, activeConnectionFD);
        printf("Remove request sent\n");

        char fcpBuffer[FCP_MESSAGE_LENGTH];
        ssize_t bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
        FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

        if(bytesRead != FCP_MESSAGE_LENGTH){
            errno = EPROTO;
            success = false;
        }else{
            switch(message->op){
                case FCP_ACK:{
                    printf("File removed correctly\n");
                    break;
                }
                case FCP_ERROR:{
                    errno =	message->control;
                    success = false;
                    break;
                }
                default:{
                    errno = EPROTO;
                    success = false;
                    break;
                }
            }
        }

        free(message);
        free(absolutePathname);
    }
    return success ? 0 : -1;
}

int writeFile(const char* pathname, const char* dirname){
	return writeOrAppendFile(pathname, NULL, 0, dirname, false);
}

int unlockFile(const char* pathname){
	if(activeConnectionFD == -1){
		//Function called without an active connection
		errno = ENOTCONN;
		return -1;
	}

	bool success = true;
	char* absolutePathname = realpath(pathname, NULL);
	if(absolutePathname == NULL){
		success = false;
	}else{
		if(strlen(absolutePathname) > FCP_MESSAGE_LENGTH - 5){
			errno = ENAMETOOLONG;
			return -1;
		}

		printf("Sending unlock request to server\n");
		fcpSend(FCP_UNLOCK, 0, (char*)absolutePathname, activeConnectionFD);
		printf("Unlock request sent\n");

		char fcpBuffer[FCP_MESSAGE_LENGTH];
		ssize_t bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
		FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

		if(bytesRead != FCP_MESSAGE_LENGTH){
			errno = EPROTO;
			success = false;
		}else{
			switch(message->op){
				case FCP_ACK:{
					printf("File unlocked successfully\n");
					break;
				}
				case FCP_ERROR:{
					errno =	message->control;
					success = false;
					break;
				}
				default:{
					errno = EPROTO;
					success = false;
					break;
				}
			}
		}

		free(message);
		free(absolutePathname);
	}
	return success ? 0 : -1;
}
