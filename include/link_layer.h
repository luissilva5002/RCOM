#ifndef _LINK_LAYER_H_
#define _LINK_LAYER_H_

#include <stdbool.h>
#include <termios.h>

#define BUF_SIZE 5
#define MAX_PACKET_SIZE 1024

// Control/flag bytes
extern const unsigned char FLAG;
extern const unsigned char A1;
extern const unsigned char C_SET;
extern const unsigned char C_UA;
extern const unsigned char DISC;

// Prebuilt frames
extern unsigned char BUFF_SET[BUF_SIZE];
extern unsigned char BUFF_UA[BUF_SIZE];
extern unsigned char BUFF_DISC[BUF_SIZE];

// State machine globals
extern volatile bool timeout;
extern volatile bool connected;
extern volatile int UA_received;
extern volatile int alarmCount;

// LinkLayer role
typedef enum { LlTx, LlRx } LinkLayerRole;

typedef struct {
    char serialPort[50];
    LinkLayerRole role;
    int baudRate;
    int nRetransmissions;
    int timeout;
} LinkLayer;

// Function prototypes
void alarmHandler(int signo);

int llopen(LinkLayer connectionParameters);
int llwrite(const unsigned char *buf, int bufSize);
int llread(unsigned char *packet);
int llclose(LinkLayer connectionParameters);

#endif // _LINK_LAYER_H_
