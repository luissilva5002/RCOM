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
int alarmCount = 0;


void alarmHandler(int signo)
{
    timeout = TRUE;
    alarmCount++;
    printf("Timeout! Tentativa %d\n", alarmCount);
}

typedef enum { START, FLAG_RCV, A_RCV, C_RCV, BCC_OK } State;

bool stateMachine(unsigned char controll)
{
    unsigned char byte;
    unsigned char state = 1;

    while ((!timeout && (controll == C2)) || (controll == C1))
    {
        int r = readByteSerialPort(&byte);
        if (r <= 0) continue;

        printf("Read byte: 0x%02X | Current state: %d\n", byte, state);

        switch (state)
        {
            case 1: // START
                if (byte == FLAG)
                    state = 2;
                break;

            case 2: // FLAG_RCV
                if (byte == A1)
                    state = 3;
                else if (byte != FLAG)
                    state = 1;
                break;

            case 3: // A_RCV
                if (byte == controll)
                    state = 4;
                else if (byte == FLAG)
                    state = 2;
                else
                    state = 1;
                break;

            case 4: // C_RCV
                if (byte == (A1 ^ controll))
                    state = 5;
                else if (byte == FLAG)
                    state = 2;
                else
                    state = 1;
                break;

            case 5: // BCC_OK
                if (byte == FLAG)
                {
                    printf("✅ Valid frame detected!\n");
                    return true;
                }
                else
                    state = 1;
                break;
        }
    }

    return FALSE;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    // abrir porta
    if (openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate) < 0) {
        perror("openSerialPort");
        return -1;
    }

    // configurar handler
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

            unsigned char byte;
            
            if (stateMachine(C2)) {
                printf("UA frame received. Connection established!\n");
                UA_received = 1;
                alarm(0);
            } else {
                printf("Timeout reached, retrying...\n");
            }

            if (!UA_received)
                printf("No UA received, retrying...\n");
        }

        if (!UA_received) {
            printf("Failed to receive UA after %d attempts.\n", alarmCount);
            return -1;
        }
    }
    else if (connectionParameters.role == LlRx) {
        printf("Receiver: waiting for SET frame...\n");
        unsigned char byte;
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

    return 1; // sucesso
}

////////////////////////////////////////////////
// LLWRITE / LLREAD / LLCLOSE (a implementar depois)
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