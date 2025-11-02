// Link layer protocol implementation
#include "link_layer.h"
#include "serial_port.h"
#include "packet_helper.h"
#include "state_machine.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>

const unsigned char FLAG = 0x7E;
const unsigned char A1   = 0x03;
const unsigned char C_SET = 0x03;
const unsigned char C_UA = 0x07;
const unsigned char DISC = 0x0B;

unsigned char BUFF_SET[BUF_SIZE] = {FLAG, A1, C_SET, A1 ^ C_SET, FLAG};
unsigned char BUFF_UA[BUF_SIZE]  = {FLAG, A1, C_UA, A1 ^ C_UA, FLAG};
unsigned char BUFF_DISC[BUF_SIZE]= {FLAG, A1, DISC, A1 ^ DISC, FLAG};

volatile bool timeout = false;
volatile bool connected = false;
volatile int UA_received = 0;
volatile int alarmCount = 0;

////////////////////////////////////////////////
// ALARM
////////////////////////////////////////////////

void alarmHandler(int signo)
{
    timeout = true;
    alarmCount++;
    printf("Timeout! Tentativa %d\n", alarmCount);
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

            timeout = false;
            alarm(connectionParameters.timeout);

            if (/*stateMachine(C_UA)*/ stateMachineLLOpen(C_UA)) {
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

        if (/*stateMachine(C_SET)*/ stateMachineLLOpen(C_SET)) {
            printf("SET frame received. Sending UA...\n");
            writeBytesSerialPort(BUFF_UA, BUF_SIZE);
            printf("UA sent. Connection established!\n");
            connected = true;
        }
        
    }

    return 1; // sucesso
}

////////////////////////////////////////////////
// LLWRITE
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
    // BYTE STUFFING
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

    // DATA
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
    // TRANSMISSION
    //////////////////////////////////////////////////////////////

    alarmCount = 0;
    bool ackReceived = false;
    timeout = false;

    printf("[llwrite] Frame I(%d) pronto (%d bytes após stuffing)\n", Ns, stuffedIndex);

    while (alarmCount < conParams.nRetransmissions && !ackReceived)
    {
        writeBytesSerialPort(stuffedData, stuffedIndex);
        printf("[llwrite] I-frame (Ns=%d) enviado (tentativa %d)\n", Ns, alarmCount + 1);

        timeout = false;
        alarm(conParams.timeout);
        unsigned char ctrl;

        int result = stateMachineLLWrite(&ctrl);

        if (result == 1) {          // RR or UA
            ackReceived = true;
            printf("[llwrite] Supervisão recebida (C=0x%02X)\n", ctrl);
            if (ctrl == 0x05 || ctrl == 0x85) 
                printf("[llwrite] ✅ RR recebido — ACK OK\n");
            else if (ctrl == 0x07) 
                printf("[llwrite] ✅ UA recebido — ligação confirmada\n");
        }
        else if (result == 0) {     // REJ
            ackReceived = false;
            printf("[llwrite] ⚠️ REJ recebido — reenviando frame\n");
        }
        else {                      //timeout / error 
            ackReceived = false;
            printf("[llwrite] ⏱️ Timeout ou erro — reenviando frame\n");
        }

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
// LLREAD
////////////////////////////////////////////////

int llread(unsigned char *packet)
{
    if (packet == NULL) {
        printf("[llread] Erro: ponteiro nulo.\n");
        return -1;
    }

    unsigned char frame[2 * MAX_PACKET_SIZE]; 
    unsigned char A = 0, C = 0;
    int frameIndex = 0;
    
    if (!stateMachineLLRead(frame, &frameIndex, &A, &C)) return -1;

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

            timeout = false;
            alarm(connectionParameters.timeout);

            if (stateMachineLLOpen(DISC)) {
                printf("DISC received. Sending UA...\n");
                writeBytesSerialPort(BUFF_UA, BUF_SIZE);
                connected = false;
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

            timeout = false;
            alarm(connectionParameters.timeout);

            if (stateMachineLLOpen(DISC)) {

                printf("DISC received. Sending DISC back...\n");
                writeBytesSerialPort(BUFF_DISC, BUF_SIZE);

                printf("Waiting for UA...\n");
                stateMachineLLOpen(C_UA);

                connected = false;
                alarm(0);

            } else {
                printf("Timeout reached. Retrying...\n");
            }
        }    
    }

    closeSerialPort();
    printf("Connection closed.\n");
    return 0;
}