// Internal Combustion Engine (IEC) module for the Vehicle Management Unit (VMU) system.

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

#include "iec.h"
#include "../vmu/vmu.h"


// Ponteiros de recurso compartilhado
SystemState *system_state = NULL;
EngineCommandIEC cmd;
sem_t *sem = NULL;
mqd_t iec_mq = (mqd_t)-1;

// Flags de controle de execução
volatile sig_atomic_t running = 1;
volatile sig_atomic_t paused  = 0;

// Estados e parâmetros do motor de combustão (IEC)
double fuel = 45.0;
double iecPercentage = 0.0;
double localVelocity = 0.0;
bool iecActive = false;
double iecRPM = 0.0;

// Consumo médio e relações de marcha
const double averageConsumeKMl = 14.7;
const float gearRatio[5] = { 3.83f, 2.36f, 1.69f, 1.31f, 1.00f };
double tireCircunferenceRatio = 2.19912;

// Contadores e índices
unsigned char gear = 0;
unsigned char counter = 0;

// Estado do motor elétrico
bool ev_on = false;

// Function to handle signals (SIGUSR1 for pause, SIGINT/SIGTERM for shutdown)
void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        paused = !paused;
        printf("[IEC] Paused: %s\n", paused ? "true" : "false");
    } else if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
        printf("[IEC] Shutting down...\n");
    }
}

void iec_initializer() {

    // Configuration of shared memory for IEC
    int shm_fd = shm_open(SHARED_MEM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[IEC] Error opening shared memory");
        exit(EXIT_FAILURE);
    }

    // Map shared memory into IEC's address space
    system_state = (SystemState *)mmap(NULL, sizeof(SystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (system_state == MAP_FAILED) {
        perror("[IEC] Error mapping shared memory");
        exit(EXIT_FAILURE);
    }
    close(shm_fd); // Close the file descriptor as the mapping is done

    // Open the semaphore for synchronization (it should already be created by VMU)
    sem = sem_open(SEMAPHORE_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("[IEC] Error opening semaphore");
        exit(EXIT_FAILURE);
    }

    // Configuration of POSIX message queue for receiving commands for the IEC module
    struct mq_attr iec_mq_attributes;
    iec_mq_attributes.mq_flags = 0;
    iec_mq_attributes.mq_maxmsg = 10;
    iec_mq_attributes.mq_msgsize = sizeof(EngineCommandIEC);
    iec_mq_attributes.mq_curmsgs = 0;

    iec_mq = mq_open(IEC_COMMAND_QUEUE_NAME, O_RDWR | O_CREAT | O_NONBLOCK, 0666, &iec_mq_attributes);
    if (iec_mq == (mqd_t)-1) {
        perror("[IEC] Error creating/opening message queue");
        munmap(system_state, sizeof(SystemState));
        sem_close(sem);
        exit(EXIT_FAILURE);
    }

}

EngineCommandIEC iec_receive (EngineCommandIEC cmd) {
    // Receive commands from the VMU through the message queue
    while (mq_receive(iec_mq, (char *)&cmd, sizeof(cmd), NULL) == -1) {

    }

    return cmd;
}


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

void iecCleanUp () {
    mq_close(iec_mq);
    munmap(system_state, sizeof(SystemState));
    sem_close(sem);
}