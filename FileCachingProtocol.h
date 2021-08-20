#ifndef SOL_PROJECT_FILECACHINGPROTOCOL_H
#define SOL_PROJECT_FILECACHINGPROTOCOL_H

#define FCP_MESSAGE_LENGTH 256

#include <stdint.h>
#include "defines.h"

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

#define O_CREATE 1
#define O_LOCK 2

#define FCP_OPEN_FLAG_ISSET(flags, flagToCheck) \
	(((flags) | (flagToCheck)) == (flags))

#pragma pack(1)
//The pragma directive shouldn't be needed, the struct is ordered in a way that should
// result in the correct alignment. However, better include it to be sure
typedef struct FCPMessage{
	int32_t control;
	char op;
	char filename[251];
} FCPMessage;
#pragma pack() //Resetting previous packing settings

FCPMessage* fcpMessageFromBuffer(char buffer[256]);

char* fcpBufferFromMessage(FCPMessage message);

FCPMessage* fcpMakeMessage(FCPOpcode operation, int32_t size, char* filename);

void fcpSend(FCPOpcode operation, int32_t size, char* filename, int fd);

void clientListAdd(ClientList** list, int descriptor);

void clientListRemove(ClientList** list, int descriptor);

void clientListUpdateStatus(ClientList* list, int descriptor, ConnectionStatus status);

ConnectionStatus clientListGetStatus(ClientList* list, int descriptor);
/*
OpenFilesList* getFileListForDescriptor(ClientList* list, int descriptor);

bool isFileOpen(OpenFilesList* list, const char* filename);

void addOpenFile(OpenFilesList** list, const char* filename);
*/

bool isFileOpenedByClient(ClientList* list, const char* filename, int descriptor);

void setFileOpened(ClientList* list, int descriptor, const char* filename);

void setFileClosed(ClientList* list, int descriptor, const char* filename);

void closeAllFiles(ClientList* list, int descriptor);

#endif //SOL_PROJECT_FILECACHINGPROTOCOL_H
