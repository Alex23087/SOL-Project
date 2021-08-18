#ifndef SOL_PROJECT_FILECACHINGPROTOCOL_H
#define SOL_PROJECT_FILECACHINGPROTOCOL_H

#define FCP_MESSAGE_LENGTH 256

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

typedef struct ClientList{
	int descriptor;
	ConnectionStatus status;
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
	FCP_ACK
} FCPOpcode;

#pragma pack(1)
//The pragma directive shouldn't be needed, the struct is ordered in a way that should
// result in the correct alignment. However, better include it to be sure
typedef struct FCPMessage{
	int32_t size;
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

#ifdef no
#define FCP_OPEN 'O'
#define FCP_READ 'R'
#define FCP_READ_N 'N'
#define FCP_WRITE 'W'
#define FCP_APPEND 'A'
#define FCP_LOCK 'L'
#define FCP_UNLOCK 'U'
#define FCP_CLOSE 'C'
#define FCP_REMOVE 'D'
#endif

#endif //SOL_PROJECT_FILECACHINGPROTOCOL_H
