// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"


#include <stdio.h>
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


volatile int TIMEOUT = 0;
volatile int UA_received = 0;
int alarmCount = 0;


void alarmHandler(int signo)
{
    TIMEOUT = 1;
    alarmCount++;
    printf("Timeout! Tentativa %d\n", alarmCount);
}

typedef enum { START, FLAG_RCV, A_RCV, C_RCV, BCC_OK } State;

int stateMachine(unsigned char byte, unsigned char control, unsigned char state)
{

//Todo: Revise this function

    switch (state)
    {
        case 1:
            if (byte == FLAG) return 2;
            printf("curent: 0x%02X\n", state);
        case 2:
            if (byte == A1) return 3;
            else return 1;
            printf("curent: 0x%02X\n", state);

        case 3:
            if (byte == control) return 4;
            else if (byte == FLAG) state = FLAG_RCV;
            else return 1;
            printf("curent: 0x%02X\n", state);

        case 4:
            if (byte == (A1 ^ control)) return 5;
            else if (byte == FLAG) return 2;
            else return 1;
            printf("curent: 0x%02X\n", state);

        case 5:
            if (byte == FLAG) {
                return 2;
            } else return 1;
            printf("curent: 0x%02X\n", state);
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

            TIMEOUT = 0;
            alarm(connectionParameters.timeout);

            unsigned char byte;
            while (!TIMEOUT && !UA_received) {
                int r = readByteSerialPort(&byte);
                if (r > 0 && stateMachine(byte, C2)) {
                    printf("UA frame received. Connection established!\n");
                    UA_received = 1;
                    alarm(0);
                }
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
            int r = readByteSerialPort(&byte);
            if (r > 0 && stateMachine(byte, C1)) {
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