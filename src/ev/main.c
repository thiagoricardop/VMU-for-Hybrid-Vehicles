#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "ev.h"

#ifndef TESTING
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
#endif