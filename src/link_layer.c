// Link layer protocol implementation
#include "link_layer.h"
#include "serial_port.h"
#include "packet_helper.h"

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
const unsigned char C_UA = 0x07;
const unsigned char BCC1 = A1 ^ C1;
const unsigned char BCC2 = A1 ^ C2;
const unsigned char DISC = 0x0B;

unsigned char BUFF_SET[BUF_SIZE] = {FLAG, A1, C1, BCC1, FLAG};
unsigned char BUFF_UA[BUF_SIZE]  = {FLAG, A1, C2, BCC2, FLAG};
unsigned char BUFF_DISC[BUF_SIZE] = {FLAG, A1, 0xFF, A1^DISC, FLAG};

typedef enum { START = 1, FLAG_RCV, A_RCV, C_RCV, BCC_OK } State;

volatile bool timeout = FALSE;
volatile bool connected = FALSE;

volatile int UA_received = 0;
volatile bool DISC_received = FALSE;
volatile int alarmCount = 0;

////////////////////////////////////////////////
// ALARM
////////////////////////////////////////////////

void alarmHandler(int signo)
{
    timeout = TRUE;
    alarmCount++;
    printf("Timeout! Tentativa %d\n", alarmCount);
}

////////////////////////////////////////////////
// STATE MACHINES
////////////////////////////////////////////////

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

bool Close_stateMachine(unsigned char controll, LinkLayer connectionParameters)
{
    unsigned char byte;
    unsigned char state = 1;


    while (!timeout)
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
                    printf("Valid closing frame detected!\n");
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

LinkLayer conParams;
int llopen(LinkLayer connectionParameters)
{
    if (connected) return -1;

    // abrir porta
    if (openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate) < 0) {
        perror("openSerialPort");
        return -1;
    }
    conParams = connectionParameters;

    // configurar handler
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = alarmHandler;

    if (sigaction(SIGALRM, &act, NULL) == -1) {
        perror("sigaction");
        closeSerialPort();
        return -1;
    }

    // protocolo de conexão
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
                connected = true;
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

        if (stateMachine(C1)) {
            printf("SET frame received. Sending UA...\n");
            writeBytesSerialPort(BUFF_UA, BUF_SIZE);
            printf("UA sent. Connection established!\n");
            connected = TRUE;
        }
        
    }

    return 1; // sucesso
}

////////////////////////////////////////////////
// LLWRITE — State Machine integrada
////////////////////////////////////////////////


int llwrite(const unsigned char *buf, int bufSize)
{
    if (buf == NULL || bufSize <= 0) {
        printf("[llwrite] Erro: buffer inválido.\n");
        return -1;
    }

    static int Ns = 0; // número de sequência (0 ou 1)

    unsigned char A = A1;
    unsigned char C = (Ns << 6); // bit 6 = Ns
    unsigned char BCC1 = A ^ C;

    unsigned char BCC2 = 0x00;
    for (int i = 0; i < bufSize; i++)
        BCC2 ^= buf[i];

    //////////////////////////////////////////////////////////////
    // Construção do frame com byte stuffing
    //////////////////////////////////////////////////////////////

    unsigned char stuffedData[2 * MAX_PACKET_SIZE];
    int stuffedIndex = 0;

    stuffedData[stuffedIndex++] = FLAG;

    // A
    if (A == FLAG) { stuffedData[stuffedIndex++] = 0x7D; stuffedData[stuffedIndex++] = 0x5E; }
    else if (A == 0x7D) { stuffedData[stuffedIndex++] = 0x7D; stuffedData[stuffedIndex++] = 0x5D; }
    else stuffedData[stuffedIndex++] = A;

    // C
    if (C == FLAG) { stuffedData[stuffedIndex++] = 0x7D; stuffedData[stuffedIndex++] = 0x5E; }
    else if (C == 0x7D) { stuffedData[stuffedIndex++] = 0x7D; stuffedData[stuffedIndex++] = 0x5D; }
    else stuffedData[stuffedIndex++] = C;

    // BCC1
    if (BCC1 == FLAG) { stuffedData[stuffedIndex++] = 0x7D; stuffedData[stuffedIndex++] = 0x5E; }
    else if (BCC1 == 0x7D) { stuffedData[stuffedIndex++] = 0x7D; stuffedData[stuffedIndex++] = 0x5D; }
    else stuffedData[stuffedIndex++] = BCC1;

    // Dados (com stuffing)
    for (int i = 0; i < bufSize; i++) {
        if (buf[i] == FLAG) {
            stuffedData[stuffedIndex++] = 0x7D;
            stuffedData[stuffedIndex++] = 0x5E;
        } else if (buf[i] == 0x7D) {
            stuffedData[stuffedIndex++] = 0x7D;
            stuffedData[stuffedIndex++] = 0x5D;
        } else {
            stuffedData[stuffedIndex++] = buf[i];
        }
    }

    // BCC2
    if (BCC2 == FLAG) { stuffedData[stuffedIndex++] = 0x7D; stuffedData[stuffedIndex++] = 0x5E; }
    else if (BCC2 == 0x7D) { stuffedData[stuffedIndex++] = 0x7D; stuffedData[stuffedIndex++] = 0x5D; }
    else stuffedData[stuffedIndex++] = BCC2;

    
    stuffedData[stuffedIndex++] = FLAG;

    //////////////////////////////////////////////////////////////
    // Envio e retransmissão
    //////////////////////////////////////////////////////////////
    alarmCount = 0;
    bool ackReceived = FALSE;
    timeout = FALSE;

    printf("[llwrite] Frame I(%d) pronto (%d bytes após stuffing)\n", Ns, stuffedIndex);

    while (alarmCount < conParams.nRetransmissions && !ackReceived)
    {
        writeBytesSerialPort(stuffedData, stuffedIndex);
        printf("[llwrite] I-frame (Ns=%d) enviado (tentativa %d)\n", Ns, alarmCount + 1);

        timeout = FALSE;
        alarm(conParams.timeout);

        //--------------------------------------------------
        // INLINE STATE MACHINE 
        //--------------------------------------------------

        unsigned char byte, ctrl = 0;
        int state = 0;

        while (!timeout && !ackReceived)
        {
            int r = readByteSerialPort(&byte);
            if (r <= 0) continue;

            switch (state)
            {
                case 0: // START
                    if (byte == FLAG) state = 1;
                    break;

                case 1: // FLAG_RCV
                    if (byte == A1) state = 2;
                    else if (byte != FLAG) state = 0;
                    break;

                case 2: // A_RCV
                    if (byte == 0x05 || byte == 0x85 ||  // RR(0)/RR(1)
                        byte == 0x01 || byte == 0x81 ||  // REJ(0)/REJ(1)
                        byte == 0x07)                    // UA
                    {
                        ctrl = byte;
                        state = 3;
                    }
                    else if (byte == FLAG)
                        state = 1;
                    else
                        state = 0;
                    break;

                case 3: // C_RCV
                    if (byte == (A1 ^ ctrl))
                        state = 4;
                    else if (byte == FLAG)
                        state = 1;
                    else
                        state = 0;
                    break;

                case 4: // BCC_OK
                    if (byte == FLAG) {
                        
                        alarm(0);
                        printf("[llwrite] Supervisão recebida (C=0x%02X)\n", ctrl);

                        if (ctrl == 0x05 || ctrl == 0x85) { // RR(0) / RR(1)
                            printf("[llwrite] ✅ RR recebido — ACK OK\n");
                            ackReceived = TRUE;
                        }
                        else if (ctrl == 0x01 || ctrl == 0x81) { // REJ(0) / REJ(1)
                            printf("[llwrite] ⚠️ REJ recebido — reenviando frame\n");
                            ackReceived = FALSE;
                        }
                        else if (ctrl == 0x07) { // UA
                            printf("[llwrite] ✅ UA recebido — ligação confirmada\n");
                            ackReceived = TRUE;
                        }
                        state = 5;
                    } else state = 0;
                    break;
            }

            if (state == 5) break;
        }
        //--------------------------------------------------

        if (!ackReceived)
            printf("[llwrite] ⏱️ Timeout ou REJ — reenviando...\n");

        alarmCount++;
    }

    if (!ackReceived) {
        printf("[llwrite] ❌ Falha após %d tentativas — sem ACK.\n", alarmCount);
        return -1;
    }

    Ns = 1 - Ns; 
    printf("[llwrite] ✅ Envio concluído com sucesso (%d bytes payload)\n", bufSize);
    return bufSize;
}


////////////////////////////////////////////////
// LLREAD — State Machine integrada
////////////////////////////////////////////////

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

int llread(unsigned char *packet)
{
    if (packet == NULL) {
        printf("[llread] Erro: ponteiro nulo.\n");
        return -1;
    }

    unsigned char byte;
    unsigned char frame[2 * MAX_PACKET_SIZE]; 
    int frameIndex = 0;

    FrameState state = STATE_START;
    unsigned char A = 0, C = 0;

    printf("[llread] Aguardando I-frame...\n");

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
                    A = byte;
                    state = STATE_A_RCV;
                } else if (byte != FLAG)
                    state = STATE_START;
                break;

            case STATE_A_RCV:
                if (byte == 0x00 || byte == 0x40) {
                    C = byte;
                    state = STATE_C_RCV;
                } else if (byte == FLAG)
                    state = STATE_FLAG_RCV;
                else
                    state = STATE_START;
                break;

            case STATE_C_RCV:
                if (byte == (A ^ C))
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
                    frame[frameIndex++] = byte;
                    state = STATE_DATA;
                }
                break;

            case STATE_DATA:
                if (byte == FLAG)
                    state = STATE_STOP;
                else if (byte == 0x7D)
                    state = STATE_DATA_ESC;
                else
                    frame[frameIndex++] = byte;
                break;

            case STATE_DATA_ESC:
                if (byte == 0x5E) frame[frameIndex++] = 0x7E;
                else if (byte == 0x5D) frame[frameIndex++] = 0x7D;
                else {
                    printf("[llread] Erro: sequência de stuffing inválida (0x%02X)\n", byte);
                    state = STATE_START;
                    frameIndex = 0;
                }
                state = STATE_DATA;
                break;

            default:
                break;
        }
    }

    printf("[llread] Frame completo recebido (%d bytes úteis)\n", frameIndex);

    if (frameIndex < 2) {
        printf("[llread] Frame demasiado curto.\n");
        return -1;
    }

    unsigned char BCC2 = frame[frameIndex - 1];
    unsigned char calcBCC2 = 0x00;
    for (int i = 0; i < frameIndex - 1; i++)
        calcBCC2 ^= frame[i];

    bool bcc2_ok = (BCC2 == calcBCC2);
 
    int Ns = (C >> 6) & 0x01;
    static int expectedNs = 0;

    if (bcc2_ok && Ns == expectedNs) {
        printf("[llread] ✅ Frame válido, BCC2 OK, Ns=%d\n", Ns);
        
        memcpy(packet, frame, frameIndex - 1);
        
        unsigned char RR[5] = {FLAG, A1, (expectedNs ? 0x05 : 0x85), A1 ^ (expectedNs ? 0x05 : 0x85), FLAG};
        writeBytesSerialPort(RR, 5);
        printf("[llread] RR enviado (espera Ns=%d)\n", 1 - expectedNs);

        expectedNs = 1 - expectedNs;
        return frameIndex - 1;
    }
    else if (!bcc2_ok) {
        printf("[llread] ❌ Erro em BCC2 (esperado 0x%02X, obtido 0x%02X)\n", calcBCC2, BCC2);
        unsigned char REJ[5] = {FLAG, A1, (expectedNs ? 0x81 : 0x01), A1 ^ (expectedNs ? 0x81 : 0x01), FLAG};
        writeBytesSerialPort(REJ, 5);
        printf("[llread] REJ enviado (Ns=%d)\n", expectedNs);
        return -1;
    }
    else {
        
        printf("[llread] ⚠️ Frame duplicado Ns=%d, reenviando RR(%d)\n", Ns, expectedNs);
        unsigned char RR[5] = {FLAG, A1, (expectedNs ? 0x85 : 0x05), A1 ^ (expectedNs ? 0x85 : 0x05), FLAG};
        writeBytesSerialPort(RR, 5);
        return 0;
    }
}


////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////

int llclose(LinkLayer connectionParameters)
{
    if (!connected) {
        printf("No connection open.\n");
        return -1;
    }

    if (connectionParameters.role == LlTx) {
        printf("Transmitter: sending DISC frame...\n");

        alarmCount = 0;

        while (alarmCount < connectionParameters.nRetransmissions && connected) {
            writeBytesSerialPort(BUFF_DISC, BUF_SIZE);
            printf("DISC frame sent\n");

            timeout = FALSE;
            alarm(connectionParameters.timeout);

            if (Close_stateMachine(DISC, connectionParameters)) {
                printf("DISC received. Sending UA...\n");
                writeBytesSerialPort(BUFF_UA, BUF_SIZE);
                connected = FALSE;
                alarm(0);
            } else {
                printf("Timeout reached. Retrying...\n");
            }
        }

        if (connected) {
            printf("Failed to close after %d attempts.\n", alarmCount);
            return -1;
        }
    } 

    else if (connectionParameters.role == LlRx) {

        printf("Receiver: waiting for DISC...\n");
        while (alarmCount < connectionParameters.nRetransmissions && connected) { 

            timeout = FALSE;
            alarm(connectionParameters.timeout);

            if (Close_stateMachine(DISC, connectionParameters)) {

                printf("DISC received. Sending DISC back...\n");
                writeBytesSerialPort(BUFF_DISC, BUF_SIZE);

                printf("Waiting for UA...\n");
                Close_stateMachine(C_UA, connectionParameters);

                connected = FALSE;
                alarm(0);

            } else {
                printf("Timeout reached. Retrying...\n");
            }
        }    
    }

    closeSerialPort();
    return 0;
}