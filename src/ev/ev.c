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
#include "../vmu/vmu.h"

// Global variables
SystemState *system_state; // Pointer to the shared memory structure holding the system state
sem_t *sem;                // Pointer to the semaphore for synchronizing access to shared memory
mqd_t ev_mq_receive;       // Message queue descriptor for receiving commands for the EV module
volatile sig_atomic_t running = 1; // Flag to control the main loop, volatile to ensure visibility across threads
volatile sig_atomic_t paused = 0;  // Flag to indicate if the simulation is paused
EngineCommand cmd; // Structure to hold the received command

// Function to handle signals (SIGUSR1 for pause, SIGINT/SIGTERM for shutdown)
void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        paused = !paused;
        printf("[EV] Paused: %s\n", paused ? "true" : "false");
    } else if (sig == SIGINT || sig == SIGTERM) {
        running = 0; // Signal main loop to terminate
        printf("[EV] Shutting down...\n");
    }
}

void init_communication() {
    // Configure signal handlers for graceful shutdown and pause
    signal(SIGUSR1, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Configuration of shared memory for EV (Open read-write to update state)
    int shm_fd = shm_open(SHARED_MEM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[EV] Error opening shared memory");
        exit(EXIT_FAILURE);
    }

    // Map shared memory into EV's address space
    system_state = (SystemState *)mmap(NULL, sizeof(SystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (system_state == MAP_FAILED) {
        perror("[EV] Error mapping shared memory");
        close(shm_fd); // Close file descriptor before exiting
        exit(EXIT_FAILURE);
    }
    close(shm_fd); // Close the file descriptor as the mapping is done

    // Open the semaphore for synchronization (it should already be created by VMU)
    sem = sem_open(SEMAPHORE_NAME, 0); // 0 flag means don't create, just open
    if (sem == SEM_FAILED) {
        perror("[EV] Error opening semaphore");
        // Clean up shared memory before exiting
        munmap(system_state, sizeof(SystemState));
        exit(EXIT_FAILURE); // Exit, VMU will handle unlinking
    }

    // Configuration of POSIX message queue for receiving commands for the EV module
    struct mq_attr ev_mq_attributes;
    ev_mq_attributes.mq_flags = 0; // Flags will be set by mq_open
    ev_mq_attributes.mq_maxmsg = 10; // Max messages in queue
    ev_mq_attributes.mq_msgsize = sizeof(EngineCommand); // Max message size
    ev_mq_attributes.mq_curmsgs = 0; // Current messages (ignored for open)

    // Open message queue read-only, non-blocking. Use O_CREAT in case VMU fails to create it.
    ev_mq_receive = mq_open(EV_COMMAND_QUEUE_NAME, O_RDONLY | O_CREAT | O_NONBLOCK, 0666, &ev_mq_attributes);
    if (ev_mq_receive == (mqd_t)-1) {
        perror("[EV] Error creating/opening message queue");
        // Clean up shared memory and semaphore before exiting
        munmap(system_state, sizeof(SystemState));
        sem_close(sem);
        // No need to unlink semaphore or shm here, VMU does that
        exit(EXIT_FAILURE);
    }

    printf("EV Module Running\n");
}

void receive_cmd(){
    EngineCommand received_cmd;
    // Receive commands from the VMU through the message queue (non-blocking)
    if (mq_receive(ev_mq_receive, (char *)&received_cmd, sizeof(received_cmd), NULL) != -1) {
        sem_wait(sem); // Acquire the semaphore to protect shared memory
        // Process the received command
        switch (received_cmd.type) {
            case CMD_START:
                // VMU sets ev_on, but we can print confirmation
                system_state->ev_on = true; // VMU already sets this
                printf("[EV] Motor Elétrico: START command received.\n");
                break;
            case CMD_STOP:
                 // VMU sets ev_on, but we can print confirmation and ensure state matches
                system_state->ev_on = false; // VMU already sets this
                system_state->rpm_ev = 0; // VMU or engine() can handle RPM reduction
                printf("[EV] Motor Elétrico: STOP command received.\n");
                break;
            case CMD_SET_POWER:
                // The VMU updates system_state->ev_power_level *before* sending this message.
                // We just need to receive the message. The engine() loop will use the value from shared memory.
                // printf("[EV] Received SET_POWER command (level: %.2f)\n", received_cmd.power_level); // Optional print
                break;
            case CMD_END:
                running = 0; // Terminate the main loop
                printf("[EV] Motor Elétrico: END command received.\n");
                break;
            default:
                fprintf(stderr, "[EV] Comando desconhecido recebido (%d)\n", received_cmd.type);
                break;
        }
        sem_post(sem); // Release the semaphore
    }
    // No sleep here, the main loop will handle the tick rate
}

void engine() {
    // Local copies of state variables to work with
    bool ev_on;
    double ev_power_level;
    int rpm_ev;
    double temp_ev;
    
    // Briefly acquire the semaphore to read values
    sem_wait(sem);
    ev_on = system_state->ev_on;
    ev_power_level = system_state->ev_power_level;
    rpm_ev = system_state->rpm_ev;
    temp_ev = system_state->temp_ev;
    sem_post(sem); // Release as soon as we've read what we need

    int new_rpm = rpm_ev;
    double new_temp = temp_ev;
    
    // Process calculations using local variables
    if (ev_on) {
        // Calculate target RPM based on the commanded power level
        int target_rpm = (int)(ev_power_level * MAX_EV_RPM);
        
        // Smoothly transition RPM (using local variables)
        if (rpm_ev< target_rpm) {
            new_rpm += (int)(MAX_EV_RPM * POWER_INCREASE_RATE);
            if (new_rpm > target_rpm) new_rpm = target_rpm;
        } else if (rpm_ev > target_rpm) {
            new_rpm -= (int)(MAX_EV_RPM * POWER_DECREASE_RATE * 0.5);
            if (new_rpm < target_rpm) new_rpm = target_rpm;
        }
        
        // Calculate temperature change
        new_temp = temp_ev + (ev_power_level * EV_TEMP_INCREASE_RATE);
        if (new_temp > 90.0){
            new_temp = 90.0; // Cap at max temp
        }
        
    } else {
        // Calculate target RPM based on the commanded power level
        int target_rpm = (int)(ev_power_level * MAX_EV_RPM);
        
        // Smoothly transition RPM (using local variables)
        if (rpm_ev< target_rpm) {
            new_rpm += (int)(MAX_EV_RPM * POWER_INCREASE_RATE);
            if (new_rpm > target_rpm) new_rpm = target_rpm;
        } else if (rpm_ev > target_rpm) {
            new_rpm -= (int)(MAX_EV_RPM * POWER_DECREASE_RATE * 0.5);
            if (new_rpm < target_rpm) new_rpm = target_rpm;
        }
        
        // Cool down the engine if it's above ambient temperature
        if (temp_ev > 25.0) {
            new_temp = temp_ev - EV_TEMP_DECREASE_RATE;
            if (new_temp < 25.0) new_temp = 25.0;
        }
    }
    
    // Acquire the semaphore again to update system state with new values
    sem_wait(sem);
    system_state->rpm_ev = new_rpm;
    system_state->temp_ev = new_temp;
    sem_post(sem);
}

void cleanup() {
    // Cleanup resources before exiting
     // Ensure shared state reflects EV is off and RPM is 0 on shutdown
    sem_wait(sem);
    system_state->ev_on = false;
    system_state->rpm_ev = 0;
    sem_post(sem);


    if (ev_mq_receive != (mqd_t)-1) mq_close(ev_mq_receive);
    // No need to unlink queue, VMU does that
    if (system_state != MAP_FAILED) munmap(system_state, sizeof(SystemState));
     // No need to unlink shm, VMU does that
    if (sem != SEM_FAILED) sem_close(sem);
     // No need to unlink sem, VMU does that

    printf("[EV] Shut down complete.\n");
}
