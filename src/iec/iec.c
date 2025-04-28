// Internal Combustion Engine (IEC) module for the Vehicle Management Unit (VMU) system.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <mqueue.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include "iec.h"
#include "../vmu/vmu.h"
#include "./VARIABLES/iec_variables.h"
#include "./INITIALIZER/iec_initializer.h"
#include "./IECQUEUE/iec_receive.h"
#include "./CONTROL_CALCULATE/iec_control_calculate.h"
#include "./CLEANER/iec_cleanUp.h"

// Function to handle signals (SIGUSR1 for pause, SIGINT/SIGTERM for shutdown)
void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        paused = !paused;
        printf("[IEC] Paused: %s\n", paused ? "true" : "false");
    } else if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
        printf("[IEC] Shutting down...\n");
    }
}

int main() {
    // Configure signal handlers for graceful shutdown and pause
    signal(SIGUSR1, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    iec_initializer();

    printf("IEC Module Running\n");

    // Main loop of the IEC module
    while (running) {
        if (!paused) {

            // Receive commands from the VMU through the message queue
            cmd = iec_receive(cmd);
            
            sem_wait(sem); // Acquire the semaphore to protect shared memory
            if (!cmd.toVMU) {

                treatValues();

                cmd.fuelIEC = fuel;
                cmd.rpm_iec = iecRPM;
                cmd.iecActive = iecActive;
                strcpy(cmd.check, "ok");
                cmd.toVMU = true;
                mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);
                
            }

            else {
                mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);
                
            }

            sem_post(sem);
        } else {
            sleep(1); // Sleep for 1 second if paused
        }
    }

    // Cleanup resources before exiting
    iecCleanUp();

    printf("[IEC] Shut down complete.\n");
    return 0;
}
