#ifndef SOL_PROJECT_TIMESPECUTILS_H
#define SOL_PROJECT_TIMESPECUTILS_H

#include <time.h>



struct timespec addTimes(const struct timespec lval, const struct timespec rval);

short compareTimes(const struct timespec lval, const struct timespec rval);

struct timespec doubleToTimespec(double time);

uint64_t getTimeStamp();

struct timespec subtractTimes(const struct timespec lval, const struct timespec rval);

double timespecToDouble(struct timespec time);

#endif //SOL_PROJECT_TIMESPECUTILS_H
