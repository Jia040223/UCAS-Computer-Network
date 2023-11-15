#ifndef __UTIL_H__
#define __UTIL_H__

#include <sys/time.h>
#include <stdbool.h>
#include <stdint.h>

long get_interval(struct timeval tv_start,struct timeval tv_end);

#endif
