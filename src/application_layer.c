// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include "serial_port.h"
#include "packet_helper.h"

#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>

#define DATA_BUFFER_SIZE 1021

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer connectionParameters;
    memset(&connectionParameters, 0, sizeof(connectionParameters));

    // Fill connection parameters
    strcpy(connectionParameters.serialPort, serialPort);
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;
    connectionParameters.role = (strcmp(role, "tx") == 0) ? LlTx : LlRx;

    // Open the data link layer connection
    printf("Opening connection on %s as %s...\n", serialPort, role);
    int status = llopen(connectionParameters);

    if (status < 0) {
        fprintf(stderr, "Error: Failed to establish link layer connection.\n");
        exit(1);
    }

    printf("Link layer connection established successfully!\n");

    switch (connectionParameters.role) {
        case LlTx: {
            // -------------------
            // TRANSMITTER
            // -------------------
            FILE *file = fopen(filename, "rb");
            if (!file) {
                perror("[App] Error opening file");
                llclose(connectionParameters);
                exit(1);
            }

            // Get file size
            fseek(file, 0, SEEK_END);
            uint32_t fileSize = ftell(file);
            fseek(file, 0, SEEK_SET);

            // Send START control packet
            sendControlPacket(CF_START, fileSize, filename);

            // Send DATA packets
            uint8_t buffer[DATA_BUFFER_SIZE];
            size_t bytesRead;
            while ((bytesRead = fread(buffer, 1, DATA_BUFFER_SIZE, file)) > 0) {
                sendDataPacket(buffer, (uint16_t)bytesRead);
            }

            // Send END control packet
            sendControlPacket(CF_END, fileSize, filename);

            fclose(file);
            printf("[App] File transmission complete!\n");
            break;
        }

        case LlRx: {
            // -------------------
            // RECEIVER
            // -------------------
            uint32_t fileSize = 0;
            char receivedFilename[MAX_FILENAME_SIZE + 1];
            uint8_t controlType;
            uint8_t dataBuffer[DATA_BUFFER_SIZE];

            // Wait for START control packet
            while (1) {
                if (receivePacket(&controlType, dataBuffer, &fileSize, receivedFilename) == 0 &&
                    controlType == CF_START)
                    break;
            }

            FILE *out = fopen(receivedFilename, "wb");
            if (!out) {
                perror("[App] Error creating output file");
                llclose(connectionParameters);
                exit(1);
            }

            uint32_t bytesReceived = 0;
            while (1) {
                int len = receivePacket(&controlType, dataBuffer, &fileSize, receivedFilename);
                if (len < 0) continue;

                if (controlType == CF_END) break;
                if (controlType == CF_DATA) {
                    fwrite(dataBuffer, 1, len, out);
                    bytesReceived += len;
                }
            }

            fclose(out);
            printf("[App] File received successfully: %u bytes written to %s\n",
                   bytesReceived, receivedFilename);
            break;
        }

        default:
            fprintf(stderr, "[App] Unknown role!\n");
            llclose(connectionParameters);
            exit(1);
    }

    // Close connection
    printf("Closing connection...\n");
    llclose(connectionParameters);
    printf("Connection closed.\n");
}
