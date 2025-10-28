// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include "serial_port.h"

#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>



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

    // Close connection
    printf("Closing connection...\n");
    llclose(connectionParameters);
    printf("Connection closed.\n");
}
