#ifndef SOL_PROJECT_FILECACHE_H
#define SOL_PROJECT_FILECACHE_H

#include <malloc.h>
#include <string.h>
#include <pthread.h>
#include "defines.h"
#include "FileCachingProtocol.h"

#define MAX_FILENAME_SIZE FCP_MESSAGE_LENGTH - 5

typedef struct CachedFile{
	char* filename;
	char* contents;
	int lockedBy;
	pthread_mutex_t* lock;
} CachedFile;

typedef struct FileList{
	CachedFile* file;
	struct FileList* next;
} FileList;

typedef enum CacheAlgorithm{
	FIFO
} CacheAlgorithm;

typedef struct FileCache{
	unsigned int maxFiles;
	unsigned long maxSize;
	FileList* files;
	CacheAlgorithm FIFO;
} FileCache;

CachedFile* initCachedFile(const char* filename);

FileCache* initFileCache(unsigned int maxFiles, unsigned long maxSize);

void addFile(FileList** list, CachedFile* file);

bool fileExists(FileCache* fileCache, const char* filename);

void createFile(FileCache* fileCache, const char* filename);

void freeFileCache(FileCache** fileCache);

#endif //SOL_PROJECT_FILECACHE_H
