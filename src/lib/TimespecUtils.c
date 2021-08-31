#include <stdint.h>
#include <sys/time.h>
#include <time.h>

#include "../include/TimespecUtils.h"



struct timespec addTimes(const struct timespec lval, const struct timespec rval){
    struct timespec out;
    out.tv_sec = lval.tv_sec + rval.tv_sec;
    out.tv_nsec = lval.tv_nsec + rval.tv_nsec;
    return out;
}

short compareTimes(const struct timespec lval, const struct timespec rval){
	return (lval.tv_sec > rval.tv_sec) ? 1 : ((lval.tv_sec == rval.tv_sec) ? ((lval.tv_nsec == rval.tv_nsec) ? 0 : ((lval.tv_nsec > rval.tv_nsec) ? 1 : -1)) : -1); // NOLINT(cppcoreguidelines-narrowing-conversions)
}

struct timespec doubleToTimespec(double time){
    struct timespec out;
    out.tv_sec = (long) time;
    out.tv_nsec = (time - out.tv_sec) * 1e9L;
    return out;
}

uint64_t getTimeStamp(){
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return tv.tv_sec*(uint64_t)1000000+tv.tv_usec;
}

struct timespec subtractTimes(const struct timespec lval, const struct timespec rval){
	struct timespec out;
	out.tv_sec = lval.tv_sec - rval.tv_sec;
	out.tv_nsec = lval.tv_nsec - rval.tv_nsec;
	return out;
}

double timespecToDouble(struct timespec time){
    return time.tv_sec + (time.tv_nsec * 1e-9);
}
