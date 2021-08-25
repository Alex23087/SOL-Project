#ifndef SOL_PROJECT_QUEUE_H
#define SOL_PROJECT_QUEUE_H

#include "defines.h"



typedef struct Queue{
	void* data;
	struct Queue* next;
} Queue;



void queueFree(Queue* queue);

bool queueIsEmpty(Queue* queue);

void* queuePop(Queue** queue);

void queuePush(Queue** queue, void* data);

#endif //SOL_PROJECT_QUEUE_H
