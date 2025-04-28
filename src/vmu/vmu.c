// VMU (Vehicle Management Unit) - Main control system for hybrid vehicle
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <semaphore.h>
#include <mqueue.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <pthread.h> 
#include <string.h>
#include <time.h>  
#include "vmu.h"
#include "./INITIALIZER/vmu_initialization.h"
#include "./VARIABLES/vmu_variables.h"
#include "./INTERFACE/interface.h"
#include "./SPEED/calculate_speed.h"
#include "./QUEUE/check_queue.h"
#include "./CONTROLLER/vmu_control_engines.h"
#include "./CLEANER/vmu_cleanUp.h"

#define TIMEOUT_SECONDS 5


struct timespec get_abs_timeout(int seconds) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += seconds;
    return ts;
}

// Function to handle signals (SIGUSR1 for pause, SIGINT/SIGTERM for shutdown)
void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        paused = !paused;
        printf("[VMU] Paused: %s\n", paused ? "true" : "false");
    } else if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
        printf("[VMU] Shutting down...\n");
    }
}


int main() {

    struct timeval start, end;
    unsigned char counterEV = 0;
    unsigned char counterIEC = 0;

    EngineCommandEV cmdEV;
    EngineCommandIEC cmdIEC;

    signal(SIGUSR1, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    strcpy(lastmsg, "");

    vmu_initialization();

    system("clear");

    // Main loop of the VMU module
    while (running) {
        if (!paused) {
            
            gettimeofday(&start, NULL);
            
            vmu_control_engines(); 
            vmu_check_queue(counterEV, ev_mq, true);
            vmu_check_queue(counterIEC, iec_mq, false);
            display_status(system_state);  // Display the current system status
            calculate_speed(system_state);

            gettimeofday(&end, NULL);

            elapsed = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;

            if (elapsed < 100) {
                delay_ms = 100 - elapsed;
                // Converte delay_ms para microsegundos e faz o delay
                usleep(delay_ms * 1000);
            }
            
        } else {
            sleep(1); // Sleep for 1 second if paused
        }
    }

    cleanUp();

    printf("[VMU] Shut down complete.\n");
    return 0;
}
