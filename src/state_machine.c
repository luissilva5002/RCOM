#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "state_machine.h"
#include "serial_port.h"
#include "link_layer.h"

typedef enum {
    STATE_START,
    STATE_FLAG_RCV,
    STATE_A_RCV,
    STATE_C_RCV,
    STATE_BCC1_OK,
    STATE_DATA,
    STATE_DATA_ESC,
    STATE_STOP
} FrameState;

bool stateMachineLLOpen(unsigned char controll) {

    unsigned char byte;
    unsigned char state = 1;

    while ((!timeout && ((controll == DISC) || (controll == C_UA))) || (!timeout && (controll == C_UA)) || (controll == C_SET))
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

    return false;
}

int stateMachineLLWrite(unsigned char *ctrl) {
    unsigned char byte;
    int state = 0;

    while (!timeout) {
        int r = readByteSerialPort(&byte);
        if (r <= 0) continue;

        switch (state) {

            case 0: 
                if (byte == FLAG){
                    state = 1; 
                }
                break;

            case 1:
                if (byte == A1) state = 2;
                else if (byte != FLAG) state = 0;
                break;

            case 2:
                if (byte == 0x05 || byte == 0x85 || byte == 0x01 || byte == 0x81 || byte == 0x07) {
                    *ctrl = byte;
                    state = 3;
                } else if (byte == FLAG) state = 1;
                else state = 0;
                break;

            case 3:
                if (byte == (A1 ^ *ctrl)) state = 4;
                else if (byte == FLAG) state = 1;
                else state = 0;
                break;

            case 4:
                if (byte == FLAG) {
                    alarm(0);
                    return (*ctrl == 0x05 || *ctrl == 0x85 || *ctrl == 0x07) ? 1: (*ctrl == 0x01 || *ctrl == 0x81) ? 0 : -1;
                } else 
                    state = 0;
                break;
        }
    }
    return -1; // timeout
}


// --- State machine for llread() ---
bool stateMachineLLRead(unsigned char *frame, int *frameIndex, unsigned char *A, unsigned char *C) {
    
    unsigned char byte;
    FrameState state = STATE_START;
    printf("[llread] Waiting for I-frame...\n");

    *frameIndex = 0;

    while (state != STATE_STOP) {
        int r = readByteSerialPort(&byte);
        if (r <= 0) continue;

        switch (state) {
            case STATE_START:
                if (byte == FLAG)
                    state = STATE_FLAG_RCV;
                break;

            case STATE_FLAG_RCV:
                if (byte == A1) {
                    *A = byte;  // ✅ dereference pointer
                    state = STATE_A_RCV;
                } else if (byte != FLAG)
                    state = STATE_START;
                break;

            case STATE_A_RCV:
                if (byte == 0x00 || byte == 0x40) {
                    *C = byte;  // ✅ dereference pointer
                    state = STATE_C_RCV;
                } else if (byte == FLAG)
                    state = STATE_FLAG_RCV;
                else
                    state = STATE_START;
                break;


            case STATE_C_RCV:
                if (byte == (*A ^ *C))
                    state = STATE_BCC1_OK;
                else if (byte == FLAG)
                    state = STATE_FLAG_RCV;
                else
                    state = STATE_START;
                break;

            case STATE_BCC1_OK:
                if (byte == FLAG)
                    state = STATE_START; 
                else if (byte == 0x7D)
                    state = STATE_DATA_ESC; 
                else {
                    frame[(*frameIndex)++] = byte;
                    state = STATE_DATA;
                }
                break;

            case STATE_DATA:
                if (byte == FLAG)
                    state = STATE_STOP;
                else if (byte == 0x7D)
                    state = STATE_DATA_ESC;
                else
                    frame[(*frameIndex)++] = byte;
                break;

            case STATE_DATA_ESC:
                if (byte == 0x5E)
                    frame[(*frameIndex)++] = 0x7E;
                else if (byte == 0x5D)
                    frame[(*frameIndex)++] = 0x7D;
                else {
                    printf("[llread] Error: invalid stuffing sequence (0x%02X)\n", byte);
                    state = STATE_START;
                    *frameIndex = 0;
                }
                state = STATE_DATA;
                break;

            default:
                break;
        }
    }

    printf("[llread] Complete frame received (%d bytes)\n", *frameIndex);

    if (*frameIndex < 2) {
        printf("[llread] Frame too short.\n");
        return false;
    }

    return true;
}
