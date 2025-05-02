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
#include "iec.h"
#include "../vmu/vmu.h"

// Global variables
SystemState *system_state = MAP_FAILED; // Pointer to the shared memory structure holding the system state
sem_t *sem;                // Pointer to the semaphore for synchronizing access to shared memory
mqd_t iec_mq_receive = (mqd_t)-1;     // Message queue descriptor for receiving commands for the IEC module
volatile sig_atomic_t running = 1; // Flag to control the main loop, volatile to ensure visibility across threads
volatile sig_atomic_t paused = 0;  // Flag to indicate if the simulation is paused
EngineCommand cmd; // Structure to hold the received command
int signal_type = -1;
int shm_fd = -1; //Shared Memory File Descriptor

// Function to handle signals (SIGUSR1 for pause, SIGINT/SIGTERM for shutdown)
void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        paused = !paused;
        printf("[IEC] Paused: %s\n", paused ? "true" : "false");
        signal_type = 0;
    } else if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
        printf("[IEC] Shutting down...\n");
        signal_type = 1;
    }
}

int get_signal(){
    return signal_type;
}

// Function to initialize communication with VMU
int init_communication_iec(char * shared_mem_name, char * semaphore_name, char * iec_queue_name) {
    // Configure signal handlers for graceful shutdown and pause
    signal(SIGUSR1, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Configuration of shared memory for IEC
    shm_fd = shm_open(shared_mem_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[IEC] Error opening shared memory");
        return 0;
    }

    // Map shared memory into IEC's address space
    system_state = (SystemState *)mmap(NULL, sizeof(SystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (system_state == MAP_FAILED) {
        perror("[IEC] Error mapping shared memory");
        return 0;
    }
    close(shm_fd); // Close the file descriptor as the mapping is done

    // Open the semaphore for synchronization (it should already be created by VMU)
    sem = sem_open(semaphore_name, 0);
    if (sem == SEM_FAILED) {
        perror("[IEC] Error opening semaphore");
        return 0;
    }

    // Configuration of POSIX message queue for receiving commands for the IEC module
    struct mq_attr iec_mq_attributes;
    iec_mq_attributes.mq_flags = 0;
    iec_mq_attributes.mq_maxmsg = 10;
    iec_mq_attributes.mq_msgsize = sizeof(EngineCommand);
    iec_mq_attributes.mq_curmsgs = 0;

    iec_mq_receive = mq_open(iec_queue_name, O_RDONLY | O_CREAT | O_NONBLOCK, 0666, &iec_mq_attributes);
    if (iec_mq_receive == (mqd_t)-1) {
        perror("[IEC] Error creating/opening message queue");
        munmap(system_state, sizeof(SystemState));
        sem_close(sem);
        return 0;
    }

    printf("IEC Module Running\n");
    return 1;
}

void receive_cmd() {
    // Receive commands from the VMU through the message queue
    if (mq_receive(iec_mq_receive, (char *)&cmd, sizeof(cmd), NULL) != -1) {
        sem_wait(sem); // Acquire the semaphore to protect shared memory
        // Process the received command
        switch (cmd.type) {
            case CMD_START:
                system_state->iec_on = true;
                printf("[IEC] Motor a Combustão Ligado\n");
                break;
            case CMD_STOP:
                system_state->iec_on = false;
                system_state->rpm_iec = 0; // Reset RPM when stopped
                printf("[IEC] Motor a Combustão Desligado\n");
                break;
            case CMD_SET_POWER:
                // IEC power is controlled by the transition factor from VMU
                system("clear");
                printf("[IEC] Received SET_POWER command (power level: %.2f)\n", cmd.power_level);
                break;
            case CMD_END:
                running = 0; // Terminate the main loop
                break;
            default:
                fprintf(stderr, "[IEC] Comando desconhecido recebido\n");
                break;
        }
        sem_post(sem); // Release the semaphore
    }
}

void engine() {
    sem_wait(sem); // Acquire the semaphore for updating engine state
    // Simulate IEC engine behavior
    if (system_state->iec_on) {
        // Increase RPM based on the transition factor received from VMU
        system_state->rpm_iec = (int)(system_state->transition_factor * 5000);
        // Increase temperature based on the transition factor
        system_state->temp_iec += system_state->transition_factor * 0.1;
    } else {
        system_state->rpm_iec = 0; // Set RPM to 0 when off
        // Cool down the engine if it's above the ambient temperature
        if (system_state->temp_iec > 25.0) {
            system_state->temp_iec -= 0.02;
        }
    }
    sem_post(sem); // Release the semaphore
}

// Function to handle the engine behavior based on the received command

// Function to cleanup resources before exiting
void cleanup() {
    // Cleanup resources before exiting
    mq_close(iec_mq_receive);
    munmap(system_state, sizeof(SystemState));
    sem_close(sem);

    printf("[IEC] Shut down complete.\n");
}

