#ifndef SOL_PROJECT_W2M_H
#define SOL_PROJECT_W2M_H

#define W2M_CLIENT_DISCONNECTED 'D'
#define W2M_SIGNAL_HANG 'H'
#define W2M_CLIENT_SERVED 'F'
#define W2M_SIGNAL_TERM 'T'
#define W2M_MESSAGE_LENGTH 5



extern int w2mPipeDescriptors[2];



int getIntFromW2MMessage(char message[W2M_MESSAGE_LENGTH]);

char* makeW2MMessage(char message, int32_t data, char out[W2M_MESSAGE_LENGTH]);

void w2mSend(char message, int32_t data);

#endif //SOL_PROJECT_W2M_H
