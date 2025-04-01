// Electric Vehicle (EV) module for the Vehicle Management Unit (VMU) system.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <mqueue.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include "ev.h"
#include "../VMU/vmu.h"

// Global variables
SystemState *system_state; // Pointer to the shared memory structure holding the system state
sem_t *sem;                // Pointer to the semaphore for synchronizing access to shared memory
mqd_t ev_mq;      // Message queue descriptor for receiving commands for the EV module
volatile sig_atomic_t running = 1; // Flag to control the main loop, volatile to ensure visibility across threads
volatile sig_atomic_t paused = 0;  // Flag to indicate if the simulation is paused
double BatteryEV = 100.0;
double evPercentage = 0.0;
bool firstReceive = true;
double localVelocity = 0.0;
double lastLocalVelocity = 0.0;
double distance = 3000;
double rpmEV;
double tireCircunferenceRatio = 2.19912;
bool evActive;
bool accelerator;
unsigned char counter = 0;

// Function to handle signals (SIGUSR1 for pause, SIGINT/SIGTERM for shutdown)
void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        paused = !paused;
        printf("[EV] Paused: %s\n", paused ? "true" : "false");
    } else if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
        printf("[EV] Shutting down...\n");
    }
}

void calculateValues () {

    if (!accelerator && localVelocity != 0.0) {
        BatteryEV += 1.0;
    }

    else if (evActive) {
        BatteryEV -= 0.01;
    }

    if (BatteryEV < 0.0) {
        BatteryEV = 0.0;
    } 

    if (BatteryEV > 100.0) {
        BatteryEV = 100.0;
    }
    lastLocalVelocity = localVelocity;
    
    rpmEV = evPercentage*((localVelocity * 16.67) / tireCircunferenceRatio);
    
}

int main() {
    // Configure signal handlers for graceful shutdown and pause
    signal(SIGUSR1, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Configuration of shared memory for EV
    int shm_fd = shm_open(SHARED_MEM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[EV] Error opening shared memory");
        exit(EXIT_FAILURE);
    }

    // Map shared memory into EV's address space
    system_state = (SystemState *)mmap(NULL, sizeof(SystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (system_state == MAP_FAILED) {
        perror("[EV] Error mapping shared memory");
        exit(EXIT_FAILURE);
    }
    close(shm_fd); // Close the file descriptor as the mapping is done

    // Open the semaphore for synchronization (it should already be created by VMU)
    sem = sem_open(SEMAPHORE_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("[EV] Error opening semaphore");
        exit(EXIT_FAILURE);
    }

    // Configuration of POSIX message queue for receiving commands for the EV module
    struct mq_attr ev_mq_attributes;
    ev_mq_attributes.mq_flags = 0;
    ev_mq_attributes.mq_maxmsg = 10;
    ev_mq_attributes.mq_msgsize = sizeof(EngineCommandEV);
    ev_mq_attributes.mq_curmsgs = 0;

    ev_mq = mq_open(EV_COMMAND_QUEUE_NAME, O_RDWR | O_CREAT | O_NONBLOCK, 0666, &ev_mq_attributes);
    if (ev_mq == (mqd_t)-1) {
        perror("[EV] Error creating/opening message queue");
        munmap(system_state, sizeof(SystemState));
        sem_close(sem);
        exit(EXIT_FAILURE);
    }

    printf("EV Module Running\n");

    EngineCommandEV cmd; // Structure to hold the received command
    // Main loop of the EV module
    while (running) {
        if (!paused) {

            while (mq_receive(ev_mq, (char *)&cmd, sizeof(cmd), NULL) == -1) {

            }
         
            sem_wait(sem); // Acquire the semaphore to protect shared memory
            if (!cmd.toVMU) {
            // Receive commands from the VMU through the message queue
                // Process the received command
                
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

                if (evPercentage != 0) {
                    evActive = true;                
                }
                else {
                    evActive = false;            
                }

                calculateValues();

                system("clear");
                printf("\nEV usage percentage: %f", evPercentage);
                cmd.batteryEV = BatteryEV;
                cmd.evActive = evActive;
                cmd.rpm_ev = rpmEV;
                sprintf(cmd.check, "Bateria enviada: %f", BatteryEV);
                cmd.toVMU = true;

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

                mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
           
            }
            
            else {
                mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
       
            } 
            sem_post(sem);   
            
        } else {
            sleep(1); // Sleep for 1 second if paused
        }
    }

    // Cleanup resources before exiting
    mq_close(ev_mq);
    munmap(system_state, sizeof(SystemState));
    sem_close(sem);

    printf("[EV] Shut down complete.\n");
    return 0;
}
