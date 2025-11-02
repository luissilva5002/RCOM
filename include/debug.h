#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

static inline void errorLog(const char *func, const char *msg)
{
    fprintf(stderr, "[ERROR] %s: %s\n", func, msg);
}

#endif