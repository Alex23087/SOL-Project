#ifndef SOL_PROJECT_FILECACHINGPROTOCOL_H
#define SOL_PROJECT_FILECACHINGPROTOCOL_H

#define FCP_MESSAGE_LENGTH 256
#define FCP_MAX_FILENAME_SIZE FCP_MESSAGE_LENGTH - 5

#include <stdint.h>

#include "defines.h"

#define O_CREATE 1
#define O_LOCK 2
#define FCP_OPEN_FLAG_ISSET(flags, flagToCheck) \
	(((flags) | (flagToCheck)) == (flags))



typedef enum ClientOperation{
	Connected,
	SendingFile,
	AppendingToFile,
	ReceivingFile,
	WaitingForLock
} ClientOperation;

typedef struct ConnectionStatusAdditionalData{
	char* filename;
	int messageLength;
	int filesToRead;
}ConnectionStatusAdditionalData;

typedef struct ConnectionStatus{
	ClientOperation op;
	ConnectionStatusAdditionalData data;
}ConnectionStatus;

typedef struct OpenFilesList{
	char* filename;
	struct OpenFilesList* next;
} OpenFilesList;

typedef struct ClientList{
	int descriptor;
	ConnectionStatus status;
	OpenFilesList* openFiles;
	struct ClientList* next;
} ClientList;

typedef enum FCPOpcode{
	FCP_OPEN,
	FCP_READ,
	FCP_READ_N,
	FCP_WRITE,
	FCP_APPEND,
	FCP_LOCK,
	FCP_UNLOCK,
	FCP_CLOSE,
	FCP_REMOVE,
	FCP_ACK,
	FCP_ERROR
} FCPOpcode;

#pragma pack(1)
//The pragma directive shouldn't be needed, the struct is ordered in a way that should
// result in the correct alignment. However, better include it to be sure
typedef struct FCPMessage{
	int32_t control;
	char op;
	char filename[FCP_MAX_FILENAME_SIZE];
} FCPMessage;
#pragma pack() //Resetting previous packing settings



void clientListAdd(ClientList** list, int descriptor);

ConnectionStatus clientListGetStatus(ClientList* list, int descriptor);

void clientListRemove(ClientList** list, int descriptor);

void clientListUpdateStatus(ClientList* list, int descriptor, ConnectionStatus status);

void closeAllFiles(ClientList* list, int descriptor);

void closeFileForEveryone(ClientList* list, const char* filename);

char* fcpBufferFromMessage(FCPMessage message);

FCPMessage* fcpMakeMessage(FCPOpcode operation, int32_t size, char* filename);

FCPMessage* fcpMessageFromBuffer(char buffer[FCP_MESSAGE_LENGTH]);

void fcpSend(FCPOpcode operation, int32_t size, char* filename, int fd);

void freeClientList(ClientList** clientList);

bool isFileOpenedByClient(ClientList* list, const char* filename, int descriptor);

void setFileClosed(ClientList* list, int descriptor, const char* filename);

void setFileOpened(ClientList* list, int descriptor, const char* filename);

#endif //SOL_PROJECT_FILECACHINGPROTOCOL_H
