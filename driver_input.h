#ifndef DRIVER_INPUT_H
#define DRIVER_INPUT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>
#include "variables.h"


#ifndef TAM_BUFFER
#define TAM_BUFFER 3
#endif

#ifndef TIMEOUT_USEC
#define TIMEOUT_USEC 80000  // 80 ms
#endif


unsigned long diff_usec(struct timeval start, struct timeval end);
void executeAction(void);
void* input_thread(void* arg);

#endif 
