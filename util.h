/* util.h */

#ifndef _UTIL_H
#define _UTIL_H

#include <limits.h>

// BTW why am I not using unsigned ints (uint32_t)
#define INF INT_MAX
#define NONE -1

void die(const char* /* message */ );

char *btimetext(int t, char *buf, int buflen);

char *timetext(int t);

#endif // _UTIL_H