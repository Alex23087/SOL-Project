#include <sys/time.h>
#include "../include/FileCache.h"
#include "../include/miniz.h"
#include "../include/TimespecUtils.h"


//Adds a file to the head of a list of files
static void addFile(FileList** list, CachedFile* file){
    FileList* newNode = malloc(sizeof(FileList));
    newNode->file = file;
    newNode->next = *list;
    *list = newNode;
}

//Recursively frees all the nodes from a file list
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
    if(out == NULL){
        return NULL;
    }
    out->lock = malloc(sizeof(pthread_mutex_t));
    if(out->lock == NULL){
        free(out);
        return NULL;
    }
    if(pthread_mutex_init(out->lock, NULL)){
        perror("Error while initializing lock for file");
        free(out->lock);
        free(out);
        return NULL;
    }

    size_t len = strnlen(filename, MAX_FILENAME_SIZE);
    out->filename = malloc(len + 1);
    strncpy(out->filename, filename, len);
    out->filename[len] = '\0';
    out->size = 0;
    out->contents = NULL;
    out->lockedBy = -1;
    out->compression = Uncompressed;
    out->lastAccessed = getTimeStamp();
    return out;
}

static void removeFileFromList(FileCache* fileCache, FileList** fileList, const char* filename){
    FileList* list = *fileList;
    if(list == NULL){
        return;
    }
    if(strcmp(list->file->filename, filename) == 0){
        //The file to remove is the head of the list
        *fileList = list->next;
        fileCache->current.size -= getFileSize(list->file);
        fileCache->current.fileNumber--;
        //Free the node by setting next to null and using the function to free an entire list
        list->next = NULL;
        freeFileList(&list);
        return;
    }
    //The file to remove is not the head of the list
    while(list->next != NULL){
        if(strcmp(list->next->file->filename, filename) == 0){
            FileList* tmp = list->next->next;
            fileCache->current.size -= getFileSize(list->next->file);
            fileCache->current.fileNumber--;
            //Free the node by setting next to null and using the function to free an entire list
            list->next->next = NULL;
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

//Initializes a new CachedFile and adds it to the FileCache's FileList
CachedFile* createFile(FileCache* fileCache, const char* filename){
    CachedFile* newFile = initCachedFile(filename);
    if(newFile == NULL){
        return NULL;
    }
    fileCache->current.fileNumber++;
    if(fileCache->current.fileNumber > fileCache->maxReached.fileNumber){
        fileCache->maxReached.fileNumber = fileCache->current.fileNumber;
    }
    addFile(&(fileCache->files), newFile);
    return newFile;
}

bool fileExists(FileCache* fileCache, const char* filename){
    return getFile(fileCache, filename) != NULL;
}

void freeCachedFile(CachedFile* file){
    if(file->contents != NULL) {
        free(file->contents);
        file->contents = NULL;
    }
    if(file->filename != NULL) {
        free(file->filename);
        file->filename = NULL;
    }
    free(file->lock);
    file->lock = NULL;
    free(file);
    file = NULL;
}

void freeFileCache(FileCache** fileCache){
    freeFileList(&((*fileCache)->files));
    free(*fileCache);
    *fileCache = NULL;
}

size_t getUncompressedSize(CachedFile* file){
    return file->compression == Uncompressed ? file->size : file->uncompressedSize;
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

//Returns the first file found that is locked by the client, or NULL if none is found.
//Used when a client disconnects to unlock all files locked by it
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

//Returns the filename of the next file that has to be evicted according to the caching algorithm selected,
//or NULL if no file can be evicted according to the policies implemented.
//A file with filename equal to the argument passed is guaranteed not to be selected by the algorithm.
const char* getFileToEvict(FileCache* fileCache, const char* fileToExclude){
    FileList* current = fileCache->files;
    switch (fileCache->cacheAlgorithm) {
        default:
        case FIFO:{
            if(current == NULL){
                return NULL;
            }
            if(strcmp(current->file->filename, fileToExclude) == 0){
                current = current->next;
                if(current == NULL){
                    return NULL;
                }
            }
            while(current->next != NULL){
                //Skip locked files and the file specified as parameter
                if(current->next->file->lockedBy != -1 || strcmp(current->next->file->filename, fileToExclude) == 0){
                    if(current->next->next == NULL){
                        break;
                    }else{
                        current = current->next->next;
                    }
                }else{
                    current = current->next;
                }
            }

            //If all the files are locked, then current->file will point to a locked file. We don't want to remove it
            if(current->file->lockedBy == -1){
                fileCache->filesEvicted++;
                return current->file->filename;
            }else{
                return NULL;
            }
        }
        case LRU:{
            char* filename = NULL;
            uint64_t currentMinTimestamp = UINT64_MAX;
            while(current != NULL){
                //Exclude files that are locked by a client, that have filename equals to filetoexclude, or that have a
                //last accessed timestamp more recent that the one we have currently selected
                if(current->file->lockedBy == -1 && strncmp(current->file->filename, fileToExclude, MAX_FILENAME_SIZE) != 0 && current->file->lastAccessed < currentMinTimestamp){
                    currentMinTimestamp = current->file->lastAccessed;
                    filename = current->file->filename;
                }
                current = current->next;
            }
            if(filename != NULL){
                fileCache->filesEvicted++;
            }
            return filename;
            break;
        }
    }
}

FileCache* initFileCache(unsigned int maxFiles, unsigned long maxSize, CompressionAlgorithm compressionAlgorithm, CacheAlgorithm cacheAlgorithm){
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
    out->cacheAlgorithm = cacheAlgorithm;
	return out;
}

//Reads a cached file. If the file is not compressed, the contents of the buffer will be returned,
//otherwise the file will be decompressed and then sent. If there's an error while decompressing the file, the buffer will be sent as-is.
char* readCachedFile(CachedFile* file, char** buffer, size_t* size){
    file->lastAccessed = getTimeStamp();
	switch(file->compression){
		case Miniz:{
            //Attempt decompression
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
		case Uncompressed:
		default:{
            //The file is not compressed, send the contents of the buffer
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

//Stored a buffer in a CachedFile. If the FileCache has been configured to compress the files,
//then there will be an attempt at compressing the file. If the compression fails, or if the size of the compressed file
//is equal or higher than that of the original file, the file will be stored non-compressed, for space and performance reasons.
size_t storeFile(FileCache* fileCache, CachedFile* file, char* contents, size_t size){
    fileCache->current.size -= getFileSize(file);
	file->lastAccessed = getTimeStamp();

	switch(fileCache->compressionAlgorithm) {
		case Miniz:{
			//Miniz (zlib) compression
			unsigned long compressedSize = compressBound(size);
			char* compressedBuffer = malloc(compressedSize);
			int compressionStatus = compress((unsigned char*)compressedBuffer, &compressedSize, (const unsigned char*)contents, size);
			if(compressionStatus != Z_OK || compressedSize >= size){
				//Error while compressing, or the compressed file is bigger or the same size
				//as the non-compressed one. Save the file as non-compressed
				free(compressedBuffer);
				fileCache->current.size += size;
				if(fileCache->current.size > fileCache->maxReached.size){
					fileCache->maxReached.size = fileCache->current.size;
				}
				file->size = size;
				file->contents = contents;
				file->compression = Uncompressed;
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
		case Uncompressed:
		default:{
			//Compression not enabled
			fileCache->current.size += size;
			if(fileCache->current.size > fileCache->maxReached.size){
				fileCache->maxReached.size = fileCache->current.size;
			}
			file->size = size;
			file->contents = contents;
			file->compression = Uncompressed;
			return size;
		}
	}
}
