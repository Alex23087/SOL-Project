#include <malloc.h>
#include <stddef.h>

#include "../include/Queue.h"



static Queue* initQueueNode(void* data){
	Queue* out = malloc(sizeof(Queue));
	out->data = data;
	out->next = NULL;
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

void* queuePop(Queue** queue){
    Queue* tmp = *queue;
    *queue = (*queue)->next;
    void* out = tmp->data;
    free(tmp);
    return out;
}

void queuePush(Queue** queue, void* data){
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
