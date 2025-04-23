#include "check_queue.h"

// This function verify if the message received is to VMU and update the system values from EV and IEC
void vmu_check_queue (unsigned char counter, mqd_t mqd, bool ev) {
    
    unsigned char localcount = 0;
    unsigned char ret;
    EngineCommandEV cmdEV;
    EngineCommandIEC cmdIEC;
    
    sem_wait(sem);

    // Receive message from EV
    if (ev) {

        // Wait for message
        while (mq_receive(mqd, (char *)&cmdEV, sizeof(cmdEV), NULL) != -1) {
            
            if (cmdEV.toVMU) {
                localcount++;
                strcpy(lastmsg, cmdEV.check);
                system_state->rpm_ev = cmdEV.rpm_ev;
                system_state->battery = cmdEV.batteryEV;
                system_state->ev_on = cmdEV.evActive;  
                safetyCount = 0;
            }

            else {
                safetyCount += 1;
            } 
        }

        if (localcount == 0 && cmdEV.toVMU) {
            strcpy(lastmsg, "nt");
            safetyCount++;
            if (safetyCount == 5) {
                system_state->safety = true;
            }
            
        }
        
        else if (!cmdEV.toVMU) {
            mq_send(mqd, (const char *)&cmdEV, sizeof(cmdEV), 0);
        }
    }
    
    else {

        while (mq_receive(mqd, (char *)&cmdIEC, sizeof(cmdIEC), NULL) != -1) {
            if (cmdIEC.toVMU) {
                localcount++;
                strcpy(lastmsg, cmdIEC.check);
                system_state->rpm_iec = cmdIEC.rpm_iec;
                system_state->fuel = cmdIEC.fuelIEC;
                system_state->iec_on = cmdIEC.iecActive;
                safetyCount = 0;
                
                if (system_state->fuel < 0.0) {
                    system_state->fuel = 0.0;
                
                }

            } 
        }

        if (localcount == 0 && cmdIEC.toVMU) {
            strcpy(lastmsg, "nt");
            safetyCount++;
            if (safetyCount == 5) {
                system_state->safety = true;
            }
            
        }

        else if (!cmdIEC.toVMU) {
            mq_send(mqd, (const char *)&cmdIEC, sizeof(cmdIEC), 0);
        }
    }
    sem_post(sem);

}