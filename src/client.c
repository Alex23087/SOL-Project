#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "include/ClientAPI.h"
#include "include/defines.h"
#include "include/ion.h"
#include "include/PathUtils.h"
#include "include/Queue.h"



typedef enum ClientOperation{
	WriteFile,
	WriteFolder,
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



static int clientWriteFile (const char* fpath, const struct stat* sb, int typeflag);



static unsigned long filesToSend;
static char* cacheMissFolderPath = NULL;



#ifdef IDE
int clientMain(int argc, char** argv){
#else
int main(int argc, char** argv){
#endif
	char opt;
	opterr = 0;
	bool finished = false;
	
	char* socketPath = NULL;
	char* readOperationFolderPath = NULL;
	bool issuedWriteOperation = false;
	bool issuedReadOperation = false;
	unsigned long timeBetweenRequests = 0;
	Queue* commandQueue = NULL;
	
	while(!finished){
		opt = getopt(argc, argv, "hf:w:W:D:r:R::d:t:l:u:c:pva:");
		switch(opt){
			case 'h':{
				printf(
						"Usage: %s [OPTIONS...]\n\n"
						"  -h\t\t\tPrints this help message.\n\n"
						"  -f filename\t\tSpecifies the socket to connect to.\n\n"
						"  -w dirname[,n=0]\tWrites the files contained in the 'dirname'\n"
						"\t\t\tdirectory to the server. If the directory contains\n"
						"\t\t\tother subdirectories, these will be visited\n"
						"\t\t\trecursively. If the parameter 'n' is not specified,\n"
						"\t\t\tor it is equal to 0, all files found will be sent,\n"
						"\t\t\totherwise only the first n files found will be sent.\n\n"
						"  -W file1[,file2...]\tWrites the files specified (separated by a ',')\n"
						"\t\t\tto the server.\n\n"
						"  -D dirname\t\tSpecifies the folder where to save files that are\n"
						"\t\t\tsent by the server because of a capacity miss.\n"
						"\t\t\tIf this option is not specified, such files will be\n"
						"\t\t\tignored by the client.\n"
						"\t\t\tIf this option is specified, but neither -w nor -W\n"
						"\t\t\tare used, the program will terminate with an error.\n\n"
						"  -r file1[,file2...]\tReads the files specified (separated by a ',')\n"
						"\t\t\tfrom the server.\n\n"
						"  -R [n=0]\t\tReads any n files from the server. If n is not\n"
						"\t\t\tspecified, or n=0, all the files currently in\n"
						"\t\t\tthe server are read\n\n"
						"  -d dirname\t\tSpecifies the folder where to save files that are\n"
						"\t\t\tread from the server with the options -r and -R.\n"
						"\t\t\tIf this option is not specified, such files will be\n"
						"\t\t\tignored by the client.\n"
						"\t\t\tIf this option is specified, but neither -r nor -R\n"
						"\t\t\tare used, the program will terminate with an error.\n\n"
						"  -t time\t\tTime in milliseconds between two requests\n"
						"\t\t\tto the server.\n\n"
						"  -l file1[,file2...]\tAcquires a lock on the files specified\n"
						"\t\t\t(separated by a ',').\n\n"
						"  -u file1[,file2...]\tReleases the lock on the files specified\n"
						"\t\t\t(separated by a ',').\n\n"
						"  -p, -v\t\tPrints info about each operation.\n\n", basename(argv[0]));
				finished = true;
				queueFree(commandQueue);
				return 0;
			}
			case 'f':{
				socketPath = optarg;
				break;
			}
			case 'w':{
				issuedWriteOperation = true;
				ClientCommand* cmd = malloc(sizeof(ClientCommand));
				cmd->op = WriteFolder;
				cmd->parameter.stringValue = optarg;
				queuePush(&commandQueue, (void*)cmd);
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
				if(optarg == NULL){
					cmd->parameter.intValue = 0;
				}else{
					errno = 0;
					char* endptr = NULL;
					if(strlen(optarg) < 3){
						fprintf(stderr, "Invalid string passed as parameter to -R: \"%s\"\n", optarg);
						return -1;
					}
					cmd->parameter.intValue = (int)strtol(optarg + 2, &endptr, 10);
					if(endptr != NULL && !(isspace(*endptr) || *endptr == 0)){
						fprintf(stderr, "Invalid string passed as time: \"%s\"\n", optarg);
						return -1;
					}
					if(errno != 0){
						//TODO: Handle error
						perror("Error in \n");
						return -1;
					}
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
			case 'v':
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
	
	finished = false;
	
	if(openConnection(socketPath, 400, ts)){
		perror("Error while connecting to server");
		finished = true;
	}
	
	//Process command queue
	while(!finished && !queueIsEmpty(commandQueue)){
		ClientCommand* currentCommand = (ClientCommand*)queuePop(&commandQueue);
		switch(currentCommand->op) {
			case WriteFile:{
				char* savePtr = NULL;
				char* token = strtok_r(currentCommand->parameter.stringValue, ",", &savePtr);
				while(token != NULL) {
					if(openFile2(token, cacheMissFolderPath, O_CREATE | O_LOCK)){
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
						errno = 0;
						if(readFile(token, (void**)(&fileBuffer), &fileSize)){
							perror("Error while reading file from server");
							finished = true;
						}else if(unlockFile(token)){
							perror("Error while unlocking file");
							finished = true;
						}else if(closeFile(token)){
							perror("Error while closing file");
							finished = true;
						}else{
							//Operation successful, save file
							if(readOperationFolderPath != NULL){
								char* newFileName = replaceBasename(readOperationFolderPath, token);
								printf("Saving file to: %s\n", newFileName);
								int fileDescriptor = open(newFileName, O_CREAT | O_WRONLY | O_TRUNC, 0644);
								if(fileDescriptor == -1){
									perror("Error while saving file");
									finished = true;
								}else{
									writen(fileDescriptor, fileBuffer, fileSize);
									close(fileDescriptor);
									printf("File saved to: %s\n", newFileName);
								}
								free(newFileName);
							}else{
								printf("No output directory specified, not saving file\n");
							}
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
					}
					if(lockFile(token)){
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
					if(closeFile(token)){
						perror("Error while closing file");
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
					if(openFile(token, O_LOCK)){
						perror("Error while opening file");
						finished = true;
						break;
					}
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
			case WriteFolder:{
				char* savePtr = NULL;
				char* dirname = strtok_r(currentCommand->parameter.stringValue, ",", &savePtr);
				if(dirname == NULL){
					//Error: no file passed
					fprintf(stderr, "No dirname passed to option -w\n");
					finished = true;
					break;
				}
				char* nString = strtok_r(NULL, ",", &savePtr);
				if(nString == NULL){
					filesToSend = 0;
				}else{
					errno = 0;
					char* endptr = NULL;
					if(strlen(nString) < 3){
						fprintf(stderr, "Invalid string passed as n to option -w: \"%s\"\n", nString);
						finished = true;
						break;
					}
					filesToSend = strtoul(nString+2, &endptr, 10);
					//Encoding values different from 0 by adding 1, because of how the value filesToSend is used in clientWriteFile
					if(filesToSend != 0){
						filesToSend++;
					}
					if(endptr != NULL && !(isspace(*endptr) || *endptr == 0)){
						fprintf(stderr, "Invalid string passed as n to option -w: \"%s\"\n", nString);
						finished = true;
						break;
					}
					if(errno != 0){
						perror("Invalid number passed as n to option -w\n");
						finished = true;
						break;
					}
				}
				
				
				if(ftw(currentCommand->parameter.stringValue, clientWriteFile, 15)){
					if(filesToSend != 1){
						perror("Error while sending files in folder");
						finished = true;
					}
				}
				break;
			}
		}
		free(currentCommand);
		usleep(timeBetweenRequests * 1000);
	}
	
	
	closeConnection(socketPath);
	
	return 0;
}

static int clientWriteFile (const char* fpath, const struct stat* sb, int typeflag){
	if(filesToSend != 1){
		if(typeflag == FTW_F){
			if(openFile(fpath, O_CREATE | O_LOCK)){
				perror("Error while opening file");
				return -1;
			}
			if(writeFile(fpath, cacheMissFolderPath)){
				perror("Error while writing file to server");
				return -1;
			}
			if(closeFile(fpath)){
				perror("Error while closing file");
				return -1;
			}
			if(filesToSend != 0){
				filesToSend--;
			}
		}
	}else{
		return -1;
	}
	return 0;
}