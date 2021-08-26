#ifndef SOL_PROJECT_FILECACHE_H
#define SOL_PROJECT_FILECACHE_H

#include <malloc.h>
#include <pthread.h>
#include <string.h>

#include "defines.h"
#include "FileCachingProtocol.h"

#define MAX_FILENAME_SIZE FCP_MESSAGE_LENGTH - 5



typedef struct CachedFile{
	char* filename;
	char* contents;
	size_t size;
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

typedef struct FileCacheStatistics{
	unsigned int fileNumber;
	unsigned long size;
	
} FileCacheStatistics;

typedef struct FileCache{
	FileCacheStatistics max;
	FileCacheStatistics current;
	FileCacheStatistics maxReached;
	FileList* files;
	CacheAlgorithm cacheAlgorithm;
	unsigned int filesEvicted;
} FileCache;



bool canFitNewData(FileCache* fileCache, const char* filename, size_t dataSize, bool append);

CachedFile* createFile(FileCache* fileCache, const char* filename);

bool fileExists(FileCache* fileCache, const char* filename);

void freeCachedFile(CachedFile* file);

void freeFileCache(FileCache** fileCache);

CachedFile* getFile(FileCache* fileCache, const char* filename);

CachedFile* getFileLockedByClient(FileCache* fileCache, int clientFd);

size_t getFileSize(CachedFile* file);

const char* getFileToEvict(FileCache* fileCache, const char* fileToExclude);

FileCache* initFileCache(unsigned int maxFiles, unsigned long maxSize);

char* readCachedFile(CachedFile* file, char** buffer, size_t* size);

void removeFileFromCache(FileCache* fileCache, const char* filename);

size_t storeFile(FileCache* fileCache, CachedFile* file, char* contents, size_t size);

#endif //SOL_PROJECT_FILECACHE_H
