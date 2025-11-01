#ifndef PACKET_HELPER_H
#define PACKET_HELPER_H

#include <stdint.h>

#define CF_START 0x01
#define CF_DATA  0x02
#define CF_END   0x03

#define TLV_FILESIZE_T 0x00
#define TLV_FILENAME_T 0x01

#define MAX_PACKET_SIZE 1024
#define MAX_FILENAME_SIZE 255

// Function declarations
int sendControlPacket(uint8_t controlType, uint32_t fileSize, const char *filename);
int sendDataPacket(const uint8_t *data, uint16_t dataSize);
int receivePacket(uint8_t *controlType, uint8_t *dataBuffer, uint32_t *fileSize, char *filename);

#endif
