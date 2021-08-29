#ifndef SOL_PROJECT_DEFINES_H
#define SOL_PROJECT_DEFINES_H

#define bool short
#define false 0
#define true 1

#define printIfVerbose(...) \
if(verbose){\
printf(__VA_ARGS__);\
}

#endif //SOL_PROJECT_DEFINES_H