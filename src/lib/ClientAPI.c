#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "../include/ClientAPI.h"
#include "../include/FileCachingProtocol.h"
#include "../include/ion.h"
#include "../include/ParseUtils.h"
#include "../include/PathUtils.h"
#include "../include/TimespecUtils.h"

#ifdef _POSIX_C_SOURCE
//Function prototype copied here since despite the function is POSIX 2008 compliant (according to man 3 realpath), the compiler doesn't recognize it
extern char *realpath (const char *__restrict __name, char *__restrict __resolved);
#endif



//Open connections key value list, useful if the API is extended to handle more connections
static ArgsList* openConnections = NULL;
static int activeConnectionFD = -1;
bool verbose = false;


//Utility function called by functions that have to save a file if the server sends one back,
//such as openFile2, readNFiles and writeOrAppendFile
static int receiveAndSaveFileFromServer(size_t filesize, const char* filename, const char* dirname){
    printIfVerbose("Receiving file from server (filename: \"%s\", bytes: %zu)\n", filename, filesize);
    size_t size = filesize;
    char* fileBuffer = malloc(size);
    ssize_t fileSize = readn(activeConnectionFD, fileBuffer, size);
    printIfVerbose("Received file from server, (%ld bytes)\n", fileSize);

    if(size != fileSize){
        //The server has sent a message of size different from what it specified previously with the FCP_WRITE message.
        //This violates the protocol
        errno = EPROTO;
        return -1;
    }

    if(dirname != NULL){
        //Directory specified, the file has to be saved
        char* newFileName = replaceDirname(dirname, (char *) filename);
        printIfVerbose("Filename: %s\n", newFileName);
        int fileDescriptor = open(newFileName, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if(fileDescriptor == -1){
            //Error while opening the new file, errno doesn't need to be set, it's already set by open (provided free doesn't fail)
            free(newFileName);
            return -1;
        }
        writen(fileDescriptor, fileBuffer, fileSize);
        close(fileDescriptor);
        printIfVerbose("File saved to: %s\n", newFileName);
        free(newFileName);
    }else{
        printIfVerbose("Save directory not specified, not saving file\n");
    }
    free(fileBuffer);
    return 0;
}

//Function used by both writeFile and appendFile, as the logic is the same, with few differences
static int writeOrAppendFile(const char* pathname, void* buf, size_t size, const char* dirname, bool append){
    if(activeConnectionFD == -1){
        //Function called without an active connection
        errno = ENOTCONN;
        return -1;
    }

    bool success = true;
    char* absolutePathname = realpath(pathname, NULL);
    if(absolutePathname == NULL){
        //Couldn't determine canonical absolute path
        success = false;
    }else{
        if(strlen(absolutePathname) > FCP_MAX_FILENAME_SIZE){
            //Name longer than the protocol allows
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
                close(fileDescriptor);
                return -1;
            }
            size = fileStat.st_size;
        }

        printIfVerbose("Sending %s request to server\n", append ? "append" : "write");
        fcpSend(append ? FCP_APPEND : FCP_WRITE, (int)size, (char*)absolutePathname, activeConnectionFD);
        printIfVerbose("%s request sent\n", append ? "Append" : "Write");

        bool receivingCacheMissFiles = false;
        char fcpBuffer[FCP_MESSAGE_LENGTH];
        ssize_t bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
        FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

        if(bytesRead == 0){
            //First reply from server was empty, protocol error
            success = false;
            errno = EPROTO;
        }else{
            //Loop while the server sends replies, which can both be an FCP_ACK at which point the client starts sending the file
            //or FCP_WRITE, in which case the client has to receive the files sent by the server
            while(bytesRead != 0){
                switch(message->op){
                    case FCP_ACK:{
                        //Sending file to server
                        receivingCacheMissFiles = false;
                        printIfVerbose("Server has sent ack back, starting transfer\n");
                        if(append){
                            writen(activeConnectionFD, buf, size);
                        }else{
                            char *fileBuffer = malloc(size);
                            readn(fileDescriptor, fileBuffer, size);
                            writen(activeConnectionFD, fileBuffer, size);
                            free(fileBuffer);
                        }
                        printIfVerbose("File transfer complete, waiting for ack\n");
                        //Getting confirmation from the server
                        if(readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH) == FCP_MESSAGE_LENGTH){
                            free(message);
                            message = fcpMessageFromBuffer(fcpBuffer);

                            switch (message->op){
                                case FCP_ACK:{
                                    printIfVerbose("Received ack from server, operation completed successfully\n");
                                    break;
                                }
                                default:{
                                    //Server sent an invalid reply, operation failed
                                    errno = EPROTO; //EBADMSG EPROTO EMSGSIZE EILSEQ
                                    success = false;
                                    break;
                                }
                            }
                        }else{
                            //Server sent an invalid reply, operation failed
                            errno = EPROTO;
                            success = false;
                        }
                        break;
                    }
                    case FCP_WRITE:{
                        //Server will send a file, receive it and save it or discard it
                        receivingCacheMissFiles = true;
                        if (receiveAndSaveFileFromServer(message->control, message->filename, dirname)) {
                            success = false;
                        }
                        break;
                    }
                    case FCP_ERROR:{
                        //There has been an error
                        errno = message->control;
                        success = false;
                        break;
                    }
                    default:{
                        //Server sent an invalid reply, operation failed
                        success = false;
                        errno = EPROTO;
                        break;
                    }
                }
                if(!success || !receivingCacheMissFiles){
                    //Break on error or if the file was sent successfully
                    break;
                }
                //Read the next message from the server otherwise
                bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
                free(message);
                message = fcpMessageFromBuffer(fcpBuffer);
            }
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
        //Error while getting the canonical absolute path
        success = false;
    }else{
        if(strlen(absolutePathname) > FCP_MAX_FILENAME_SIZE){
            //Filename too long for the protocol
            errno = ENAMETOOLONG;
            return -1;
        }

        printIfVerbose("Sending close request to server\n");
        fcpSend(FCP_CLOSE, 0, (char*)absolutePathname, activeConnectionFD);
        printIfVerbose("Close request sent\n");

        char fcpBuffer[FCP_MESSAGE_LENGTH];
        readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
        FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

        switch(message->op){
            case FCP_ACK:{
                printIfVerbose("File closed correctly\n");
                break;
            }
            case FCP_ERROR:{
                //Something went wrong
                errno =	message->control;
                success = false;
                break;
            }
            default:{
                //Server has sent an invalid reply
                errno = EPROTO;
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
        //Error while getting canonical absolute path
        success = false;
    }else{
        if(strlen(absolutePathname) > FCP_MAX_FILENAME_SIZE){
            //The filename is too long for the protocol
            errno = ENAMETOOLONG;
            return -1;
        }

        printIfVerbose("Sending lock request to server\n");
        fcpSend(FCP_LOCK, 0, (char*)absolutePathname, activeConnectionFD);
        printIfVerbose("Lock request sent\n");

        char fcpBuffer[FCP_MESSAGE_LENGTH];
        ssize_t bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
        FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

        if(bytesRead != FCP_MESSAGE_LENGTH){
            //Server has sent an invalid reply
            errno = EPROTO;
            success = false;
        }else{
            switch(message->op){
                case FCP_ACK:{
                    printIfVerbose("File locked successfully\n");
                    break;
                }
                case FCP_ERROR:{
                    errno =	message->control;
                    success = false;
                    break;
                }
                default:{
                    //Server has sent an invalid reply
                    success = false;
                    errno = EPROTO;
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
        //Attempt to connect
		connectionSucceeded = !connect(clientSocketDescriptor, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
		if(!connectionSucceeded){
			if(compareTimes(deadlineTime, endTime) >= 0){
                //If the time remaining to the deadline is more than the step specified, wait for msec and try again
                struct timespec ts = doubleToTimespec((double)msec / (double)1000);
                nanosleep(&ts, NULL);
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
        //Couldn't connect to the server
		errno = ENXIO;
		return -1;
	}
}

int openFile(const char* pathname, int flags){
	return openFile2(pathname, NULL, flags);
}

int openFile2(const char* pathname, const char* dirname, int flags){
	if(activeConnectionFD == -1){
		//Function called without an active connection
		errno = ENOTCONN;
		return -1;
	}

	bool success = true;
	char* absolutePathname = realpath(pathname, NULL);
	if(absolutePathname == NULL){
        //Couldn't determine canonical absolute path
		success = false;
	}else{
		if(strlen(absolutePathname) > FCP_MAX_FILENAME_SIZE){
            //Filename too long for the protocol
			errno = ENAMETOOLONG;
			return -1;
		}

		printIfVerbose("Sending open request to server\n");
		fcpSend(FCP_OPEN, flags, (char*)absolutePathname, activeConnectionFD);
		printIfVerbose("Open request sent\n");

		bool receivingCacheMissFiles = false;
		char fcpBuffer[FCP_MESSAGE_LENGTH];
		ssize_t bytesRead = 0;
		FCPMessage* message = NULL;

		do{
            //Get message from the server
			bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
			message = fcpMessageFromBuffer(fcpBuffer);
			if(bytesRead != FCP_MESSAGE_LENGTH){
                //Invalid reply from the server
				errno = EPROTO;
				success = false;
			}else{
				switch(message->op){
					case FCP_ACK:{
						receivingCacheMissFiles = false;
						printIfVerbose("File opened correctly\n");
						break;
					}
					case FCP_ERROR:{
						errno =	message->control;
						success = false;
						break;
					}
					case FCP_WRITE:{
                        //Server will send a file
                        receivingCacheMissFiles = true;
						if(receiveAndSaveFileFromServer(message->control, message->filename, dirname)){
							success = false;
						}
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
		}while(receivingCacheMissFiles && success);

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
        //Couldn't determine canonical absolute path
		success = false;
	}else{
		if(strlen(absolutePathname) > FCP_MAX_FILENAME_SIZE){
            //Filename too long for the protocol
			errno = ENAMETOOLONG;
			return -1;
		}

		printIfVerbose("Sending read request to server\n");
		fcpSend(FCP_READ, 0, (char*)absolutePathname, activeConnectionFD);
		printIfVerbose("Read request sent\n");

        //Get message from the server
		char fcpBuffer[FCP_MESSAGE_LENGTH];
        ssize_t bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
		FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

        printf("%ld, %s\n", bytesRead, fcpBuffer);

		if(bytesRead != FCP_MESSAGE_LENGTH){
            //Invalid reply
			success = false;
			errno = EPROTO;
		}else{
			switch(message->op){
				case FCP_WRITE:{
					//Server will send the file, send ack and read the file in the output buffer
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

    printIfVerbose("Sending readN request to server\n");
    fcpSend(FCP_READ_N, N, NULL, activeConnectionFD);
    printIfVerbose("ReadN request sent\n");

    //Get reply from server
    bool success = true;
    char fcpBuffer[FCP_MESSAGE_LENGTH];
    ssize_t bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
    FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

    int filesRead = 0;
    bool finished = false;
    while(bytesRead != 0 && !finished){
        switch(message->op){
            case FCP_WRITE:{
                //Server will send a file
                if(receiveAndSaveFileFromServer(message->control, message->filename, dirname)){
                    success = false;
                }else{
                    filesRead++;
                }
                break;
            }
            case FCP_ACK:{
                //All files sent
                printIfVerbose("ReadN operation finished\n");
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
        //Get next message from server
        bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
        free(message);
        message = fcpMessageFromBuffer(fcpBuffer);
    }

    free(message);
    return success ? filesRead : -1;
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
        //Couldn't determine canonical absolute path
        success = false;
    }else{
        if(strlen(absolutePathname) > FCP_MAX_FILENAME_SIZE){
            //Filename too long for the protocol
            errno = ENAMETOOLONG;
            return -1;
        }

        printIfVerbose("Sending remove file request to server\n");
        fcpSend(FCP_REMOVE, 0, (char*)absolutePathname, activeConnectionFD);
        printIfVerbose("Remove request sent\n");

        //Read reply from the server
        char fcpBuffer[FCP_MESSAGE_LENGTH];
        ssize_t bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
        FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

        if(bytesRead != FCP_MESSAGE_LENGTH){
            //Server sent an invalid reply
            errno = EPROTO;
            success = false;
        }else{
            switch(message->op){
                case FCP_ACK:{
                    printIfVerbose("File removed correctly\n");
                    break;
                }
                case FCP_ERROR:{
                    errno =	message->control;
                    success = false;
                    break;
                }
                default:{
                    //Server sent an invalid reply
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
        //Couldn't determine canonical absolute path
		success = false;
	}else{
		if(strlen(absolutePathname) > FCP_MAX_FILENAME_SIZE){
            //Filename too long for the protocol
			errno = ENAMETOOLONG;
			return -1;
		}

		printIfVerbose("Sending unlock request to server\n");
		fcpSend(FCP_UNLOCK, 0, (char*)absolutePathname, activeConnectionFD);
		printIfVerbose("Unlock request sent\n");

        //Get reply from server
		char fcpBuffer[FCP_MESSAGE_LENGTH];
		ssize_t bytesRead = readn(activeConnectionFD, fcpBuffer, FCP_MESSAGE_LENGTH);
		FCPMessage* message = fcpMessageFromBuffer(fcpBuffer);

		if(bytesRead != FCP_MESSAGE_LENGTH){
            //Invalid reply from server
			errno = EPROTO;
			success = false;
		}else{
			switch(message->op){
				case FCP_ACK:{
					printIfVerbose("File unlocked successfully\n");
					break;
				}
				case FCP_ERROR:{
					errno =	message->control;
					success = false;
					break;
				}
				default:{
                    //Invalid reply from server
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
