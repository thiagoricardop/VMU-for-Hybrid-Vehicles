#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include "vmu.c"

int main() {

    // Initialize communication with EV and IEC modules
    init_communication();

    system("clear");
    // Main loop of the VMU module
    while (running) {
        if (!paused) {
            vmu_control_engines(); // Control the engines based on the system state
            calculate_speed(system_state); // Calculate the current speed
            display_status(system_state);  // Display the current system status
            usleep(200000); // Sleep for 200 milliseconds
        } else {
            sleep(1); // Sleep for 1 second if paused
        }
    }

    cleanup(); // Cleanup resources before exiting
    return 0;
}