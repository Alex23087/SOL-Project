#include <unistd.h>

#include "server.h"
#include "client.h"

int main() {
    /*
    ArgsList* args = readConfigFile("/home/alex23087/test.conf");
	
    printf("%s\n", (char*)(getNodeForKey(args, "asd")->data));
    printf("%ld\n", getLongValue(args, "param"));
    printf("%s\n", getStringValue(args, "asd"));
    printf("%s\n", getStringValue(args, "param"));
    
	freeArgsListNode(args);
    */
    if(fork()){
    	sleep(1);
    	const int len = 2;
    	char* argv[len];
    	argv[0] = "server";
    	argv[1] = "-h";
    	serverMain(len, (char **) argv);
    }else{
    	const int len = 7;
    	char* argv[len];
    	argv[0] = "client";
    	argv[1] = "-D";
    	argv[2] = "test";
    	argv[3] = "-w";
    	argv[4] = "test";
    	argv[5] = "-f";
    	argv[6] = "/tmp/LSOfilestorage.sk";
    	clientMain(len, (char **) argv);
    }
    return 0;
}
