#ifndef SOL_PROJECT_PARSEUTILS_H
#define SOL_PROJECT_PARSEUTILS_H

#include <stdio.h>



typedef enum ArgsType{
    String,
    Long
} ArgsType;

typedef struct ArgsList{
    ArgsType type;
    char* name;
    void* data;
    struct ArgsList* next;
} ArgsList;



void freeArgsListNode(ArgsList* node);

long getLongValue(ArgsList* list, const char* key);

ArgsList* getNodeForKey(ArgsList* list, const char* key);

char* getStringValue(ArgsList* list, const char* key);

ArgsList* initArgsListNode();

ArgsList* readConfigFile(const char* filename);

#endif //SOL_PROJECT_PARSEUTILS_H