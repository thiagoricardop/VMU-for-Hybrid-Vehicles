// Electric Vehicle (EV) module for the Vehicle Management Unit (VMU) system.

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
#include "ev.h"

#include "./VARIABLES/ev_variables.h"
#include "../vmu/vmu.h"
#include "./EVQUEUE/ev_receive.h"
#include "./CONTROL_CALCULATE/ev_control_calculate.h"
#include "./CLEANER/ev_cleanUp.h"
#include "./INITIALIZER/ev_initializer.h"


// Function to handle signals (SIGUSR1 for pause, SIGINT/SIGTERM for shutdown)
void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        paused = !paused;
        printf("[EV] Paused: %s\n", paused ? "true" : "false");
    } else if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
        printf("[EV] Shutting down...\n");
    }
}


int main() {
    // Configure signal handlers for graceful shutdown and pause
    signal(SIGUSR1, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int status = ev_initializer();

    printf("EV Module Running\n");

    // Main loop of the EV module
    while (running) {
        if (!paused) {

            cmd = ev_receive(cmd);
         
            sem_wait(sem); // Acquire the semaphore to protect shared memory
            if (!cmd.toVMU) {
            // Receive commands from the VMU through the message queue
                // Process the received command
                
                ev_treatValues();

                system("clear");
                printf("\nEV usage percentage: %f", evPercentage);
                cmd.batteryEV = BatteryEV;
                cmd.evActive = evActive;
                cmd.rpm_ev = rpmEV;
                sprintf(cmd.check, "Bateria enviada: %f", BatteryEV);
                cmd.toVMU = true;

                mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
           
            }
            
            else {
                mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
       
            } 
            sem_post(sem);   
            
        } else {
            sleep(1); // Sleep for 1 second if paused
        }
    }

    ev_cleanUp();

    printf("[EV] Shut down complete.\n");
    return 0;
}
