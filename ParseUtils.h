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

ArgsList* readConfigFile(const char* filename);

void freeArgsListNode(ArgsList* node);

ArgsList* getNodeForKey(ArgsList* list, const char* key);

long getLongValue(ArgsList* list, const char* key);

char* getStringValue(ArgsList* list, const char* key);

#endif //SOL_PROJECT_PARSEUTILS_H