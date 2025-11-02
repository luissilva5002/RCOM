#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdbool.h>
#include <stddef.h>

// --- Function declarations ---
bool stateMachineLLOpen(unsigned char controll);      // for UA/SET/DISC
int stateMachineLLWrite(unsigned char *ctrl);
bool stateMachineLLRead(unsigned char *frame, int *frameIndex, unsigned char *A, unsigned char *C);

#endif // STATE_MACHINE_H
