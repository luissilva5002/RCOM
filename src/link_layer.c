// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>

const unsigned char FLAG = 0x7E;
const unsigned char A1 = 0x03;
const unsigned char C1 = 0x03;
const unsigned char C2 = 0x07;
const unsigned char BCC1 = A1 ^ C1;
const unsigned char BCC2 = A1 ^ C2;

unsigned char BUFF_SET[BUF_SIZE] = {FLAG, A1, C1, BCC1, FLAG};
unsigned char BUFF_UA[BUF_SIZE]  = {FLAG, A1, C2, BCC2, FLAG};

volatile bool timeout = FALSE;
volatile int UA_received = 0;
volatile int alarmCount = 0;

void alarmHandler(int signo)
{
    timeout = TRUE;
    alarmCount++;
    printf("Timeout! Tentativa %d\n", alarmCount);
}

typedef enum { START = 1, FLAG_RCV, A_RCV, C_RCV, BCC_OK } State;

// State machine for receiver and transmitter
bool stateMachine(unsigned char controll)
{
    unsigned char byte;
    State state = START;

    while (1) {
        int r = readByteSerialPort(&byte);
        if (r <= 0) continue;

        // Debug output for each byte
        printf("Read byte: 0x%02X | Current state: %d\n", byte, state);

        switch (state)
        {
            case START:
                if (byte == FLAG)
                    state = FLAG_RCV;
                break;

            case FLAG_RCV:
                if (byte == A1)
                    state = A_RCV;
                else if (byte != FLAG)
                    state = START;
                break;

            case A_RCV:
                if (byte == controll)
                    state = C_RCV;
                else if (byte == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;

            case C_RCV:
                if (byte == (A1 ^ controll))
                    state = BCC_OK;
                else if (byte == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;

            case BCC_OK:
                if (byte == FLAG) {
                    printf("✅ Valid frame detected!\n");
                    return true;
                } else
                    state = START;
                break;
        }
    }
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    if (openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate) < 0) {
        perror("openSerialPort");
        return -1;
    }

    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = alarmHandler;
    if (sigaction(SIGALRM, &act, NULL) == -1) {
        perror("sigaction");
        closeSerialPort();
        return -1;
    }

    if (connectionParameters.role == LlTx) {
        printf("Transmitter: sending SET frame...\n");
        alarmCount = 0;
        UA_received = 0;

        while (alarmCount < connectionParameters.nRetransmissions && UA_received == 0) {
            writeBytesSerialPort(BUFF_SET, BUF_SIZE);
            printf("SET frame sent\n");

            timeout = FALSE;
            alarm(connectionParameters.timeout);

            if (stateMachine(C2)) {
                printf("UA frame received. Connection established!\n");
                UA_received = 1;
                alarm(0);
            } else {
                printf("Timeout reached, retrying...\n");
            }
        }

        if (!UA_received) {
            printf("Failed to receive UA after %d attempts.\n", alarmCount);
            return -1;
        }
    }
    else if (connectionParameters.role == LlRx) {
        printf("Receiver: waiting for SET frame...\n");
        int connected = 0;

        while (!connected) {
            if (stateMachine(C1)) {
                printf("SET frame received. Sending UA...\n");
                writeBytesSerialPort(BUFF_UA, BUF_SIZE);
                printf("UA sent. Connection established!\n");
                connected = 1;
            }
        }
    }

    return 1;
}

////////////////////////////////////////////////
// LLWRITE / LLREAD / LLCLOSE
////////////////////////////////////////////////


int llwrite(const unsigned char *buf, int bufSize)
{
    return 0;
}

int llread(unsigned char *packet)
{
    return 0;
}

int llclose()
{
    closeSerialPort();
    return 0;
}
