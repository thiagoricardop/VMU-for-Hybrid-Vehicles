#include "vmu_control_engines.h"

// Function to control the engines based on the current speed and system state
void vmu_control_engines() {

    EngineCommandEV  cmdEV;
    EngineCommandIEC cmdIEC;
    
    sem_wait(sem); // Acquire the semaphore

    // Load current system state
    double current_speed = system_state->speed;
    double current_battery = system_state->battery;
    double current_fuel = system_state->fuel;
    bool current_accelerator = system_state->accelerator;

    // Handle stop condition when both battery and fuel depleted
    if (current_battery == IS_EMPTY && current_fuel == IS_EMPTY) {
        current_accelerator = false;
        carStop = true;
    }


    // START condition after stop when battery recharged
    if (carStop && current_battery >= CHARGE_FULL) {
        carStop = false;
    }


    // No-fuel, start transition to Eletric power mode only (ACT_ALONE = 1.0 , IS_EMPTY = 0.0 , transitionEV is a boolean)
    else if (current_battery > IS_EMPTY && current_fuel == IS_EMPTY && !transitionEV && system_state->evPercentage < ACT_ALONE) {

        sprintf(system_state->debg, "Fuel empty, starting EV transition cycle %d                               ", cont); // This extra space is required to clean the line before without having to do system("clear")
        transitionEV = true;
    }

    
    // When vehicle is in combustion only and EV battery reaches 100% of charge, start transtion to standard hybrid mode based on the current speed of the vehicle
    // Or start transition to eletric engine only when IEC fuel is empty
    else if ((transitionIEC && current_battery == CHARGE_FULL) || (system_state->fuel == IS_EMPTY)) {
    
        transitionIEC = false;
        transitionEV  = true;
    }

    
    // Gradual transition to IEC mode (combustion only)
    if (transitionIEC) {

        sprintf(system_state->debg, "IEC transition in progress %d                                             ", cont);

        system_state->evPercentage -= HALF_PERCENT; // HALF_PERCENT = 0.005
        system_state->iecPercentage += HALF_PERCENT;
        system_state->power_mode = HYBRID // Power mode setted to Hybrid

        if (system_state->iecPercentage >= ACT_ALONE) {
            system_state->evPercentage = INACTIVE; 
            system_state->iecPercentage = ACT_ALONE;
            system_state->power_mode = COMBUSTION_ONLY; // Power mode setted to Combustion only
            system_state->ev_on = false;
        }

        // This condition ends when the vehicles EV's battery charge reaches 100%, that is at line 40
    }


    // Gradual transition to EV or hybrid mode based on vehicle's fuel, EV battery and current speed. The system enters here when vehicle is in combustion only and EV battery
    // reaches 100% or vehicle's fuel is empty
    else if (transitionEV && system_state->evPercentage < ACT_ALONE) { // ACT_ALONE = 1.0

        sprintf(system_state->debg, "EV transition in progress %d                                              ", cont);
        
        // if speed is slower than 45km/h and fuel's not empty (gradual transition is aplyied until ev percentages reaches CHARGE_FULLof use)
        if (current_speed <= MAX_EV_SPEED && system_state->fuel > IS_EMPTY) { // MAX_EV_SPEED == 45.0
            

            // Smoothly transition (+- 0.5% per cicle)
            system_state->evPercentage += HALF_PERCENT; // HALF_PERCENT = 0.005
            system_state->iecPercentage -= HALF_PERCENT;
            system_state->power_mode = HYBRID // Power mode setted to Hybrid
        
            
            // Calculate estimated EV rpm based on current speed and current EV percentage
            double evRpm = system_state->evPercentage * ((current_speed * CIRCUNFERENCE_RATIO ) / TIRE_PERIMETER);
        
            
            // if EV RPM exceeds the limit, recalculate EV percentage and RPM correctly based on current vehicle speed
            if (evRpm > MAX_EV_RPM) {
                system_state->evPercentage = MAX_EV_RPM / ((current_speed * CIRCUNFERENCE_RATIO ) / TIRE_PERIMETER);
                system_state->iecPercentage = ACT_ALONE - system_state->evPercentage;
            }
            
            
            // if EV percentage reaches CHARGE_FULLof use, ends the transition
            if (system_state->evPercentage >= ACT_ALONE) {
                system_state->evPercentage = ACT_ALONE; // ACT_ALONE = 1.0
                system_state->iecPercentage = INACTIVE;
                system_state->power_mode = ELETRIC_ONLY; // // Power mode setted to Eletric only
                transitionEV = false;
            }
        }

        
        // if speed is faster than 45km/h and fuel's not empty (gradual transition is aplyied until ev and iec percentages reaches the correct ratio based on 
        // vehicles current speed)
        else if (current_speed > MAX_EV_SPEED && system_state->fuel > IS_EMPTY) { // MAX_EV_SPEED = 45.0

            
            // Calculate the correct ratio of EV and IEC engines
            double evp  = MAX_EV_SPEED / current_speed;
            double iecp = (current_speed - MAX_EV_SPEED) / current_speed;
            
            
            // Apply smoothly transition 
            system_state->power_mode = HYBRID; 
            system_state->evPercentage  += HALF_PERCENT; // HALF_PERCENT = 0.005
            system_state->iecPercentage -= HALF_PERCENT;
            
            sprintf(system_state->debg, "Hybrid EV heavy %d                                                  ", cont);
            
            
            // Transition ends when EV percentage surpass the correct ratio for the first time
            if (system_state->evPercentage >= evp) {
                
                // Apply the correct ratio
                system_state->evPercentage  = evp;
                system_state->iecPercentage = iecp;
                transitionEV = false;
            }
        }


        // Transition to Eletric only if vehicles fuel is empty
        else if (system_state->fuel == IS_EMPTY) {
        
            if (current_speed > MAX_EV_SPEED) {
                system_state->accelerator = false;
            }
        
            
            // Set EV percentage to CHARGE_FULLwhen vehicles speed is under than 45km/h
            if (current_speed <= MAX_EV_SPEED ) {
                
                system_state->evPercentage  = ACT_ALONE;
                system_state->iecPercentage = INACTIVE;
        
            } 
            
            // Apply smoothly transition to EV if vehicle's speed is above than 45km/h 
            else {
                
                system_state->evPercentage  += 0.02; // TWO_PERCENT = 0.02
                system_state->iecPercentage -= 0.02;

            }

            // Ends transition when EV percentage reaches 100% percent of use            
            if (system_state->evPercentage >= ACT_ALONE) { // ACT_ALONE = 1.0
                transitionEV = false;
                system_state->evPercentage  = ACT_ALONE;
                system_state->iecPercentage = INACTIVE; // INACTIVE = 0.0
            }

            system_state->power_mode = ELETRIC_ONLY; // Eletric only
        }

    }


    // Hybrid driving logic when both sources available
    else if (current_speed > MAX_EV_SPEED && current_battery >= TEN_PERCENT && current_fuel >= IS_EMPTY && !transitionEV) {

        sprintf(system_state->debg, "Car running at standard hybrid mode %d                        ", cont);
        
        if (!transitionIEC) {
            double evp  = MAX_EV_SPEED / current_speed;
            double iecp = (current_speed - MAX_EV_SPEED) / current_speed;
            
            system_state->power_mode = HYBRID            
            
            system_state->evPercentage  = evp;
            system_state->iecPercentage = iecp;

        } 
        
        else {
            system_state->power_mode = COMBUSTION_ONLY; // Combustion only

        }
    }


    else if ((current_speed < MAX_EV_SPEED && current_battery > TEN_PERCENT) && !transitionEV) {
        
        sprintf(system_state->debg, "Car running at EV mode only %d                                ", cont);
        
        system_state->evPercentage  = ACT_ALONE; // 100% of use
        system_state->iecPercentage = INACTIVE; // 0% of use
        system_state->power_mode    = ELETRIC_ONLY; // Eletric only 

    }


    // Activate transition to IEC combustion only when EV battery is lower than 10%
    else if ((current_battery <= TEN_PERCENT && current_fuel > IS_EMPTY) && !transitionEV) {

        if (!transitionIEC) {
            
            // if vehicle is parked, starts with combustion only power mode 
            if (current_speed == PARKED) {
        
                system_state->evPercentage  = INACTIVE; // 0% of use / INACTIVE = 0.0
                system_state->iecPercentage = ACT_ALONE; // 100% of use / ACT_ALONE = 1.0
                system_state->power_mode    = COMBUSTION_ONLY; // Combustion only = 2
                system_state->ev_on         = false;
            } 
            
            // Activate transition to combustion only if vehicles not parked
            else {
                
                transitionIEC = true;
            }
        }
    }

    else {
        // Fallback final else
        sprintf(system_state->debg, "Default engine command %d                                   ", cont);

        if (system_state->fuel == IS_EMPTY && current_speed >= 45 ) {
            system_state->accelerator = false;
        }
        
    }

    // Assign all cmdEV and cmdIEC fields and send once
    cmdEV.globalVelocity = current_speed;
    cmdEV.toVMU = false;
    cmdEV.accelerator = current_accelerator;
    cmdEV.power_level = system_state->evPercentage;
    cmdEV.type = CMD_START;
    cmdEV.iec_fuel = system_state->fuel;

    cmdIEC.globalVelocity = current_speed;
    cmdIEC.toVMU = false;
    cmdIEC.ev_on = system_state->ev_on;
    cmdIEC.power_level = system_state->iecPercentage;
    cmdIEC.type = CMD_START;

    mq_send(ev_mq,  (const char *)&cmdEV,  sizeof(cmdEV),  0);
    mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);

    sem_post(sem); // Release the semaphore
}