#ifndef SOL_PROJECT_CLIENTAPI_H
#define SOL_PROJECT_CLIENTAPI_H

#include <stddef.h>
#include <time.h>

#define O_CREATE 1
#define O_LOCK 2



int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname);

int closeConnection(const char *sockname);

int closeFile(const char* pathname);

int lockFile(const char* pathname);

int openConnection(const char* sockname, int msec, const struct timespec abstime);

int openFile(const char* pathname, int flags);

int openFile2(const char* pathname, const char* dirname, int flags);

int readFile(const char* pathname, void** buf, size_t* size);

int readNFiles(int N, const char* dirname);

int removeFile(const char* pathname);

int writeFile(const char* pathname, const char* dirname);

int unlockFile(const char* pathname);

#endif //SOL_PROJECT_CLIENTAPI_H