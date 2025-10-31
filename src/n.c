int llopen(LinkLayer connectionParameters)
{
    if (connected) return -1;

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
                connected = TRUE;
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
        bool connecting = TRUE;

        while (connecting) {
            if (stateMachine(C1)) {
                printf("SET frame received. Sending UA...\n");
                writeBytesSerialPort(BUFF_UA, BUF_SIZE);
                printf("UA sent. Connection established!\n");
                connecting = FALSE;
                connected = TRUE;
            }
        }
    }

    return 1;
}