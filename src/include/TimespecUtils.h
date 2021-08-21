#ifndef SOL_PROJECT_TIMESPECUTILS_H
#define SOL_PROJECT_TIMESPECUTILS_H

#include <bits/types/struct_timespec.h>

short compareTimes(const struct timespec lval, const struct timespec rval);

struct timespec subtractTimes(const struct timespec lval, const struct timespec rval);

struct timespec addTimes(const struct timespec lval, const struct timespec rval);

struct timespec doubleToTimespec(double time);

double timespecToDouble(struct timespec time);

#endif //SOL_PROJECT_TIMESPECUTILS_H
