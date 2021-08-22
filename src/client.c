#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "include/ClientAPI.h"
#include "include/defines.h"
#include "include/Queue.h"
#include "include/ion.h"

typedef enum ClientOperation{
	WriteFile,
	ReadFile,
	LockFile,
	UnlockFile,
	RemoveFile,
	ReadNFiles,
	AppendFile
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
		opt = getopt(argc, argv, "hf:w:W:D:r:R:d:t:l:u:c:pa:");
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
				ClientCommand* cmd = malloc(sizeof(ClientCommand));
				cmd->op = ReadNFiles;
				errno = 0;
				char* endptr = NULL;
				cmd->parameter.intValue = (int)strtol(optarg, &endptr, 10);
				if(endptr != NULL && !(isspace(*endptr) || *endptr == 0)){
					fprintf(stderr, "Invalid string passed as time: \"%s\"\n", optarg);
					return -1;
				}
				if(errno != 0){
					//TODO: Handle error
					perror("Error in \n");
					return -1;
				}
				queuePush(&commandQueue, (void*)cmd);
				break;
			}
			case 'd':{
				readOperationFolderPath = optarg;
				break;
			}
			case 't':{
				errno = 0;
				char* endptr = NULL;
				timeBetweenRequests = strtoul(optarg, &endptr, 10);
				if(endptr != NULL && !(isspace(*endptr) || *endptr == 0)){
					fprintf(stderr, "Invalid string passed as time: \"%s\"\n", optarg);
					return -1;
				}
				if(errno != 0){
					//TODO: Handle error
					perror("Invalid number passed as time\n");
					return -1;
				}
				printf("TimeBetweenRequests: %lu\n", timeBetweenRequests);
				break;
			}
			case 'l':{
				ClientCommand* cmd = malloc(sizeof(ClientCommand));
				cmd->op = LockFile;
				cmd->parameter.stringValue = optarg;
				queuePush(&commandQueue, (void*)cmd);
				break;
			}
			case 'u':{
				ClientCommand* cmd = malloc(sizeof(ClientCommand));
				cmd->op = UnlockFile;
				cmd->parameter.stringValue = optarg;
				queuePush(&commandQueue, (void*)cmd);
				break;
			}
			case 'c':{
				ClientCommand* cmd = malloc(sizeof(ClientCommand));
				cmd->op = RemoveFile;
				cmd->parameter.stringValue = optarg;
				queuePush(&commandQueue, (void*)cmd);
				break;
			}
			case 'p':{
				//TODO: Implement this
				break;
			}
			case 'a':{
				issuedWriteOperation = true;
				ClientCommand* cmd = malloc(sizeof(ClientCommand));
				cmd->op = AppendFile;
				cmd->parameter.stringValue = optarg;
				queuePush(&commandQueue, (void*)cmd);
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
	
	
	//Process command queue
	finished = false;
	while(!finished && !queueIsEmpty(commandQueue)){
		ClientCommand* currentCommand = (ClientCommand*)queuePop(&commandQueue);
		switch(currentCommand->op) {
			case WriteFile:{
				char* savePtr = NULL;
				char* token = strtok_r(currentCommand->parameter.stringValue, ",", &savePtr);
				while(token != NULL) {
					if(openFile(token, O_CREATE | O_LOCK)){
						perror("Error while opening file");
						finished = true;
					}else{
						if(writeFile(token, cacheMissFolderPath)){
							perror("Error while writing file to server");
							finished = true;
						}else{
							if(closeFile(token)){
								perror("Error while closing file");
								finished = true;
							}
						}
					}
					token = strtok_r(NULL, ",", &savePtr);
				}
				break;
			}
			case ReadFile:{
				char* savePtr = NULL;
				char* token = strtok_r(currentCommand->parameter.stringValue, ",", &savePtr);
				while(token != NULL) {
					if(openFile(token, 0)){
						perror("Error while opening file");
						finished = true;
					}else if(lockFile(token)){
						perror("Error while locking file");
						finished = true;
					}else{
						char* fileBuffer;
						size_t fileSize = 0;
						if(readFile(token, (void**)(&fileBuffer), &fileSize)){
							perror("Error while reading file from server");
							finished = true;
						}else if(unlockFile(token)){
							perror("Error while unlocking file");
							finished = true;
						}else if(closeFile(token)){
							perror("Error while closing file");
							finished = true;
						}
						free(fileBuffer);
					}
					token = strtok_r(NULL, ",", &savePtr);
				}
				break;
			}
			case LockFile:{
				char* savePtr = NULL;
				char* token = strtok_r(currentCommand->parameter.stringValue, ",", &savePtr);
				while(token != NULL) {
					if(openFile(token, 0)){
						perror("Error while opening file");
						finished = true;
						break;
					}else if(lockFile(token)){
						perror("Error while locking file");
						finished = true;
						break;
					}
					token = strtok_r(NULL, ",", &savePtr);
				}
				break;
			}
			case UnlockFile:{
				char* savePtr = NULL;
				char* token = strtok_r(currentCommand->parameter.stringValue, ",", &savePtr);
				while(token != NULL) {
					if(unlockFile(token)){
						perror("Error while unlocking file");
						finished = true;
						break;
					}
					token = strtok_r(NULL, ",", &savePtr);
				}
				break;
			}
			case RemoveFile:{
				char* savePtr = NULL;
				char* token = strtok_r(currentCommand->parameter.stringValue, ",", &savePtr);
				while(token != NULL) {
					if(removeFile(token)){
						perror("Error while deleting file");
						finished = true;
						break;
					}
					token = strtok_r(NULL, ",", &savePtr);
				}
				break;
			}
			case ReadNFiles:{
				if(readNFiles(currentCommand->parameter.intValue, readOperationFolderPath)){
					perror("Error while reading N files");
					finished = true;
					break;
				}
				break;
			}
			case AppendFile:{
				char* savePtr = NULL;
				char* firstFile = strtok_r(currentCommand->parameter.stringValue, ",", &savePtr);
				if(firstFile == NULL){
					//Error: no file passed
					fprintf(stderr, "No file passed to option -a\n");
					finished = true;
					break;
				}
				char* secondFile = strtok_r(NULL, ",", &savePtr);
				if(secondFile == NULL){
					//Error: only one file passed
					fprintf(stderr, "Only one file passed to option -a\n");
					finished = true;
					break;
				}
				if(strtok_r(NULL, ",", &savePtr) != NULL){
					//Error: more than two files passed
					fprintf(stderr, "More than two files passed to option -a\n");
					finished = true;
					break;
				}
				
				if(openFile(firstFile, O_LOCK)){
					perror("Error while opening file on server");
					finished = true;
					break;
				}
				
				int fileDescriptor = open(secondFile, O_RDONLY);
				if(fileDescriptor <= -1){
					perror("Error while opening file to append");
					finished = true;
					break;
				}
				
				struct stat fileStat;
				if(fstat(fileDescriptor, &fileStat)){
					//Fstat failed
					perror("Error while getting file info");
					finished = true;
					break;
				}
				off_t fileSize = fileStat.st_size;
				
				char* fileBuffer = malloc(fileSize);
				readn(fileDescriptor, fileBuffer, fileSize);
				
				if(appendToFile(firstFile, fileBuffer, fileSize, cacheMissFolderPath)){
					perror("Error while appending file to server");
					finished = true;
				}else{
					if(closeFile(firstFile)){
						perror("Error while closing file");
						finished = true;
					}
				}
				
				free(fileBuffer);
				close(fileDescriptor);
				break;
			}
		}
		free(currentCommand);
		usleep(timeBetweenRequests * 1000);
	}
	
	
	closeConnection(socketPath);
	
	return 0;
}