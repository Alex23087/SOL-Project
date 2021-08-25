#include <ctype.h>
#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include "../include/defines.h"
#include "../include/ParseUtils.h"

#define MAX_LINE_BUFFER 100



static ArgsList* argsListNodeFromString(const char* input){
    ArgsList* out = initArgsListNode();

    size_t i = 0;
    while(isalpha(input[i])){
        i++;
    }

    char* name = malloc(sizeof(char) * (i+1));
    strncpy(name, input, i);
    name[i] = '\0';

    while(isspace(input[i])){
        i++;
    }
    if(input[i] != '='){
        //TODO: Handle error
        fprintf(stderr, "Invalid format in config file: \"%s\"", input);
        exit(1);
    }
    i++;
    while(isspace(input[i])){
        i++;
    }

    if(input[i] == '\"'){
        out->type = String;
        i++;
    }else{
        out->type = Long;
    }

    size_t start = i;
    void* value;
    switch(out->type) {
        case String:
            while(input[i] != '\"'){
                if(input[i] == '\n' || input[i] == EOF){
                    //TODO: Handle error
                    fprintf(stderr, "Invalid format in input: \"%s\"\n", input);
                    exit(1);
                }
                i++;
            }
            size_t length = i - start;
            value = malloc(sizeof(char) * (length + 1));
            strncpy(value, &input[start], length);
            ((char*)value)[length] = '\0';
            break;
        case Long:
            errno = 0;
            value = malloc(sizeof(long));
            *(long*)value = strtol(&input[i], NULL, 10);
            if(errno != 0){
                //TODO: Handle error
                perror("Invalid long format in input\n");
            }
            break;
    }

    out->name = name;
    out->data = value;

    return out;
}

static bool strIsWhitespace(const char* in){
	size_t len = strlen(in);
	for(size_t i = 0; i < len; i++){
		if(!isspace(in[i])){
			return false;
		}
	}
	return true;
}



void freeArgsListNode(ArgsList* node){
    if(node->next != NULL){
        freeArgsListNode(node->next);
        node->next = NULL;
    }
    free(node->data);
    free(node->name);
    free(node);
    node = NULL;
}

long getLongValue(ArgsList* list, const char* key){
    ArgsList* node = getNodeForKey(list, key);
    if(node == NULL || node->type != Long || node->data == NULL){
        //TODO: Handle error
        fprintf(stderr, "Error while getting long value for key: %s\n", key);
        exit(1);
    }
    return *(long*)node->data;
}

ArgsList* getNodeForKey(ArgsList* list, const char* key){
    ArgsList* current = list;
    while(current != NULL && current->name != NULL && strcmp(current->name, key) != 0){
        current = current->next;
    }
    return current;
}

char* getStringValue(ArgsList* list, const char* key){
    ArgsList* node = getNodeForKey(list, key);
    if(node == NULL || node->type != String || node->data == NULL){
        //TODO: Handle error
        fprintf(stderr, "Error while getting string value for key: %s\n", key);
        return NULL;
    }

    //Returns a copy of the string, so that the list node can be safely deallocated without destroying the string value
    char* out = malloc(sizeof(char) * strlen((char*)(node->data)) + 1);
    strcpy(out, (char*)(node->data));
    return out;
}

ArgsList* initArgsListNode(){
    ArgsList* out = malloc(sizeof(ArgsList));
    out->type = Long;
    out->data = NULL;
    out->name = NULL;
    out->next = NULL;
    return out;
}

ArgsList* readConfigFile(const char* filename){
    ArgsList* head = NULL;

    char* buffer = malloc(MAX_LINE_BUFFER);

    FILE* file = fopen(filename, "rb");
    if(file){
        while(true){
            if(fgets(buffer, MAX_LINE_BUFFER, file) == NULL){
                if(feof(file)) {
                	break;
                }else{
                	fprintf(stderr, "Error while reading from file \"%s\"\n", filename);
                	//TODO: Handle error
                }
            }else{
            	if(strIsWhitespace(buffer)){
            		continue;
            	}
                if(head == NULL){
                    head = argsListNodeFromString(buffer);
                }else{
                    ArgsList* newHead = argsListNodeFromString(buffer);
                    newHead->next = head;
                    head = newHead;
                }
            }
        }
        fclose(file);
    }else{
        //TODO: Handle error
	    perror("File couldn't be read");
    }

    free(buffer);
    return head;
}
