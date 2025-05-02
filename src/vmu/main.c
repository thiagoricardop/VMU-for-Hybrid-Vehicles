#include "vmu.h"


struct timespec get_abs_timeout(int seconds) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += seconds;
    return ts;
}

#ifndef TESTING
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
#endif