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
    
    if(0){
    	const int len = 3;
    	char* argv[len];
    	argv[0] = "server";
    	argv[1] = "-c";
    	argv[2] = "/mnt/e/Progetti/SOL-Project/tests/test3/config.txt";
    	serverMain(len, (char **) argv);
    }else{
    	const int len = 14;
    	char* argv[len];
    	argv[0] = "client";
        argv[1] = "-f";
        argv[2] = "/tmp/LSOfilestorage.sk";
        argv[3] = "-t";
        argv[4] = "0";
        argv[5] = "-p";
        argv[6] = "-D";
        argv[7] = "/mnt/e/Progetti/SOL-Project/tests/test3/tmp";
        argv[8] = "-d";
        argv[9] = "/mnt/e/Progetti/SOL-Project/tests/test3/tmp";
        argv[10] = "-W";
        argv[11] = "/mnt/e/Progetti/SOL-Project/tests/cats/small/ion.h";
        argv[12] = "-r";
        argv[13] = "/mnt/e/Progetti/SOL-Project/tests/cats/small/ion.h";

    	clientMain(len, (char **) argv);
    }
    return 0;
}
