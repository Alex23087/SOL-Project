#ifndef SOL_PROJECT_QUEUE_H
#define SOL_PROJECT_QUEUE_H

#include "defines.h"
typedef struct Queue{
	int data;
	struct Queue* next;
} Queue;

void queuePush(Queue** queue, int data);

int queuePop(Queue** queue);

void queueFree(Queue* queue);

bool queueIsEmpty(Queue* queue);

#endif //SOL_PROJECT_QUEUE_H
