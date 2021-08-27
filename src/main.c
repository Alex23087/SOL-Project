#include <stdio.h>
#include <unistd.h>

#include "include/client.h"
#include "include/server.h"

int main() {
    /*
    ArgsList* args = readConfigFile("/home/alex23087/test.conf");
	
    printf("%s\n", (char*)(getNodeForKey(args, "asd")->data));
    printf("%ld\n", getLongValue(args, "param"));
    printf("%s\n", getStringValue(args, "asd"));
    printf("%s\n", getStringValue(args, "param"));
    
	freeArgsListNode(args);
    */
    setbuf(stdout, NULL); //Set print buffer to 0 because of problems with the IDE
    
    if(1){
    	const int len = 3;
    	char* argv[len];
    	argv[0] = "server";
    	argv[1] = "-c";
    	argv[2] = "/mnt/e/Progetti/SOL-Project/tests/test1/config.txt";
    	serverMain(len, (char **) argv);
    }else{
    	const int len = 5;
    	char* argv[len];
    	argv[0] = "client";
    	argv[1] = "-w";
    	argv[2] = "/mnt/e/Progetti/SOL-Project/tests/test1/files/tmp/config.txt";
    	argv[3] = "-f";
    	argv[4] = "/tmp/LSOfilestorage.sk";
    	clientMain(len, (char **) argv);
    }
    return 0;
}
