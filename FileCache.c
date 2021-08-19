#include "FileCache.h"

CachedFile* initCachedFile(const char* filename){
	CachedFile* out = malloc(sizeof(CachedFile));
	if(pthread_mutex_init(out->lock, NULL)){
		perror("Error while initializing lock for file");
		free(out);
		return NULL;
	}
	int len = strlen(filename);
	out->filename = malloc(len + 1);
	strncpy(out->filename, filename, len);
	out->filename[len] = '\0';
	out->contents = NULL;
	out->lockedBy = -1;
	return out;
}

FileCache* initFileCache(unsigned int maxFiles, unsigned long maxSize){
	FileCache* out = malloc(sizeof(FileCache));
	out->maxFiles = maxFiles;
	out->maxSize = maxSize;
	out->files = NULL;
	return out;
}

void addFile(FileList** list, CachedFile* file){
	FileList* newNode = malloc(sizeof(FileList));
	newNode->file = file;
	newNode->next = *list;
	*list = newNode;
}

bool fileExists(FileCache* fileCache, const char* filename){
	FileList* current = fileCache->files;
	while(current != NULL){
		if(strncmp(current->file->filename, filename, MAX_FILENAME_SIZE) == 0){
			return true;
		}
		current = current->next;
	}
	return false;
}

void createFile(FileCache* fileCache, const char* filename){
	addFile(&(fileCache->files), initCachedFile(filename));
}

void freeFileCache(FileCache** fileCache){
	//TODO: Implement
}