#include "statistics.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "debug.h"

Statistics statistics;

////////////////////////////////////////////////
// INIT STATISTICS
////////////////////////////////////////////////

void initStatistics(const LinkLayer *connectionParameters)
{
    statistics.role = connectionParameters->role;
    statistics.baudrate = connectionParameters->baudRate;

    statistics.dataBytes = 0;
    statistics.totalBytes = 0;
    statistics.totalFrames = 0;
    statistics.badFrames = 0;
    statistics.totalRej = 0;
    statistics.totalTimeouts = 0;
}

////////////////////////////////////////////////
// PRINT STATISTICS
////////////////////////////////////////////////

void printStatistics()
{
    double totalTime = (statistics.end.tv_sec - statistics.start.tv_sec) * 1e9 +
                       (statistics.end.tv_nsec - statistics.start.tv_nsec);

    double measuredBaudrate = statistics.dataBytes * 8 * 1e9 / totalTime;
    double efficiency = measuredBaudrate / statistics.baudrate;

    printf("\n************ STATISTICS ************\n\n");
    printf("Communication time: %.3f s\n", totalTime / 1e9);
    printf("Serial port baudrate: %d\n", statistics.baudrate);
    printf("Measured baudrate: %.2f bit/s\n", measuredBaudrate);
    printf("Efficiency: %.3f\n\n", efficiency);

    if (statistics.role == LlTx) {
        printf("Total bytes transmitted: %d\n", statistics.totalBytes);
        printf("Data bytes transmitted: %d\n", statistics.dataBytes);
        printf("Total frames transmitted: %d\n", statistics.totalFrames);
        printf("Total REJ received: %d\n", statistics.totalRej);
        printf("Total timeouts: %d\n", statistics.totalTimeouts);
        printf("Frame error ratio: %.3f\n",
               (double)(statistics.totalRej + statistics.totalTimeouts) / statistics.totalFrames);
    } 
    else if (statistics.role == LlRx) {
        printf("Total bytes received: %d\n", statistics.totalBytes);
        printf("Data bytes received: %d\n", statistics.dataBytes);
        printf("Total frames received: %d\n", statistics.totalFrames);
        printf("Good frames: %d\n", statistics.totalFrames - statistics.badFrames);
        printf("Bad frames: %d\n", statistics.badFrames);
        printf("Frame error ratio: %.3f\n",
               (double)statistics.badFrames / statistics.totalFrames);
    }

    printf("\n************************************\n");
}

////////////////////////////////////////////////
// STORE STATISTICS TO CSV
////////////////////////////////////////////////

int storeStatistics()
{
    FILE *file;
    double totalTime = (statistics.end.tv_sec - statistics.start.tv_sec) * 1e9 +
                       (statistics.end.tv_nsec - statistics.start.tv_nsec);

    double measuredBaudrate = statistics.dataBytes * 8 * 1e9 / totalTime;
    double efficiency = measuredBaudrate / statistics.baudrate;

    const char *filename = (statistics.role == LlTx) ? "stats-tx.csv" : "stats-rx.csv";
    int fileExists = (access(filename, F_OK) == 0);

    file = fopen(filename, fileExists ? "a" : "w");
    if (file == NULL) {
        errorLog(__func__, "Couldn't open statistics file");
        return -1;
    }

    if (!fileExists) {
        if (statistics.role == LlTx)
            fprintf(file, "Baudrate,Payload,CommTime(s),MeasuredBaudrate,Efficiency,TotalBytes,DataBytes,Frames,REJ,Timeouts,FrameErrorRatio\n");
        else
            fprintf(file, "Baudrate,Payload,CommTime(s),MeasuredBaudrate,Efficiency,TotalBytes,DataBytes,Frames,Good,Bad,FrameErrorRatio\n");
    }

    if (statistics.role == LlTx) {
        fprintf(file, "%d,%d,%.3f,%.2f,%.3f,%d,%d,%d,%d,%d,%.3f\n",
                statistics.baudrate,
                MAX_PACKET_SIZE,
                totalTime / 1e9,
                measuredBaudrate,
                efficiency,
                statistics.totalBytes,
                statistics.dataBytes,
                statistics.totalFrames,
                statistics.totalRej,
                statistics.totalTimeouts,
                (double)(statistics.totalRej + statistics.totalTimeouts) / statistics.totalFrames);
    } 
    else {
        fprintf(file, "%d,%d,%.3f,%.2f,%.3f,%d,%d,%d,%d,%d,%.3f\n",
                statistics.baudrate,
                MAX_PACKET_SIZE,
                totalTime / 1e9,
                measuredBaudrate,
                efficiency,
                statistics.totalBytes,
                statistics.dataBytes,
                statistics.totalFrames,
                statistics.totalFrames - statistics.badFrames,
                statistics.badFrames,
                (double)statistics.badFrames / statistics.totalFrames);
    }

    fclose(file);
    return 0;
}
