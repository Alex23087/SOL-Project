#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "ClientAPI.h"
#include "defines.h"

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
	
	while(!finished){
		opt = getopt(argc, argv, "hf:w:W:D:r:R:d:t:l:u:c:p:");
		switch(opt){
			case 'h':{
				//TODO: Write client help
				printf("%s\n", argv[0]);
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
				//TODO: Implement this
				break;
			}
			case 'D':{
				cacheMissFolderPath = optarg;
				break;
			}
			case 'r':{
				issuedReadOperation = true;
				//TODO: Implement this
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
	if(issuedWriteOperation){
		writeFile(NULL, NULL);
	}
	sleep(1);
	closeConnection(socketPath);
	
	return 0;
}