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
    // Prepare the LinkLayer connection parameters struct
    LinkLayer connectionParameters;
    memset(&connectionParameters, 0, sizeof(LinkLayer));

    // Copy serial port path
    strncpy(connectionParameters.serialPort, serialPort, sizeof(connectionParameters.serialPort) - 1);

    // Convert role string ("tx" or "rx") into LinkLayerRole enum
    if (strcmp(role, "tx") == 0)
        connectionParameters.role = LlTx;
    else if (strcmp(role, "rx") == 0)
        connectionParameters.role = LlRx;
    else {
        fprintf(stderr, "❌ Invalid role '%s'. Use 'tx' or 'rx'.\n", role);
        exit(1);
    }

    // Set remaining parameters
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    printf("🔧 Opening link-layer connection on port %s as %s...\n", connectionParameters.serialPort, connectionParameters.role == LlTx ? "Transmitter" : "Receiver");

    // Call llopen() from link layer
    int fd = llopen(connectionParameters);
    if (fd < 0) {
        fprintf(stderr, "❌ Failed to open link-layer connection.\n");
        exit(1);
    }

    printf("✅ Link-layer connection established successfully!\n");

    // Close the link when done
    llclose();
    printf("🔒 Connection closed.\n");
}



/*

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    
    if (openSerialPort(serialPort, baudRate) < 0)
    {
        perror("openSerialPort");
        exit(-1);
    }

    printf("Serial port %s opened as %s\n", serialPort, role);

    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = alarmHandler;
    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        closeSerialPort();
        exit(1);
    }

    // -----------------------------
    // TRANSMITTER
    // -----------------------------
    if (strcmp(role, "tx") == 0)
    {
        printf("Transmitter: sending SET frame...\n");
        alarmCount = 0;
        UA_received = FALSE;

        while (alarmCount < nTries && UA_received == FALSE)
        {
            int nbytes = writeBytesSerialPort(BUFF_SET, BUF_SIZE);
            printf("SET frame sent: %d bytes\n", nbytes);

            TIMEOUT = FALSE;
            alarm(timeout);

            unsigned char byte;
            while (TIMEOUT == FALSE && UA_received == FALSE)
            {
                if (txstateMachine())
                {
                    printf("UA frame received. Connection established!\n");
                    UA_received = TRUE;
                    alarm(0);
                }
            }

            if (!UA_received)
                printf("No UA received, retrying...\n");
        }

        if (!UA_received)
            printf("Failed to receive UA after %d attempts.\n", alarmCount);
    }

    // -----------------------------
    // RECEIVER
    // -----------------------------
    else if (strcmp(role, "rx") == 0)
    {
        printf("Receiver: waiting for SET frame...\n");
        unsigned char byte;
        int connected = FALSE;

        while (!connected)
        {
            if (rxstateMachine(C1))
            {
                printf("SET frame received. Sending UA...\n");
                writeBytesSerialPort(BUFF_UA, BUF_SIZE);
                printf("UA sent. Connection established!\n");
                connected = TRUE;
            }
        }
    }
    else
    {
        printf("Invalid role. Use 'tx' or 'rx'.\n");
    }

    alarm(0);
    closeSerialPort();
    printf("Serial port %s closed\n", serialPort);
    */
