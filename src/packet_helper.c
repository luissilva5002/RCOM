#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "link_layer.h"   // where llwrite and llread are declared
#include "packet_helper.h"

// ==========================================================
//  SEND CONTROL PACKET (START or END)
// ==========================================================
int sendControlPacket(uint8_t controlType, uint32_t fileSize, const char *filename)
{
    uint8_t packet[MAX_PACKET_SIZE];
    int pos = 0;

    packet[pos++] = controlType;  // C = 1 (start) or 3 (end)

    // ---- TLV: File Size ----
    packet[pos++] = TLV_FILESIZE_T; // T
    packet[pos++] = 4;              // L
    packet[pos++] = (fileSize >> 24) & 0xFF;
    packet[pos++] = (fileSize >> 16) & 0xFF;
    packet[pos++] = (fileSize >> 8) & 0xFF;
    packet[pos++] = fileSize & 0xFF;

    // ---- TLV: File Name ----
    int nameLen = strlen(filename);
    if (nameLen > MAX_FILENAME_SIZE)
        return -1;

    packet[pos++] = TLV_FILENAME_T; // T
    packet[pos++] = nameLen;        // L
    memcpy(packet + pos, filename, nameLen);
    pos += nameLen;

    printf("[App] Sending CONTROL packet (type=%d, size=%u, name=%s)\n",
           controlType, fileSize, filename);

    int bytes = llwrite(packet, pos);
    return bytes;
}


// ==========================================================
//  SEND DATA PACKET
// ==========================================================
int sendDataPacket(const uint8_t *data, uint16_t dataSize)
{
    if (dataSize > MAX_PACKET_SIZE - 3) {
        fprintf(stderr, "[sendDataPacket] dataSize too large: %u\n", dataSize);
        return -1;
    }

    uint8_t packet[MAX_PACKET_SIZE];
    if (dataSize > 65535)
        return -1; // size too large

    int pos = 0;
    packet[pos++] = CF_DATA;        // C
    packet[pos++] = (dataSize >> 8) & 0xFF;  // L2
    packet[pos++] = dataSize & 0xFF;         // L1
    memcpy(packet + pos, data, dataSize);
    pos += dataSize;

    printf("[App] Sending DATA packet (%d bytes)\n", dataSize);

    int bytes = llwrite(packet, pos);
    return bytes;
}


// ==========================================================
//  RECEIVE PACKET (BLOCKING)
//  Handles both CONTROL and DATA types
// ==========================================================
int receivePacket(uint8_t *controlType,
                  uint8_t *dataBuffer,
                  uint32_t *fileSize,
                  char *filename)
{
    uint8_t packet[MAX_PACKET_SIZE];
    int len = llread(packet);

    if (len <= 0) {
        printf("[App] ❌ llread() failed\n");
        return -1;
    }

    *controlType = packet[0];

    if (*controlType == CF_DATA) {
        uint16_t dataLen = len; // llread returns payload size without BCC2
        memcpy(dataBuffer, packet, dataLen);
        printf("[App] Received DATA packet (%d bytes)\n", dataLen);
        return dataLen;
    }

    else if (*controlType == CF_START || *controlType == CF_END) {
        // Control packet (parse TLV)
        int pos = 1;
        while (pos < len) {
            uint8_t T = packet[pos++];
            uint8_t L = packet[pos++];
            if (T == TLV_FILESIZE_T && L == 4) {
                *fileSize = (packet[pos] << 24) |
                            (packet[pos + 1] << 16) |
                            (packet[pos + 2] << 8) |
                            packet[pos + 3];
                pos += 4;
            } else if (T == TLV_FILENAME_T) {
                memcpy(filename, packet + pos, L);
                filename[L] = '\0';
                pos += L;
            } else {
                pos += L; // skip unknown
            }
        }

        printf("[App] Received CONTROL packet (type=%d, size=%u, name=%s)\n",
               *controlType, *fileSize, filename);
        return 0;
    }

    printf("[App] ⚠️ Unknown packet type: 0x%02X\n", *controlType);
    return -1;
}