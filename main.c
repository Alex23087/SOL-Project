#include <unistd.h>
#include <stdio.h>

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
    setbuf(stdout, NULL); //Set print buffer to 0 because of problems with the IDE
    
    if(1){
    	const int len = 2;
    	char* argv[len];
    	argv[0] = "server";
    	argv[1] = "-h";
    	serverMain(len, (char **) argv);
    }else{
    	const int len = 5;
    	char* argv[len];
    	argv[0] = "client";
    	argv[1] = "-r";
    	argv[2] = "/mnt/e/Progetti/SOL-Project/config.txt";
    	argv[3] = "-f";
    	argv[4] = "/tmp/LSOfilestorage.sk";
    	clientMain(len, (char **) argv);
    }
    return 0;
}
