#include "../include/FileCache.h"
#include "../include/miniz.h"


static void addFile(FileList** list, CachedFile* file){
    FileList* newNode = malloc(sizeof(FileList));
    newNode->file = file;
    newNode->next = *list;
    *list = newNode;
}

static void freeFileList(FileList** fileList){
    if(*fileList != NULL){
        freeFileList(&((*fileList)->next));
        freeCachedFile((*fileList)->file);
        free(*fileList);
        *fileList = NULL;
    }
}

static CachedFile* initCachedFile(const char* filename){
    CachedFile* out = malloc(sizeof(CachedFile));
    out->lock = malloc(sizeof(pthread_mutex_t));
    if(pthread_mutex_init(out->lock, NULL)){
        perror("Error while initializing lock for file");
        free(out->lock);
        free(out);
        return NULL;
    }

    size_t len = strlen(filename);
    out->filename = malloc(len + 1);
    strncpy(out->filename, filename, len);
    out->filename[len] = '\0';
    out->size = 0;
    out->contents = NULL;
    out->lockedBy = -1;
    return out;
}

static void removeFileFromList(FileCache* fileCache, FileList** fileList, const char* filename){
    FileList* list = *fileList;
    if(list == NULL){
        return;
    }
    if(strcmp(list->file->filename, filename) == 0){
        *fileList = list->next;
        list->next = NULL;
        fileCache->current.size -= getFileSize(list->file);
        fileCache->current.fileNumber--;
        freeFileList(&list);
        return;
    }
    while(list->next != NULL){
        if(strcmp(list->next->file->filename, filename) == 0){
            FileList* tmp = list->next->next;
            list->next->next = NULL;
            fileCache->current.size -= getFileSize(list->next->file);
            fileCache->current.fileNumber--;
            freeFileList(&(list->next));
            list->next = tmp;
            return;
        }
        list = list->next;
    }
}



bool canFitNewData(FileCache* fileCache, const char* filename, size_t dataSize, bool append){
	return (fileCache->current.size) - (append ? 0 : getFileSize(getFile(fileCache, filename))) + dataSize <= fileCache->max.size;
}

bool canFitNewFile(FileCache* fileCache){
	return fileCache->current.fileNumber < fileCache->max.fileNumber;
}

CachedFile* createFile(FileCache* fileCache, const char* filename){
    fileCache->current.fileNumber++;
    if(fileCache->current.fileNumber > fileCache->maxReached.fileNumber){
        fileCache->maxReached.fileNumber = fileCache->current.fileNumber;
    }
    CachedFile* newFile = initCachedFile(filename);
    addFile(&(fileCache->files), newFile);
    return newFile;
}

bool fileExists(FileCache* fileCache, const char* filename){
    return getFile(fileCache, filename) != NULL;
}

void freeCachedFile(CachedFile* file){
    free(file->contents);
    free(file->filename);
    free(file->lock);
    free(file);
}

void freeFileCache(FileCache** fileCache){
    freeFileList(&((*fileCache)->files));
    free(*fileCache);
    *fileCache = NULL;
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

size_t getFileSize(CachedFile* file){
    return file->size;
}

const char* getFileToEvict(FileCache* fileCache, const char* fileToExclude){
    FileList* current = fileCache->files;
    if(current == NULL){
        return NULL;
    }
    if(strcmp(current->file->filename, fileToExclude) == 0){
        current = current->next;
        if(current == NULL){
            return NULL;
        }
    }
    fileCache->filesEvicted++;
    while(current->next != NULL){
        if(strcmp(current->next->file->filename, fileToExclude) == 0){
            if(current->next->next == NULL){
                break;
            }else{
                current = current->next->next;
            }
        }else{
            current = current->next;
        }
    }
    return current->file->filename;
}

FileCache* initFileCache(unsigned int maxFiles, unsigned long maxSize, CompressionAlgorithm compressionAlgorithm){
	FileCache* out = malloc(sizeof(FileCache));
	out->max.fileNumber = maxFiles;
	out->max.size = maxSize;
	out->current.size = 0;
	out->current.fileNumber = 0;
	out->maxReached.size = 0;
	out->maxReached.fileNumber = 0;
	out->filesEvicted = 0;
	out->files = NULL;
	out->compressionAlgorithm = compressionAlgorithm;
	return out;
}

char* readCachedFile(CachedFile* file, char** buffer, size_t* size){
	switch(file->compression){
		case Miniz:{
			*size = file->uncompressedSize;
			*buffer = malloc(file->uncompressedSize);
			int decompressionResult = uncompress((unsigned char*)*buffer, size, (const unsigned char*)(file->contents), getFileSize(file));
			if(decompressionResult != Z_OK){
				//There has been an error, send the file buffer as is
				free(*buffer);
				*size = getFileSize(file);
				*buffer = malloc(*size);
				memcpy(*buffer, file->contents, *size);
				return *buffer;
			}else{
				//Decompression successful
				return *buffer;
			}
		}
		case None:
		default:{
			*size = getFileSize(file);
			*buffer = malloc(*size);
			memcpy(*buffer, file->contents, *size);
			return *buffer;
		}
	}
}

void removeFileFromCache(FileCache* fileCache, const char* filename){
	removeFileFromList(fileCache, &(fileCache->files), filename);
}

size_t storeFile(FileCache* fileCache, CachedFile* file, char* contents, size_t size){
    fileCache->current.size -= getFileSize(file);
	
	switch(fileCache->compressionAlgorithm) {
		case Miniz:{
			//Miniz (zlib) compression
			unsigned long compressedSize = compressBound(size);
			char* compressedBuffer = malloc(compressedSize);
			int compressionStatus = compress((unsigned char*)compressedBuffer, &compressedSize, (const unsigned char*)contents, size);
			if(compressionStatus != Z_OK || compressedSize > size){
				//Error while compressing, or the compressed file is bigger
				//than the non compressed one. Save the file as uncompressed
				free(compressedBuffer);
				fileCache->current.size += size;
				if(fileCache->current.size > fileCache->maxReached.size){
					fileCache->maxReached.size = fileCache->current.size;
				}
				file->size = size;
				file->contents = contents;
				file->compression = None;
				return size;
			}else{
				//Compression successful
				free(contents);
				fileCache->current.size += compressedSize;
				if(fileCache->current.size > fileCache->maxReached.size){
					fileCache->maxReached.size = fileCache->current.size;
				}
				file->size = compressedSize;
				file->uncompressedSize = size;
				file->contents = compressedBuffer;
				file->compression = Miniz;
				return compressedSize;
			}
		}
		case None:
		default:{
			//Compression not enabled
			fileCache->current.size += size;
			if(fileCache->current.size > fileCache->maxReached.size){
				fileCache->maxReached.size = fileCache->current.size;
			}
			file->size = size;
			file->contents = contents;
			file->compression = None;
			return size;
		}
	}
}
