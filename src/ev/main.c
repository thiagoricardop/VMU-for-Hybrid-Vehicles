#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "ev.c"

int main() {
    system("clear");
    // Initialize communication with VMU
    if(init_communication_ev(SHARED_MEM_NAME, SEMAPHORE_NAME, IEC_COMMAND_QUEUE_NAME) == 0){
        exit(EXIT_FAILURE);
    }    
    
    // Main loop of the EV module
    while (running) {
        if (!paused) {
            receive_cmd(); // Receive commands from the VMU
            engine(); // Call the engine function to update the engine state
            // Simulate EV engine behavior
            usleep(70000); // Small delay for the IEC loop
        } else {
            sleep(1); // Sleep for 1 second if paused
        }
    }

    cleanup(); // Cleanup resources before exiting
    return 0;
}