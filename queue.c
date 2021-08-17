#include <stddef.h>
#include <malloc.h>
#include "queue.h"

Queue* initQueueNode(int data){
	Queue* out = malloc(sizeof(Queue));
	out->data = data;
	out->next = NULL;
	return out;
}

void queuePush(Queue** queue, int data){
	Queue* current = *queue;
	if(current == NULL){
		*queue = initQueueNode(data);
		return;
	}
	while(current->next != NULL){
		current = current->next;
	}
	current->next = initQueueNode(data);
}

int queuePop(Queue** queue){
	Queue* tmp = *queue;
	*queue = (*queue)->next;
	int out = tmp->data;
	free(tmp);
	return out;
}

void queueFree(Queue* queue){
	if(queue == NULL){
		return;
	}
	queueFree(queue->next);
	free(queue);
}

bool queueIsEmpty(Queue* queue){
	return queue == NULL;
}