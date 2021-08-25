#include "../include/ServerLib.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../include/ClientAPI.h"
#include "../include/ion.h"
#include "../include/W2M.h"



int w2mPipeDescriptors[2];



int getIntFromW2MMessage(char message[W2M_MESSAGE_LENGTH]){
	return message[4] + (message[3] << 8) + (message[2] << 16) + (message[1] << 24);
}

char* makeW2MMessage(char message, int32_t data, char out[W2M_MESSAGE_LENGTH]){
	out[0] = message;
	switch(message){
		case W2M_CLIENT_SERVED: case W2M_CLIENT_DISCONNECTED:{
			out[1] = (data >> 24) & 0xFF;
			out[2] = (data >> 16) & 0xFF;
			out[3] = (data >> 8) & 0xFF;
			out[4] = data & 0xFF;
			break;
		}
		default:{
			for(size_t i = 1; i < W2M_MESSAGE_LENGTH; i++){
				out[i] = 0;
			}
		}
	}

	return out;
}

void w2mSend(char message, int32_t data){
	char buffer[W2M_MESSAGE_LENGTH];
	writen(w2mPipeDescriptors[1], makeW2MMessage(message, data, buffer), W2M_MESSAGE_LENGTH);
}