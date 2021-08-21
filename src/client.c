#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "include/ClientAPI.h"
#include "include/defines.h"
#include "include/Queue.h"

typedef enum ClientOperation{
	WriteFile,
	ReadFile
} ClientOperation;

typedef union ClientParameter{
	int intValue;
	char* stringValue;
} ClientParameter;

typedef struct ClientCommand{
	ClientOperation op;
	ClientParameter parameter;
} ClientCommand;

#ifdef IDE
int clientMain(int argc, char** argv){
#else
	int main(int argc, char** argv){
#endif
	char opt;
	opterr = 0;
	bool finished = false;
	
	char* socketPath = NULL;
	char* cacheMissFolderPath = NULL;
	char* readOperationFolderPath = NULL;
	bool issuedWriteOperation = false;
	bool issuedReadOperation = false;
	unsigned long timeBetweenRequests = 0;
	Queue* commandQueue = NULL;
	
	while(!finished){
		opt = getopt(argc, argv, "hf:w:W:D:r:R:d:t:l:u:c:p:");
		switch(opt){
			case 'h':{
				//TODO: Write client help
				printf("%s\n", argv[0]);
				finished = true;
				break;
			}
			case 'f':{
				socketPath = optarg;
				break;
			}
			case 'w':{
				issuedWriteOperation = true;
				//TODO: Implement this
				break;
			}
			case 'W':{
				issuedWriteOperation = true;
				ClientCommand* cmd = malloc(sizeof(ClientCommand));
				cmd->op = WriteFile;
				cmd->parameter.stringValue = optarg;
				queuePush(&commandQueue, (void*)cmd);
				break;
			}
			case 'D':{
				cacheMissFolderPath = optarg;
				break;
			}
			case 'r':{
				issuedReadOperation = true;
				ClientCommand* cmd = malloc(sizeof(ClientCommand));
				cmd->op = ReadFile;
				cmd->parameter.stringValue = optarg;
				queuePush(&commandQueue, (void*)cmd);
				break;
			}
			case 'R':{
				issuedReadOperation = true;
				//TODO: Implement this
				break;
			}
			case 'd':{
				readOperationFolderPath = optarg;
			}
			case 't':{
				errno = 0;
				timeBetweenRequests = strtoul(optarg, NULL, 10);
				if(errno != 0){
					//TODO: Handle error
					perror("Invalid number passed as time\n");
					return -1;
				}
				
			}
			case 'l':{
				//TODO: Implement this
				break;
			}
			case 'u':{
				//TODO: Implement this
				break;
			}
			case 'c':{
				//TODO: Implement this
				break;
			}
			case '?':{
				fprintf(stderr, "Unrecognized option: %c\n", optopt);
				finished = true;
				return -1;
			}
			case -1:{
				finished = true;
				break;
			}
		}
	}
	
	
	//Checking if the options passed are valid
	if(socketPath == NULL){
		fprintf(stderr, "No server address specified\n");
		return -1;
	}
	if(cacheMissFolderPath != NULL && !issuedWriteOperation){
		//Specification asks to return an error if option D has been specified and there are no writes
		fprintf(stderr, "Option -D has been passed, but there are no write operations\n");
		return -1;
	}
	if(readOperationFolderPath != NULL && !issuedReadOperation){
		//Specification asks to return an error if option d has been specified and there are no reads
		fprintf(stderr, "Option -d has been passed, but there are no read operations\n");
		return -1;
	}
	
	struct timespec ts;
	ts.tv_sec = 2;
	ts.tv_nsec = 0;
	
	openConnection(socketPath, 400, ts);
	sleep(1);
	
	
	//Process command queue
	finished = false;
	while(!finished && !queueIsEmpty(commandQueue)){
		ClientCommand* currentCommand = (ClientCommand*)queuePop(&commandQueue);
		switch(currentCommand->op) {
			case WriteFile:{
				if(openFile(currentCommand->parameter.stringValue, O_CREATE | O_LOCK)){
					perror("Error while opening file");
					finished = true;
				}else{
					if(writeFile(currentCommand->parameter.stringValue, cacheMissFolderPath)){
						perror("Error while writing file to server");
						finished = true;
					}else{
						if(closeFile(currentCommand->parameter.stringValue)){
							perror("Error while closing file");
						}
					}
				}
				break;
			}
			case ReadFile:{
				/*if(openFile(currentCommand->parameter.stringValue, O_LOCK)){
					perror("Error while opening file");
					finished = true;
				}else{
					char* fileBuffer;
					size_t fileSize = 0;
					if(readFile(currentCommand->parameter.stringValue, (void**)(&fileBuffer), &fileSize)){
						perror("Error while reading file from server");
						finished = true;
					}else{
						if(closeFile(currentCommand->parameter.stringValue)){
							perror("Error while closing file");
							finished = true;
						}
					}
					free(fileBuffer);
				}*/
				if(openFile(currentCommand->parameter.stringValue, 0)){
					perror("Error while opening file");
					finished = true;
				}else{
					if(lockFile(currentCommand->parameter.stringValue)){
						perror("Error while locking file");
						finished = true;
					}else{
						char* fileBuffer;
						size_t fileSize = 0;
						if(readFile(currentCommand->parameter.stringValue, (void**)(&fileBuffer), &fileSize)){
							perror("Error while reading file from server");
							finished = true;
						}else{
							if(unlockFile(currentCommand->parameter.stringValue)){
								perror("Error while unlocking file");
								finished = true;
							}else{
								if(closeFile(currentCommand->parameter.stringValue)){
									perror("Error while closing file");
									finished = true;
								}
							}
						}
						free(fileBuffer);
					}
				}
			}
		}
		free(currentCommand);
	}
	
	
	closeConnection(socketPath);
	
	return 0;
}