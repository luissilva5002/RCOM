#ifndef STATISTICS_H
#define STATISTICS_H

#include "link_layer.h"
#include <time.h>

typedef struct {
    LinkLayerRole role;
    int baudrate;

    int totalBytes;
    int dataBytes;
    int totalFrames;
    int badFrames;
    int totalRej;
    int totalTimeouts;

    struct timespec start;
    struct timespec end;
} Statistics;

extern Statistics statistics;

// Funções
void initStatistics(const LinkLayer *connectionParameters);
void printStatistics();
int storeStatistics();

#endif
