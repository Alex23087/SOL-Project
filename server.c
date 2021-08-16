#include <getopt.h>
#include <stdio.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "defines.h"
#include "ParseUtils.h"

#define MAX_BACKLOG 10
#define cleanup() \
	unlink(socketPath);\
	free(socketPath);\
	free(logFilePath);

int serverMain(int argc, char** argv){
	char* configFilePath = "/mnt/e/Progetti/SOL-Project/config.txt";
	unsigned short nWorkers = 10;
	unsigned int maxFiles = 100;
	unsigned long storageSize = 1024 * 1024 * 1024;
	char* socketPath = NULL;
	char* logFilePath = NULL;
	
	//Command line options parsing
	char opt;
	opterr = 0;
	bool finished = false;
	
	while(!finished){
		opt = getopt(argc, argv, "hf:c:");
		switch(opt){
			case 'h':{
				//TODO: Write server help
				printf("%s\n", argv[0]);
				break;
			}
			case 'f':{
				socketPath = optarg;
				break;
			}
			case 'c':{
				configFilePath = optarg;
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
	
	
	//Config file parsing
	ArgsList* configArgs = readConfigFile(configFilePath);
	nWorkers = getLongValue(configArgs, "nWorkers");
	maxFiles = getLongValue(configArgs, "maxFiles");
	socketPath = getStringValue(configArgs, "socketPath");
	logFilePath = getStringValue(configArgs, "logFile");
	storageSize = getLongValue(configArgs, "storageSize");
	//TODO: Maybe change the config so that you can specify sizes such as "100M", "1G", etc.
	freeArgsListNode(configArgs);
	
	
	//Creating socket
	int serverSocketDescriptor = -1;
	struct sockaddr_un serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sun_family = AF_UNIX;
	strncpy(serverAddress.sun_path, socketPath, strlen(socketPath) + 1);
	
	serverSocketDescriptor = socket(AF_UNIX, SOCK_STREAM, 0);
	if(serverSocketDescriptor < 0){
		perror("Error while creating the server socket");
		cleanup();
		return -1;
	}
	
	if(bind(serverSocketDescriptor, (struct sockaddr *)&serverAddress, sizeof(serverAddress))){
		perror("Error while binding socket");
		cleanup();
		return -1;
	}
	
	if(listen(serverSocketDescriptor, MAX_BACKLOG)){
		perror("Error while setting socket to listen");
		cleanup();
		return -1;
	}
	
	int clientDescriptor = accept(serverSocketDescriptor, NULL, NULL);
	if(clientDescriptor < 0){
		perror("Error while accepting a new connection");
		cleanup();
		return -1;
	}
	printf("Incoming connection received");
	
	
	cleanup();
	return 0;
}