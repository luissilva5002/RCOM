#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

#define _POSIX_SOURCE 1  // POSIX compliant source

#define FALSE 0
#define TRUE  1

#define BAUDRATE  B38400
#define BUF_SIZE  5

// ---------------------------------------------------
// FRAME CONSTANTS
// ---------------------------------------------------
const unsigned char FLAG  = 0x7E;
const unsigned char A1    = 0x03;
const unsigned char C1    = 0x03; // SET
const unsigned char C2    = 0x07; // UA
const unsigned char BCC1  = 0x03 ^ 0x03;
const unsigned char BCC2  = 0x03 ^ 0x07;

unsigned char BUFF_SET[BUF_SIZE] = { FLAG, A1, C1, BCC1, FLAG };
unsigned char BUFF_UA [BUF_SIZE] = { FLAG, A1, C2, BCC2, FLAG };

int fd = -1;
struct termios oldtio;

volatile int TIMEOUT     = FALSE;
volatile int UA_received = FALSE;
int alarmCount = 0;

// ---------------------------------------------------
// ALARM HANDLER
// ---------------------------------------------------
void alarmHandler(int signo)
{
TIMEOUT = TRUE;
alarmCount++;
printf("Timeout! Tentativa %d\n", alarmCount);
}

// ---------------------------------------------------
// SERIAL PORT FUNCTIONS 
// ---------------------------------------------------
int  openSerialPort(const char *serialPort, int baudRate);
int  closeSerialPort();
int  readByteSerialPort(unsigned char *byte);
int  writeBytesSerialPort(const unsigned char *bytes, int nBytes);

// ---------------------------------------------------
// RECEIVER STATE MACHINE
// ---------------------------------------------------
typedef enum{
    START,FLAG_RCV,A_RCV,C_RCV,BCC_OK,STOP_STATE
} 
State;

int stateMachine(unsigned char byte, unsigned char control){
    static State state = START;

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
            if (byte == control)
                state = C_RCV;
            else if (byte == FLAG)
                state = FLAG_RCV;
            else
                state = START;
            break;

        case C_RCV:
            if (byte == (A1 ^ control))
                state = BCC_OK;
            else if (byte == FLAG)
                state = FLAG_RCV;
            else
                state = START;
            break;

        case BCC_OK:
            if (byte == FLAG)
            {
                state = START; // reset para próxima trama
                return TRUE;
            }
            else
                state = START;
            break;

        case STOP_STATE:
            state = START;
            break;
    }

    return FALSE;

}

// ---------------------------------------------------
// MAIN
// ---------------------------------------------------
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
    printf("Incorrect usage.\n");
    printf("Usage: %s <SerialPort> <role>\n", argv[0]);
    printf("Example: %s /dev/ttyS0 tx\n", argv[0]);
    printf("         %s /dev/ttyS1 rx\n", argv[0]);
    exit(1);
    }


    const char *serialPort = argv[1];
    const char *role       = argv[2];

    if (openSerialPort(serialPort, BAUDRATE) < 0)
    {
        perror("openSerialPort");
        exit(-1);
    }

    printf("Serial port %s opened as %s\n", serialPort, role);

    // configurar signal handler
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = alarmHandler;
    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        closeSerialPort();
        exit(1);
    }

// --------------------------------------------------
// TRANSMITTER
// ---------------------------------------------------
    if (strcmp(role, "tx") == 0)
    {
        printf("Transmitter: sending SET frame...\n");
        alarmCount = 0;
        UA_received = FALSE;

        while (alarmCount < 3 && UA_received == FALSE)
        {
            writeBytesSerialPort(BUFF_SET, BUF_SIZE);
            printf("SET frame sent\n");

            TIMEOUT = FALSE;
            alarm(3); // timeout de 3 segundos

            unsigned char byte;
            while (TIMEOUT == FALSE && UA_received == FALSE)
            {
                int r = readByteSerialPort(&byte);
                if (r > 0)
                {
                    if (stateMachine(byte, C2))
                    {
                        printf("UA frame received. Connection established!\n");
                        UA_received = TRUE;
                        alarm(0); // cancela o timeout
                    }
                }
            }

            if (!UA_received)
                printf("No UA received, retrying...\n");
        }

        if (!UA_received)
            printf("Failed to receive UA after %d attempts.\n", alarmCount);
    }

// ---------------------------------------------------
// RECEIVER
// --------------------------------------------------
    else if (strcmp(role, "rx") == 0)
    {
        printf("Receiver: waiting for SET frame...\n");
        unsigned char byte;
        int connected = FALSE;

        while (!connected)
        {
            int r = readByteSerialPort(&byte);
            if (r > 0)
            {
                if (stateMachine(byte, C1))
                {
                    printf("SET frame received. Sending UA...\n");
                    writeBytesSerialPort(BUFF_UA, BUF_SIZE);
                    printf("UA sent. Connection established!\n");
                    connected = TRUE;
                }
            }
        }
    }
    else
    {
        printf("Invalid role. Use 'tx' or 'rx'.\n");
    }

    alarm(0); // cancela alarmes pendentes

    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        exit(-1);
    }

    printf("Serial port %s closed\n", serialPort);
    return 0;

}

// --------------------------------------------------
// SERIAL PORT IMPLEMENTATION
// ---------------------------------------------------
int openSerialPort(const char *serialPort, int baudRate)
{
    fd = open(serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
    perror(serialPort);
    return -1;
    }

    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    struct termios newtio;
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME] = 0; 
    newtio.c_cc[VMIN]  = 1;

    tcflush(fd, TCIOFLUSH);
        if (tcsetattr(fd, TCSANOW, &newtio) == -1)
        {
            perror("tcsetattr");
            close(fd);
            return -1;
        }

    return fd;


}

int closeSerialPort()
{
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        return -1;
    }
    return close(fd);
}

int readByteSerialPort(unsigned char *byte)
{
    return read(fd, byte, 1);
}

int writeBytesSerialPort(const unsigned char *bytes, int nBytes)
{
    return write(fd, bytes, nBytes);
}