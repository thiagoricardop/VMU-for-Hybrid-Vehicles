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
#define TIMEOUT_SECONDS 5

struct timespec get_abs_timeout(int seconds) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += seconds;
    return ts;
}

/*
To use the VMU, you need first to make the executable files using `make` on VMU-FOR-HYBRID-VEHICLES/src terminal. After that, run each file in a different terminal, starting by ./vmu, then the ./ev and ./iec.
The vmu.c is the main module that controls the vehicle's powertrain, while the ev.c and iec.c are the electric and internal combustion engine modules, respectively.
The VMU communicates with the EV and IEC modules through POSIX message queues, and controls the engines based on the current speed and user input.
*/

/*
After running all executables in different terminals, go to the vmu terminal, press `1` to accelerate and press `enter`.
*/

/*
To shut down the system, press `Ctrl+C` on vmu terminal.
*/

// Global variables
SystemState *system_state; // Pointer to the shared memory structure holding the system state
sem_t *sem;                // Pointer to the semaphore for synchronizing access to shared memory
mqd_t ev_mq, iec_mq;      // Message queue descriptors for communication with EV and IEC modules
volatile sig_atomic_t running = 1; // Flag to control the main loop, volatile to ensure visibility across threads
volatile sig_atomic_t paused = 0;  // Flag to indicate if the simulation is paused
unsigned char cont = 0;
char lastmsg[35];
unsigned char safetyCount = 0;
int ciclesQuantity;
double iecTransitionRatio;
double evTransitionRatio;
bool transitionIEC = false;
bool transitionEV = false;
long int elapsed = 0;
long int remaining = 0;
bool start = true;
long elapsed;
long delay_ms;
int transition = 0;
double expectedvalueEV = 0;
double expectedValueIEC = 0;
bool finish = false;
bool carStop = false;

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

// Function to display the current state of the system
void display_status(const SystemState *state) {
    system("clear");
    printf("=== Estado do Sistema ===\n");
    printf("Speed: %.2f km/h\n", state->speed);
    printf("RPM EV: %d\n", state->rpm_ev);
    printf("RPM IEC: %d\n", state->rpm_iec);
    printf("Eletric engine ratio: %f \n", state->evPercentage);
    printf("Combustion engine ratio: %f \n", state->iecPercentage);
    printf("EV: %s\n", state->ev_on ? "ON" : "OFF");
    printf("IEC: %s\n", state->iec_on ? "ON" : "OFF");
    printf("Temperature EV: %.2f C\n", state->temp_ev);
    printf("Temperature IEC: %.2f C\n", state->temp_iec);
    printf("Battery: %f%%\n", state->battery);
    printf("Fuel (liters): %f\n", state->fuel);
    printf("Power mode: %s\n", 
           state->power_mode == 0 ? "Electric Only" : 
           state->power_mode == 1 ? "Hybrid" : 
           state->power_mode == 2 ? "Combustion Only" :
           state->power_mode == 3 ? "Regenerative Braking" : "Parked");
    printf("Accelerator: %s\n", state->accelerator ? "ON" : "OFF");
    printf("Brake: %s\n", state->brake ? "ON" : "OFF");
    printf("Counter: %d\n", cont);
    printf("Last mensage: %s", state->debg);
    printf("\nTransition EV: %s", transitionEV ? "ON" : "OFF" );
    printf("\nType `1` for accelerate, `2` for brake, or `0` for none, and press Enter:\n");
    cont++;
}

// Function to initialize the system state
void init_system_state(SystemState *state) {
    state->accelerator = false;
    state->brake = false;
    state->speed = MIN_SPEED;
    state->rpm_ev = 0;
    state->rpm_iec = 0;
    state->ev_on = false;
    state->iec_on = false;
    state->temp_ev = 25.0;
    state->temp_iec = 25.0;
    state->battery = MAX_BATTERY;
    state->fuel = MAX_FUEL;
    state->power_mode = 5; // Parked mode initially
    state->transition_factor = 0.0;
    state->transitionCicles = 0;
    strcpy(state->debg, "Nops");
}

// Function to set the accelerator state
void set_acceleration(bool accelerate) {
    sem_wait(sem); // Acquire the semaphore to protect shared memory
    system_state->accelerator = accelerate;
    if (accelerate) {
        system_state->brake = false; // If accelerating, brake is off
    }
    sem_post(sem); // Release the semaphore
}

// Function to set the braking state
void set_braking(bool brake) {
    sem_wait(sem); // Acquire the semaphore
    system_state->brake = brake;
    if (brake) {
        system_state->accelerator = false; // If braking, accelerator is off
    }
    sem_post(sem); // Release the semaphore
}

int calculateCicleEstimated( SystemState *state ) {
    double localSpeed = 0;
    int cicles = 0;

    while(localSpeed <= state->speed) {
        cicles++;
        localSpeed = cicles * (1.13 - (0.00162 * cicles));
        if ( cicles == 350 ) {
            break;
        }

    }

    return cicles;
}

// Function to calculate the current speed based on engine RPM and transition factor
void calculate_speed(SystemState *state) {
    double fator_variacao = 0.0;
    sem_wait(sem); // Acquire the semaphore
    
    if (state->accelerator) {
        ciclesQuantity = calculateCicleEstimated(state);
        // Calculate speed increase based on RPM of both engines and the transition factor
        state->speed = ciclesQuantity*(1.13 - (0.00162 * ciclesQuantity));
        
        if (state->fuel == 0 && state->speed > 70.0) {
            state->speed = 70.0;
        }
        /*
        // Calculate speed increase based on RPM of both engines and the transition factor
        fator_variacao = (state->rpm_ev * (1.0 - state->transition_factor) + state->rpm_iec * state->transition_factor) * 0.0001;
        state->speed = fmin(state->speed + fator_variacao, MAX_SPEED); // Ensure speed does not exceed the maximum
        */
    } 
    
    else if (!state->brake) {
        // Coasting - apply a slight deceleration
        state->speed = fmax(state->speed - 0.3, MIN_SPEED); // Ensure speed does not go below the minimum
    } 
    
    else {
        // Braking - apply a stronger deceleration
        double fator_variacao_freio = 2.0;
        state->speed = fmax(state->speed - fator_variacao_freio, MIN_SPEED);
    }

    sem_post(sem); // Release the semaphore
    
}

// Function to control the engines based on the current speed and system state
void vmu_control_engines() {
    EngineCommandEV cmdEV;
    EngineCommandIEC cmdIEC;
    
    sem_wait(sem); // Acquire the semaphore
    double current_speed = system_state->speed;
    double current_battery = system_state->battery;
    double current_fuel = system_state->fuel;
    bool current_accelerator = system_state->accelerator;
    double evPercentage = system_state->evPercentage;
    double iecPercentage = system_state->iecPercentage;

    if (current_battery == 0.0 && current_fuel == 0.0) {
        current_accelerator = false;
        carStop = true;
    }

    if ( carStop && current_battery >= 100.0 ) {
        
        carStop = false;

        system_state->transition_factor = 0.0;
        cmdEV.globalVelocity = current_speed;
        cmdEV.power_level = system_state->evPercentage;
        cmdEV.type = CMD_START;
        cmdEV.toVMU = false;
        cmdEV.accelerator = current_accelerator;

        cmdIEC.type = CMD_START;
        cmdIEC.globalVelocity = current_speed;
        cmdIEC.power_level = system_state->iecPercentage;
        cmdIEC.toVMU = false;
        cmdIEC.ev_on = system_state->ev_on;
        
        mq_send(ev_mq, (const char *)&cmdEV, sizeof(cmdEV), 0);
        mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);
    }


    else if (current_battery > 0.0 && current_fuel == 0.0 && !transitionEV && system_state->evPercentage < 1.0 ) {

        sprintf(system_state->debg, "To no if para tev se fuel 0 %d", cont);

        transitionEV = true;

        system_state->transition_factor = 0.0;
        cmdEV.globalVelocity = current_speed;
        cmdEV.power_level = system_state->evPercentage;
        cmdEV.type = CMD_START;
        cmdEV.toVMU = false;
        cmdEV.accelerator = current_accelerator;

        cmdIEC.type = CMD_START;
        cmdIEC.globalVelocity = current_speed;
        cmdIEC.power_level = system_state->iecPercentage;
        cmdIEC.toVMU = false;
        cmdIEC.ev_on = system_state->ev_on;
        
        mq_send(ev_mq, (const char *)&cmdEV, sizeof(cmdEV), 0);
        mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);
    }
    
    
    else if (transitionIEC && current_battery == 100) {
        
        transitionIEC = false;
        transitionEV = true;
        transition = 300;
    
    }

    if (transitionEV && system_state->evPercentage < 1.0) {

        sprintf(system_state->debg, "To no if TEV %d", cont);

        evPercentage = system_state->evPercentage;
        iecPercentage = system_state->iecPercentage;

        if(current_speed <= 70.0 && system_state->fuel > 0.0) {
            
            system_state->evPercentage += 0.01;
            system_state->iecPercentage -= 0.01;
            system_state->power_mode = 1;
            
            if (evPercentage >= 1) {
    
                system_state->evPercentage = 1.0;
                system_state->iecPercentage = 0;
                system_state->power_mode = 0;
                
                transitionEV = false;
            }
        }
        
        else if (current_speed > 70.0 && system_state->fuel > 0.0) {
            evTransitionRatio = (70.0/current_speed);
            iecTransitionRatio = ((current_speed - 70.0)/current_speed);
        
            
            system_state->power_mode = 1;
            system_state->evPercentage += 0.01;
            system_state->iecPercentage -= 0.01;
            

            sprintf(system_state->debg, "To no else (TEV HYB): %d", cont);
            
            if(system_state->evPercentage >= evTransitionRatio) {
            
                evPercentage = evTransitionRatio;
                iecPercentage = iecTransitionRatio;
                transitionEV = false;
            }
            
        }

        else if (system_state->fuel == 0.0 && current_speed > 70.0) {
            system_state->accelerator = false;
            if (current_speed*(system_state->evPercentage) <= 70.0) {
                system_state->power_mode = 1;
                system_state->evPercentage += 0.01;
                system_state->iecPercentage -= 0.01;
            }

            if(system_state->evPercentage == 1.0) {
                transitionEV = false;
                system_state->power_mode = 0;
            } 
        }

        if (system_state->evPercentage > 0.9) {
            system_state->evPercentage = 1.0;
            system_state->iecPercentage = 0.0;
            system_state->power_mode = 0;

            transitionEV = false;
        }

        
        cmdEV.power_level = evPercentage; // EV power decreases as transition factor increases
        cmdEV.globalVelocity = current_speed;
        cmdEV.type = CMD_START; 
        cmdEV.toVMU = false;
        cmdEV.accelerator = current_accelerator;
        mq_send(ev_mq, (const char *)&cmdEV, sizeof(cmdEV), 0);

        cmdIEC.power_level = iecPercentage; // IEC power increases as transition factor increases
        cmdIEC.globalVelocity = current_speed;
        cmdIEC.type = CMD_START; 
        cmdIEC.toVMU = false;
        cmdIEC.ev_on = system_state->ev_on;
        mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);
        
    }


    // Logic for controlling engine transitions based on speed and user input
    else if ((current_speed > 70.0 && current_battery >= 10.0 && current_fuel >= 0.0) && !transitionEV) {
        sprintf(system_state->debg, "To no else hybrid padrao for di if %d", cont);
        if (transitionIEC == false) {

            float evp = 70.0/current_speed;
            float iecp = (current_speed - 70.0)/current_speed;
            // Send power commands to EV and IEC based on the transition factor

            sprintf(system_state->debg, "To no else hybrid padrao %d", cont);
            
            system_state->power_mode = 1;
            system_state->evPercentage = evp;
            system_state->iecPercentage = iecp;
            

            cmdEV.power_level = evp; // EV power decreases as transition factor increases
            cmdEV.globalVelocity = current_speed;
            cmdEV.type = CMD_START; 
            cmdEV.toVMU = false;
            cmdEV.accelerator = current_accelerator;
            mq_send(ev_mq, (const char *)&cmdEV, sizeof(cmdEV), 0);

            cmdIEC.power_level = (current_speed - 70.0)/current_speed; // IEC power increases as transition factor increases
            cmdIEC.globalVelocity = current_speed;
            cmdIEC.type = CMD_START; 
            cmdIEC.toVMU = false;
            cmdIEC.ev_on = system_state->ev_on;
            mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);
            
        }
        
        else {

            system_state->power_mode = 2;
            cmdEV.power_level = 0.0; // EV power decreases as transition factor increases
            cmdEV.globalVelocity = current_speed;
            cmdEV.type = CMD_START; 
            cmdEV.toVMU = false;
            cmdEV.accelerator = current_accelerator;
            mq_send(ev_mq, (const char *)&cmdEV, sizeof(cmdEV), 0);

            cmdIEC.power_level = 1.0; // IEC power increases as transition factor increases
            cmdIEC.globalVelocity = current_speed;
            cmdIEC.type = CMD_START; 
            cmdIEC.toVMU = false;
            cmdIEC.ev_on = system_state->ev_on;
            mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);
        }
          
    } 
    
    else if ((current_speed < 70.0 && current_battery > 10.0)  && !transitionEV) {
        
        sprintf(system_state->debg, "else currente speed < 70 fora do if: %d", cont);

        if(transitionIEC == false) {
            
            sprintf(system_state->debg, "To no else currente speed < 70: %d", cont);

            
            system_state->evPercentage = 1.0;
            system_state->iecPercentage = 0.0;
            system_state->power_mode = 0.0;
            

            cmdEV.type = CMD_START;
            cmdEV.power_level = 1.0;
            cmdEV.globalVelocity = current_speed;
            cmdEV.toVMU = false;
            cmdEV.accelerator = current_accelerator;
            mq_send(ev_mq, (const char *)&cmdEV, sizeof(cmdEV), 0);

            cmdIEC.type = CMD_STOP;
            cmdIEC.power_level = 0.0;
            cmdIEC.globalVelocity = current_speed;
            cmdIEC.toVMU = false;
            cmdIEC.ev_on = system_state->ev_on;
            mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);

        }

        else if (system_state->fuel == 0.0) {

            sprintf(system_state->debg, "To no else currente speed < 70 22: %d", cont);
            system_state->evPercentage = 1.0;
            system_state->iecPercentage = 0.0;
            system_state->power_mode = 0.0;
            

            cmdEV.type = CMD_START;
            cmdEV.power_level = 1.0;
            cmdEV.globalVelocity = current_speed;
            cmdEV.toVMU = false;
            cmdEV.accelerator = current_accelerator;
            mq_send(ev_mq, (const char *)&cmdEV, sizeof(cmdEV), 0);

            cmdIEC.type = CMD_STOP;
            cmdIEC.power_level = 0.0;
            cmdIEC.globalVelocity = current_speed;
            cmdIEC.toVMU = false;
            cmdIEC.ev_on = system_state->ev_on;
            mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);
        }

    } 

    
    // Emergency mode: If battery is low, switch to IEC if fuel is available

    else if ((current_battery <= 10.0 && current_fuel > 0.0 || (transitionIEC && current_fuel > 0.0)) && !transitionEV) {
        
        if (transitionIEC == false) {
            transitionIEC = true;
        }

        sprintf(system_state->debg, "To no else TIEC %d", cont);
            
        system_state->evPercentage -= 0.01;
        system_state->iecPercentage += 0.01;
        system_state->power_mode = 1;
        
        if (iecPercentage >= 1) {

            system_state->evPercentage = 0.0;
            system_state->iecPercentage = 1.0;
            system_state->power_mode = 2;
            system_state->ev_on = false;

        }

        
        cmdEV.power_level = evPercentage; // EV power decreases as transition factor increases
        cmdEV.globalVelocity = current_speed;
        cmdEV.type = CMD_START; 
        cmdEV.toVMU = false;
        cmdEV.accelerator = current_accelerator;
        mq_send(ev_mq, (const char *)&cmdEV, sizeof(cmdEV), 0);

        cmdIEC.power_level = iecPercentage; // IEC power increases as transition factor increases
        cmdIEC.globalVelocity = current_speed;
        cmdIEC.type = CMD_START; 
        cmdIEC.toVMU = false;
        cmdIEC.ev_on = system_state->ev_on;
        mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);
    } 

    else if (current_fuel <= 2.25 && current_battery >= 10.0 && system_state->evPercentage != 1.0) {
        
        sprintf(system_state->debg, "To no ultimo else %d", cont);

        transitionEV = true;
        system_state->evPercentage = 0;

        cmdEV.power_level = system_state->evPercentage;
        cmdEV.globalVelocity = current_speed;
        cmdEV.type = CMD_START;
        cmdEV.toVMU = false;
        cmdEV.accelerator = current_accelerator;
        mq_send(ev_mq, (const char *)&cmdEV, sizeof(cmdEV), 0);

        system_state->iecPercentage = 1.0;

        cmdIEC.power_level = system_state->iecPercentage;
        cmdIEC.globalVelocity = current_speed;
        cmdIEC.type = CMD_START;
        cmdIEC.toVMU = false;
        cmdIEC.ev_on = system_state->ev_on;
        mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);

        system_state->transitionCicles = 0.0;

        system_state->power_mode = 2;
    }

    else {

        sprintf(system_state->debg, "To no ultimo else 2 %d", cont);

        cmdEV.power_level = system_state->evPercentage;
        cmdEV.globalVelocity = current_speed;
        cmdEV.type = CMD_START;
        cmdEV.toVMU = false;
        cmdEV.accelerator = current_accelerator;
        mq_send(ev_mq, (const char *)&cmdEV, sizeof(cmdEV), 0);

        cmdIEC.power_level = system_state->iecPercentage;
        cmdIEC.globalVelocity = current_speed;
        cmdIEC.type = CMD_START;
        cmdIEC.toVMU = false;
        cmdIEC.ev_on = system_state->ev_on;
        mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);
    
    }

/*
    else if ( current_fuel == 0.0 && current_battery <= 20.0 ) {

        system_state->evPercentage = 0.0;

        cmdEV.power_level = system_state->evPercentage;
        cmdEV.globalVelocity = current_speed;
        cmdEV.type = CMD_START;
        cmdEV.toVMU = false;
        cmdEV.accelerator = current_accelerator;
        mq_send(ev_mq, (const char *)&cmdEV, sizeof(cmdEV), 0);

        system_state->iecPercentage = 0.0;

        cmdIEC.power_level = system_state->iecPercentage;
        cmdIEC.globalVelocity = current_speed;
        cmdIEC.type = CMD_START;
        cmdIEC.toVMU = false;
        cmdIEC.ev_on = system_state->ev_on;
        mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);

        system_state->transitionCicles = 0;

        system_state->power_mode = 4;
    }
*/
/*
    else if (current_battery <= 5.0 && current_fuel <= 2.25) {

        system_state->accelerator = false;
        finish = true;
   
        // If both battery and fuel are low, stop both engines (for safety or simulation end)
        system_state->transition_factor = 0.0;
        cmdEV.type = CMD_SET_POWER;
        cmdEV.power_level = 0.0;
        cmdEV.toVMU = false;
        cmdEV.accelerator = current_accelerator;
        
        cmdIEC.type = CMD_SET_POWER;
        cmdIEC.power_level = 0.0;
        cmdIEC.toVMU = false;
        
        mq_send(ev_mq, (const char *)&cmdEV, sizeof(cmdEV), 0);
        mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);
        
    }
*/
    sem_post(sem); // Release the semaphore
}

// Function to read user input for pedal control in a separate thread
void *read_input(void *arg) {
    char input[10];
    while (running) {
        fgets(input, sizeof(input), stdin);
        // Remove trailing newline character from input
        input[strcspn(input, "\n")] = 0;

        // Process the user input to control acceleration and braking
        if (strcmp(input, "0") == 0 && !finish) {
            set_braking(false);
            set_acceleration(false);
            sem_wait(sem);
            system_state->iec_on = false;
            system_state->ev_on = false;
            sem_post(sem);
        } else if (strcmp(input, "1") == 0 && !finish) {
            set_acceleration(true);
            set_braking(false);
        } else if (strcmp(input, "2") == 0 ) {
            set_acceleration(false);
            set_braking(true);
            sem_wait(sem);
            system_state->iec_on = false;
            system_state->ev_on = false;
            sem_post(sem);
        }
        usleep(10000); // Small delay to avoid busy-waiting
    }
    return NULL;
}

void vmu_check_queue (unsigned char counter, mqd_t mqd, bool ev) {
    
    unsigned char localcount = 0;
    unsigned char ret;
    EngineCommandEV cmdEV;
    EngineCommandIEC cmdIEC;
    
    sem_wait(sem);
    if (ev) {
        while (mq_receive(mqd, (char *)&cmdEV, sizeof(cmdEV), NULL) != -1) {
            if (cmdEV.toVMU) {
                localcount++;
                strcpy(lastmsg, cmdEV.check);
                system_state->rpm_ev = cmdEV.rpm_ev;
                system_state->battery = cmdEV.batteryEV;
                system_state->ev_on = cmdEV.evActive;  
                safetyCount = 0;
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

int main() {

    struct timeval start, end;

    // Configure signal handlers for graceful shutdown and pause
    signal(SIGUSR1, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    unsigned char counterEV = 0;
    unsigned char counterIEC = 0;

    EngineCommandEV cmdEV;
    EngineCommandIEC cmdIEC;


    strcpy(lastmsg, "");

    shm_unlink(SHARED_MEM_NAME); 

    // Configuration of shared memory for VMU
    int shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[VMU] Error opening shared memory");
        exit(EXIT_FAILURE);
    }

    // Configure the size of the shared memory
    if (ftruncate(shm_fd, sizeof(SystemState)) == -1) {
        perror("[VMU] Error configuring shared memory size");
        exit(EXIT_FAILURE);
    }

    // Map shared memory into VMU's address space
    system_state = (SystemState *)mmap(NULL, sizeof(SystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (system_state == MAP_FAILED) {
        perror("[VMU] Error mapping shared memory");
        exit(EXIT_FAILURE);
    }
    

    sem_unlink(SEMAPHORE_NAME);

    // Create semaphore for synchronization
    sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("[VMU] Error creating semaphore");
        exit(EXIT_FAILURE);
    }

    // Initialize system state
    init_system_state(system_state);

    mq_unlink(EV_COMMAND_QUEUE_NAME);

    // Configuration of POSIX message queues for communication with the EV module
    struct mq_attr ev_mq_attributes;
    ev_mq_attributes.mq_flags = 0;
    ev_mq_attributes.mq_maxmsg = 10;
    ev_mq_attributes.mq_msgsize = sizeof(EngineCommandEV);
    ev_mq_attributes.mq_curmsgs = 0;

    ev_mq = mq_open(EV_COMMAND_QUEUE_NAME, O_RDWR| O_CREAT | O_NONBLOCK, 0666, &ev_mq_attributes);
    if (ev_mq == (mqd_t)-1) {
        perror("[VMU] Error creating/opening EV message queue");
        munmap(system_state, sizeof(SystemState));
        shm_unlink(SHARED_MEM_NAME);
        sem_close(sem);
        sem_unlink(SEMAPHORE_NAME);
        exit(EXIT_FAILURE);
    }

    mq_unlink(IEC_COMMAND_QUEUE_NAME);

    // Configuration of POSIX message queues for communication with the IEC module
    struct mq_attr iec_mq_attributes;
    iec_mq_attributes.mq_flags = 0;
    iec_mq_attributes.mq_maxmsg = 10;
    iec_mq_attributes.mq_msgsize = sizeof(EngineCommandIEC);
    iec_mq_attributes.mq_curmsgs = 0;

    iec_mq = mq_open(IEC_COMMAND_QUEUE_NAME, O_RDWR | O_CREAT | O_NONBLOCK, 0666, &iec_mq_attributes);
    if (iec_mq == (mqd_t)-1) {
        perror("[VMU] Error creating/opening IEC message queue");
        mq_close(ev_mq);
        mq_unlink(EV_COMMAND_QUEUE_NAME);
        munmap(system_state, sizeof(SystemState));
        shm_unlink(SHARED_MEM_NAME);
        sem_close(sem);
        sem_unlink(SEMAPHORE_NAME);
        exit(EXIT_FAILURE);
    }

    printf("VMU Module Running\n");

    // Create a separate thread to read user input for pedal control
    pthread_t input_thread;
    if (pthread_create(&input_thread, NULL, read_input, NULL) != 0) {
        perror("[VMU] Error creating input thread");
        running = 0; // Exit main loop if thread creation fails
    }
 
    system_state->transitionCicles = 0;

    printf("\n\nVMU Module Running Debug 1\n");
    
    printf("\n\nVMU Module Running Debug 3\n");

    // Main loop of the VMU module
    while (running) {
        if (!paused) {
            
            gettimeofday(&start, NULL);
            
            vmu_control_engines(); 
            calculate_speed(system_state);
            vmu_check_queue(counterEV, ev_mq, true);
            vmu_check_queue(counterIEC, iec_mq, false);
            display_status(system_state);  // Display the current system status

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

    // Cleanup resources before exiting
    cmdEV.type = CMD_END;
    cmdIEC.type = CMD_END;
    cmdEV.toVMU = false;
    cmdIEC.toVMU = false;
    mq_send(ev_mq, (const char *)&cmdEV, sizeof(cmdEV), 0);
    mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);
    pthread_cancel(input_thread); // Request the input thread to terminate
    pthread_join(input_thread, NULL); // Wait for the input thread to finish

    mq_close(ev_mq);
    mq_unlink(EV_COMMAND_QUEUE_NAME);
    mq_close(iec_mq);
    mq_unlink(IEC_COMMAND_QUEUE_NAME);
    munmap(system_state, sizeof(SystemState));
    shm_unlink(SHARED_MEM_NAME);
    sem_close(sem);
    sem_unlink(SEMAPHORE_NAME);

    printf("[VMU] Shut down complete.\n");
    return 0;
}
