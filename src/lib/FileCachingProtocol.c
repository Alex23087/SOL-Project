#include <malloc.h>
#include <memory.h>
#include <pthread.h>
#include <stddef.h>

#include "../include/FileCachingProtocol.h"
#include "../include/ion.h"



//Adds a file to a list of open files
static void addOpenFile(OpenFilesList** list, const char* filename){
    OpenFilesList* newNode = malloc(sizeof(OpenFilesList));
    newNode->filename = malloc(strlen(filename) + 1);
    strcpy(newNode->filename, filename);
    newNode->next = *list;
    *list = newNode;
}

//Recursively frees a list of open files
static void closeAllFilesFromList(OpenFilesList** list){
    if((*list) != NULL){
        closeAllFilesFromList(&((*list)->next));
        free((*list)->filename);
        free((*list));
        *list = NULL;
    }
}

//Gets the list of open files relative to the client with descriptor passed as parameter,
//or NULL if there is no client with that descriptor.
static OpenFilesList** getFileListForDescriptor(ClientList* list, int descriptor){
    ClientList* current = list;
    while(current != NULL){
        if(current->descriptor == descriptor){
            return &(current->openFiles);
        }
        current = current->next;
    }
    return NULL;
}

static bool isFileOpen(OpenFilesList* list, const char* filename){
    OpenFilesList* current = list;
    while(current != NULL){
        if(strcmp(current->filename, filename) == 0){
            return true;
        }
        current = current->next;
    }
    return false;
}

//Removes the file with name passed as parameter from a list of open files
static void removeOpenFile(OpenFilesList** list, const char* filename){
    OpenFilesList* current = *list;
    if(current == NULL){
        return;
    }

    if(strcmp(current->filename, filename) == 0){
        *list = current->next;
        free(current->filename);
        free(current);
        return;
    }

    while(current->next != NULL){
        if(strcmp(current->next->filename, filename) == 0){
            OpenFilesList* tmp = current->next->next;
            free(current->next->filename);
            free(current->next);
            current->next = tmp;
            return;
        }
        current = current->next;
    }
}



//Inserts a new client to the tail of the ClientList passed as parameter
void clientListAdd(ClientList** list, int descriptor){
    ClientList* newNode = malloc(sizeof(ClientList));
    memset(newNode, 0, sizeof(ClientList));
    newNode->descriptor = descriptor;
    newNode->status.op = Connected;
    newNode->status.data.messageLength = 0;
    newNode->status.data.filename = NULL;
    newNode->status.data.filesToRead = 0;
    newNode->next = NULL;

    if(*list == NULL){
        *list = newNode;
        return;
    }

    ClientList* current = *list;
    while(current->next != NULL){
        current = current->next;
    }
    current->next = newNode;
}

ConnectionStatus clientListGetStatus(ClientList* list, int descriptor){
    ClientList* current = list;
    while(current != NULL){
        if(current->descriptor == descriptor){
            return current->status;
        }
        current = current->next;
    }
    //Trying to get status of a non connected client, fatal error
    fprintf(stderr, "Trying to get status of a non connected client, terminating thread\n");
    pthread_exit(NULL);
}

void clientListRemove(ClientList** list, int descriptor){
    ClientList* current = *list;
    closeAllFiles(*list, descriptor);
    if(current == NULL){
        return;
    }
    if(current->descriptor == descriptor){
        current = (*list)->next;
        if((*list)->status.data.filename != NULL){
            free((*list)->status.data.filename);
        }
        free(*list);
        *list = current;
        return;
    }
    while(current->next != NULL && current->next->descriptor != descriptor){
        current = current->next;
    }
    if(current->next == NULL){
        return;
    }

    ClientList* tmp = current->next->next;
    if(current->next->status.data.filename != NULL){
        free(current->next->status.data.filename);
    }
    free(current->next);
    current->next = tmp;
}

void clientListUpdateStatus(ClientList* list, int descriptor, ConnectionStatus status){
    ClientList* current = list;
    while(current != NULL){
        if(current->descriptor == descriptor){
            if(current->status.data.filename != NULL){
                free(current->status.data.filename);
            }
            current->status = status;
            return;
        }
        current = current->next;
    }
}

void closeAllFiles(ClientList* list, int descriptor){
    OpenFilesList** fileList = getFileListForDescriptor(list, descriptor);
    closeAllFilesFromList(fileList);
}

//Closes all files for all clients in the ClientList
void closeFileForEveryone(ClientList* list, const char* filename){
    ClientList* current = list;
    while(current != NULL){
        setFileClosed(current, current->descriptor, filename);
        current = current->next;
    }
}

//Serializes an FCPMessages into a char buffer
char* fcpBufferFromMessage(FCPMessage message){
    char* out = malloc(FCP_MESSAGE_LENGTH);
    out = memcpy(out, &message, FCP_MESSAGE_LENGTH);
    return out;
}

//Creates a FCPMessage object with the parameters specified
FCPMessage* fcpMakeMessage(FCPOpcode operation, int32_t size, char* filename){
    FCPMessage* message = malloc(FCP_MESSAGE_LENGTH);
    memset(message, 0, FCP_MESSAGE_LENGTH);
    message->op = operation;
    message->control = size;
    if(filename != NULL){
        strncpy(message->filename, filename, FCP_MESSAGE_LENGTH - 5);
    }
    return message;
}

//Deserializes a FCPMessage object from a char buffer
FCPMessage* fcpMessageFromBuffer(char buffer[FCP_MESSAGE_LENGTH]){
	FCPMessage* out = malloc(FCP_MESSAGE_LENGTH);
	memcpy(out, buffer, FCP_MESSAGE_LENGTH);
	return out;
}

//Creates a FCPMessage, converts it to a buffer and sends it to the file descriptor specified
void fcpSend(FCPOpcode operation, int32_t size, char* filename, int fd){
	FCPMessage* message = fcpMakeMessage(operation, size, filename);
	char* buffer = fcpBufferFromMessage(*message);
    writen(fd, buffer, FCP_MESSAGE_LENGTH);
	free(message);
	free(buffer);
}

//Recursively frees a ClientList
void freeClientList(ClientList** clientList){
	if(*clientList == NULL){
		return;
	}
	freeClientList(&((*clientList)->next));
	clientListRemove(clientList, (*clientList)->descriptor);
}

bool isFileOpenedByClient(ClientList* list, const char* filename, int descriptor){
    OpenFilesList** filesList = getFileListForDescriptor(list, descriptor);
	return filesList == NULL ? false : isFileOpen(*filesList, filename);
}

void setFileClosed(ClientList* list, int descriptor, const char* filename){
    OpenFilesList** fileList = getFileListForDescriptor(list, descriptor);
    removeOpenFile(fileList, filename);
}

void setFileOpened(ClientList* list, int descriptor, const char* filename){
	OpenFilesList** fileList = getFileListForDescriptor(list, descriptor);
	if(!isFileOpen(*fileList, filename)){
		addOpenFile(fileList, filename);
	}
}
