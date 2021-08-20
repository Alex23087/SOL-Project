#include "FileCache.h"

CachedFile* initCachedFile(const char* filename){
	CachedFile* out = malloc(sizeof(CachedFile));
	out->lock = malloc(sizeof(pthread_mutex_t));
	if(pthread_mutex_init(out->lock, NULL)){
		perror("Error while initializing lock for file");
		free(out->lock);
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
	return getFile(fileCache, filename) != NULL;
}

CachedFile* createFile(FileCache* fileCache, const char* filename){
	CachedFile* newFile = initCachedFile(filename);
	addFile(&(fileCache->files), newFile);
	return newFile;
}

void freeFileCache(FileCache** fileCache){
	//TODO: Implement
}

CachedFile* getFile(FileCache* fileCache, const char* filename){
	FileList* current = fileCache->files;
	while(current != NULL){
		if(strncmp(current->file->filename, filename, MAX_FILENAME_SIZE) == 0){
			return current->file;
		}
		current = current->next;
	}
	return NULL;
}

CachedFile* getFileLockedByClient(FileCache* fileCache, int clientFd){
	FileList* current = fileCache->files;
	while(current != NULL){
		if(current->file->lockedBy == clientFd){
			return current->file;
		}
		current = current->next;
	}
	return NULL;
}

size_t storeFile(CachedFile* file, char* contents){
	//TODO: Implement compression
	file->contents = contents;
	return sizeof(contents);
}