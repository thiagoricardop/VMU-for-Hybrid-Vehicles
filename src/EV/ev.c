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
#include "ev.h"
#include "../VMU/vmu.h"

// Global variables
SystemState *system_state; // Pointer to the shared memory structure holding the system state
sem_t *sem;                // Pointer to the semaphore for synchronizing access to shared memory
mqd_t ev_mq_receive;      // Message queue descriptor for receiving commands for the EV module
volatile sig_atomic_t running = 1; // Flag to control the main loop, volatile to ensure visibility across threads
volatile sig_atomic_t paused = 0;  // Flag to indicate if the simulation is paused

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
    ev_mq_attributes.mq_msgsize = sizeof(EngineCommand);
    ev_mq_attributes.mq_curmsgs = 0;

    ev_mq_receive = mq_open(EV_COMMAND_QUEUE_NAME, O_RDONLY | O_CREAT | O_NONBLOCK, 0666, &ev_mq_attributes);
    if (ev_mq_receive == (mqd_t)-1) {
        perror("[EV] Error creating/opening message queue");
        munmap(system_state, sizeof(SystemState));
        sem_close(sem);
        exit(EXIT_FAILURE);
    }

    printf("EV Module Running\n");

    EngineCommand cmd; // Structure to hold the received command
    // Main loop of the EV module
    while (running) {
        if (!paused) {
            // Receive commands from the VMU through the message queue
            if (mq_receive(ev_mq_receive, (char *)&cmd, sizeof(cmd), NULL) != -1) {
                sem_wait(sem); // Acquire the semaphore to protect shared memory
                // Process the received command
                switch (cmd.type) {
                    case CMD_START:
                        system_state->ev_on = true;
                        printf("[EV] Motor Elétrico Ligado\n");
                        break;
                    case CMD_STOP:
                        system_state->ev_on = false;
                        system_state->rpm_ev = 0; // Reset RPM when stopped
                        printf("[EV] Motor Elétrico Desligado\n");
                        break;
                    case CMD_SET_POWER:
                        // EV power is controlled by the inverse of the transition factor from VMU
                        system("clear");
                        printf("[EV] Received SET_POWER command (power level: %.2f)\n", cmd.power_level);
                        break;
                    case CMD_END:
                        running = 0; // Terminate the main loop
                        break;
                    default:
                        fprintf(stderr, "[EV] Comando desconhecido recebido\n");
                        break;
                }
                sem_post(sem); // Release the semaphore
            }

            sem_wait(sem); // Acquire the semaphore for updating engine state
            // Simulate EV engine behavior
            if (system_state->ev_on) {
                // Increase RPM based on the inverse of the transition factor received from VMU
                system_state->rpm_ev = (int)((1.0 - system_state->transition_factor) * 8000);
                // Increase temperature based on the inverse of the transition factor
                system_state->temp_ev += (1.0 - system_state->transition_factor) * 0.05;
            } else {
                system_state->rpm_ev = 0; // Set RPM to 0 when off
                // Cool down the engine if it's above the ambient temperature
                if (system_state->temp_ev > 25.0) {
                    system_state->temp_ev -= 0.01;
                }
            }
            sem_post(sem); // Release the semaphore

            usleep(50000); // Small delay for the EV loop
        } else {
            sleep(1); // Sleep for 1 second if paused
        }
    }

    // Cleanup resources before exiting
    mq_close(ev_mq_receive);
    munmap(system_state, sizeof(SystemState));
    sem_close(sem);

    printf("[EV] Shut down complete.\n");
    return 0;
}