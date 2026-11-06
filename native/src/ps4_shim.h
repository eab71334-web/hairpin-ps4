#pragma once
#include <stdlib.h>
#include <math.h>
// OpenOrbis libc++ calls nanosleep() but its time.h does not declare it.
struct timespec;
extern "C" int nanosleep(const struct timespec*, struct timespec*);
