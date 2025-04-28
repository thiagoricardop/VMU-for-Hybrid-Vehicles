#include "ev_control_calculate.h"

void calculateValues () {

    if (!accelerator && localVelocity != 0.0) {
        BatteryEV += 1.0;
    }

    else if (evActive && localVelocity > 0.0) {
        BatteryEV -= 0.01;
    }

    if (BatteryEV < 0.0) {
        BatteryEV = 0.0;
    } 

    if (BatteryEV > 100.0) {
        BatteryEV = 100.0;
    }
    lastLocalVelocity = localVelocity;

    if (fuel > 0.0) {

        rpmEV = evPercentage*((localVelocity * 16.67) / tireCircunferenceRatio);
    }

    else {
        rpmEV = evPercentage*((localVelocity * 16.67) / tireCircunferenceRatio);
        
        if (rpmEV > 341.113716) {
            rpmEV = 341.113716;
        }
    }
    
}

void ev_treatValues () {

    switch (cmd.type) {
                    
        case CMD_START:
            if ( BatteryEV >= 10) {
       
                evActive = true;
                strcpy(cmd.check, "ok");
   
            }

            else {
                cmd.evActive = false;
                evActive = false;
                strcpy(cmd.check, "no");
            }
            break;
        
        case CMD_STOP:
            
            break;

        case CMD_END:
            running = 0; // Terminate the main loop
            break;
        default:
            
            break;
    }

    localVelocity = cmd.globalVelocity;
    evPercentage = cmd.power_level;
    accelerator = cmd.accelerator;
    fuel = cmd.iec_fuel;

    if (evPercentage != 0) {
        evActive = true;                
    }
    else {
        evActive = false;            
    }

    // Simulate EV engine behavior
    if (evActive) {
        // Increase temperature based on the inverse of the transition factor
        system_state->temp_ev += (1.0 - system_state->transition_factor) * 0.05;
    } else {
        // Cool down the engine if it's above the ambient temperature
        if (system_state->temp_ev > 25.0) {
            system_state->temp_ev -= 0.01;
        }
    }

    calculateValues();    
}