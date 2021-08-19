#include <stdint-gcc.h>
#include <stddef.h>
#include <malloc.h>
#include <memory.h>
#include "FileCachingProtocol.h"
#include "ion.h"

FCPMessage* fcpMessageFromBuffer(char buffer[FCP_MESSAGE_LENGTH]){
	FCPMessage* out = malloc(FCP_MESSAGE_LENGTH);
	memcpy(out, buffer, FCP_MESSAGE_LENGTH);
	return out;
}

char* fcpBufferFromMessage(FCPMessage message){
	char* out = malloc(FCP_MESSAGE_LENGTH);
	out = memcpy(out, &message, FCP_MESSAGE_LENGTH);
	return out;
}

FCPMessage* fcpMakeMessage(FCPOpcode operation, int32_t size, char* filename){
	FCPMessage* message = malloc(FCP_MESSAGE_LENGTH);
	message->op = operation;
	message->size = size;
	if(filename != NULL){
		strncpy(message->filename, filename, FCP_MESSAGE_LENGTH - 5);
	}
	return message;
}

void fcpSend(FCPOpcode operation, int32_t size, char* filename, int fd){
	FCPMessage* message = fcpMakeMessage(operation, size, filename);
	char* buffer = fcpBufferFromMessage(*message);
	writen(fd, buffer, FCP_MESSAGE_LENGTH);
	free(message);
	free(buffer);
}

void clientListAdd(ClientList** list, int descriptor){
	ClientList* newNode = malloc(sizeof(ClientList));
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

void clientListRemove(ClientList** list, int descriptor){
	ClientList* current = *list;
	if(current == NULL){
		return;
	}
	if(current->descriptor == descriptor){
		free(*list);
		*list = NULL;
		return;
	}
	while(current->next != NULL && current->next->descriptor != descriptor){
		current = current->next;
	}
	if(current->next == NULL){
		return;
	}
	
	ClientList* tmp = current->next->next;
	free(current->next);
	current->next = tmp;
}

void clientListUpdateStatus(ClientList* list, int descriptor, ConnectionStatus status){
	ClientList* current = list;
	while(current != NULL){
		if(current->descriptor == descriptor){
			current->status = status;
			return;
		}
		current = current->next;
	}
}

ConnectionStatus clientListGetStatus(ClientList* list, int descriptor){
	ClientList* current = list;
	while(current != NULL){
		if(current->descriptor == descriptor){
			return current->status;
		}
		current = current->next;
	}
}