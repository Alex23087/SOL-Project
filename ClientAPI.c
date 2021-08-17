#include <sys/un.h>
#include <bits/types/struct_timespec.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#include "ClientAPI.h"
#include "timespecUtils.h"

int openConnection(const char* sockname, int msec, const struct timespec abstime){
	//Connect to server
	int clientSocketDescriptor = -1;
	struct sockaddr_un serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sun_family = AF_UNIX;
	strncpy(serverAddress.sun_path, sockname, strlen(sockname) + 1);
	
	clientSocketDescriptor = socket(AF_UNIX, SOCK_STREAM, 0);
	if(clientSocketDescriptor < 0){
		perror("Error while creating socket");
		return -1;
	}
	
	struct timespec currentTime;
	clock_gettime(CLOCK_REALTIME, &currentTime);
	
	printf("%d %d %d\n", compareTimes(abstime, currentTime), compareTimes(currentTime, abstime), compareTimes(currentTime, currentTime));
	
	struct timespec deadlineTime = addTimes(currentTime, abstime);
	struct timespec endTime = addTimes(currentTime, doubleToTimespec(msec * 1e-3));
	
	while(connect(clientSocketDescriptor, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) && (compareTimes(deadlineTime, currentTime) > 0)){
		perror("Error while connecting to socket");
		if(compareTimes(deadlineTime, endTime) >= 0){
			usleep(msec * 1e3L);
		}else{
			break;
		}
		clock_gettime(CLOCK_REALTIME, &currentTime);
		endTime = addTimes(currentTime, doubleToTimespec(msec * 1e-3));
	}
}