#include "../include/FileCachingProtocol.h"
#include "../include/ClientAPI.h"
#include <libgen.h>
#include <malloc.h>
#include <sys/un.h>
#include "../include/PathUtils.h"

char* replaceBasename(const char* dirname, char* filename){
	size_t dirnameLen = strlen(dirname);
	bool shouldAppendSeparator = (dirname[dirnameLen - 1] != '/');
	
	const char* fileBasename = basename(filename);
	size_t nameLength = strlen(dirname) + strlen(fileBasename) + 1 + shouldAppendSeparator;
	char* fullName = malloc(nameLength);
	memset(fullName, 0, nameLength);
	strcpy(fullName, dirname);
	if(shouldAppendSeparator){
		strcat(fullName, "/");
	}
	strcat(fullName, fileBasename);
	return fullName;
}