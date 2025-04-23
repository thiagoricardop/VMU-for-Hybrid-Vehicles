#include "iec_control_calculate.h"

void calculateValues() {

    if (localVelocity <= 15.0) {
        gear = 1;
    }
    else if (localVelocity <= 30.0) {
        gear = 2;
    }
    else if (localVelocity <= 40.0) {
        gear = 3;
    }
    else if (localVelocity <= 70.0) {
        gear = 4;
    }
    else {
        gear = 5;
    }
    
    if ( iecActive && fuel > 0.0) {
        if (fuel > 0.0) {
            fuel -= iecPercentage*(localVelocity/(averageConsumeKMl*36000.0));
            if (fuel < 0.0) {
                fuel == 0.0;
            }    
        }

    }

    else if (!iecActive) {
        iecRPM = 0.0;
    }

    if (iecActive) {
        iecRPM = ((localVelocity*16.67)/tireCircunferenceRatio)*(gearRatio[gear-1] * 3.55); 
    }

}

void treatValues() {
    
    // Process the received command
    switch (cmd.type) {
        case CMD_START:

            break;
        case CMD_STOP:
            system_state->iec_on = false;
            system_state->rpm_iec = 0; // Reset RPM when stopped
            break;

        case CMD_END:
            running = 0; // Terminate the main loop
            break;
        default:
            
            break;
    }        


    localVelocity = cmd.globalVelocity;
    iecPercentage = cmd.power_level;
    ev_on = cmd.ev_on;
    
    if (iecPercentage != 0.0) {
        iecActive = true;                
    }
    else {
        iecActive = false;            
    }

    calculateValues();
    
    system("clear");
    printf("\nIEC usage percentage: %f", iecPercentage); 
    printf("\nIEC RPM: %f", iecRPM);

    // Simulate IEC engine behavior
    if (system_state->iec_on) {
        // Increase temperature based on the transition factor
        system_state->temp_iec += system_state->transition_factor * 0.1;
    } else {
        system_state->rpm_iec = 0; // Set RPM to 0 when off
        // Cool down the engine if it's above the ambient temperature
        if (system_state->temp_iec > 25.0) {
            system_state->temp_iec -= 0.02;
        }
    }
}