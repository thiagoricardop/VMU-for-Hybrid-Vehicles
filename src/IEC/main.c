#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "iec.c"

int main() {
    system("clear");
    init_communication(); // Initialize communication with VMU
    // Main loop of the IEC module
    while (running) {
        if (!paused) {
            
            receive_cmd(); // Receive commands from the VMU
            engine(); // Call the engine function to update the engine state

            usleep(70000); // Small delay for the IEC loop
        } else {
            sleep(1); // Sleep for 1 second if paused
        }
    }

    
    return 0;
}