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
int shm_fd = -1;

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

int init_communication_ev(char * shared_mem_name, char * semaphore_name, char * iec_queue_name) {
    // Configure signal handlers for graceful shutdown and pause
    signal(SIGUSR1, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Configuration of shared memory for EV
    shm_fd = shm_open(SHARED_MEM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[EV] Error opening shared memory");
        return 0;
    }

    // Map shared memory into EV's address space
    system_state = (SystemState *)mmap(NULL, sizeof(SystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (system_state == MAP_FAILED) {
        perror("[EV] Error mapping shared memory");
        close(shm_fd); 
        return 0;
    }
    close(shm_fd);

    // Open the semaphore for synchronization
    sem = sem_open(SEMAPHORE_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("[EV] Error opening semaphore");
        // Clean up shared memory before exiting
        munmap(system_state, sizeof(SystemState));
        return 0; // Exit
    }

    // Configuration of POSIX message queue for receiving commands for the EV module
    struct mq_attr ev_mq_attributes;
    ev_mq_attributes.mq_flags = 0;
    ev_mq_attributes.mq_maxmsg = 10; 
    ev_mq_attributes.mq_msgsize = sizeof(EngineCommand); 
    ev_mq_attributes.mq_curmsgs = 0; 

    // Open message queue read-only, non-blocking. Use O_CREAT in case VMU fails to create it.
    ev_mq_receive = mq_open(EV_COMMAND_QUEUE_NAME, O_RDONLY | O_CREAT | O_NONBLOCK, 0666, &ev_mq_attributes);
    if (ev_mq_receive == (mqd_t)-1) {
        perror("[EV] Error creating/opening message queue");
        // Clean up shared memory and semaphore before exiting
        munmap(system_state, sizeof(SystemState));
        sem_close(sem);
        return 0;
    }

    printf("EV Module Running\n");
    return 1;
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
                system_state->ev_on = false; 
                system_state->rpm_ev = 0; // Set RPM to 0 when stopping
                printf("[EV] Motor Elétrico: STOP command received.\n");
                break;
            case CMD_SET_POWER:
                // The VMU updates system_state->ev_power_level *before* sending this message.
                // We just need to receive the message. The engine() loop will use the value from shared memory.
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
    
}

void engine() {
    // Local copies of state variables to work with
    bool ev_on;
    double ev_power_level;
    int rpm_ev;
    double temp_ev;
    
   
    sem_wait(sem);
    ev_on = system_state->ev_on;
    ev_power_level = system_state->ev_power_level;
    rpm_ev = system_state->rpm_ev;
    temp_ev = system_state->temp_ev;
    sem_post(sem);

    int new_rpm = rpm_ev;
    double new_temp = temp_ev;
    
    // Process calculations using local variables
    if (ev_on) {
        // Calculate target RPM based on the commanded power level
        int target_rpm = (int)(ev_power_level * MAX_EV_RPM);
        
        // Smoothly transition RPM 
        if (rpm_ev< target_rpm) {
            new_rpm += (int)(MAX_EV_RPM * POWER_INCREASE_RATE);
            if (new_rpm > target_rpm) new_rpm = target_rpm;
        } else if (rpm_ev > target_rpm) {
            new_rpm -= (int)(MAX_EV_RPM * POWER_DECREASE_RATE * 0.5);
            if (new_rpm < target_rpm) new_rpm = target_rpm;
        }
        
        // Calculate temperature change
        new_temp = temp_ev + (ev_power_level * EV_TEMP_INCREASE_RATE);
        if (new_temp > MAX_EV_TEMP){
            new_temp = MAX_EV_TEMP; // Cap at max temp
        }
        
    } else {
        // Calculate target RPM based on the commanded power level
        int target_rpm = (int)(ev_power_level * MAX_EV_RPM);
        
        // Smoothly transition RPM (using local variables)
        if (rpm_ev > target_rpm) {
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
    if (system_state != MAP_FAILED) munmap(system_state, sizeof(SystemState));
    if (sem != SEM_FAILED) sem_close(sem);

    printf("[EV] Shut down complete.\n");
}
