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
SystemState *system_state; // Pointer to the shared memory structure holding the system state
sem_t *sem;                // Pointer to the semaphore for synchronizing access to shared memory
mqd_t iec_mq_receive;      // Message queue descriptor for receiving commands for the IEC module
volatile sig_atomic_t running = 1; // Flag to control the main loop, volatile to ensure visibility across threads
volatile sig_atomic_t paused = 0;  // Flag to indicate if the simulation is paused
// EngineCommand cmd; // No need for a global command struct if processed immediately

// Function to handle signals (SIGUSR1 for pause, SIGINT/SIGTERM for shutdown)
void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        paused = !paused;
        printf("[IEC] Paused: %s\n", paused ? "true" : "false");
    } else if (sig == SIGINT || sig == SIGTERM) {
        running = 0; // Signal main loop to terminate
        printf("[IEC] Shutting down...\n");
    }
}

// Function to initialize communication with VMU
void init_communication() {
    // Configure signal handlers for graceful shutdown and pause
    signal(SIGUSR1, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Configuration of shared memory for IEC (Open read-write to update state)
    int shm_fd = shm_open(SHARED_MEM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[IEC] Error opening shared memory");
        exit(EXIT_FAILURE);
    }

    // Map shared memory into IEC's address space
    system_state = (SystemState *)mmap(NULL, sizeof(SystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (system_state == MAP_FAILED) {
        perror("[IEC] Error mapping shared memory");
        close(shm_fd); // Close file descriptor before exiting
        exit(EXIT_FAILURE);
    }
    close(shm_fd); // Close the file descriptor as the mapping is done

    // Open the semaphore for synchronization (it should already be created by VMU)
    sem = sem_open(SEMAPHORE_NAME, 0); // 0 flag means don't create, just open
    if (sem == SEM_FAILED) {
        perror("[IEC] Error opening semaphore");
        // Clean up shared memory before exiting
        munmap(system_state, sizeof(SystemState));
        exit(EXIT_FAILURE); // Exit, VMU will handle unlinking
    }

    // Configuration of POSIX message queue for receiving commands for the IEC module
    struct mq_attr iec_mq_attributes;
    iec_mq_attributes.mq_flags = 0; // Flags will be set by mq_open
    iec_mq_attributes.mq_maxmsg = 10; // Max messages in queue
    iec_mq_attributes.mq_msgsize = sizeof(EngineCommand); // Max message size
    iec_mq_attributes.mq_curmsgs = 0; // Current messages (ignored for open)

     // Open message queue read-only, non-blocking. Use O_CREAT in case VMU fails to create it.
    // Note: O_CREAT here is generally less safe if VMU *isn't* the creator and cleanup is complex.
    // Prefer VMU to create/unlink, and modules just open. But keeping O_CREAT for robustness.
    iec_mq_receive = mq_open(IEC_COMMAND_QUEUE_NAME, O_RDONLY | O_CREAT | O_NONBLOCK, 0666, &iec_mq_attributes);
    if (iec_mq_receive == (mqd_t)-1) {
        perror("[IEC] Error creating/opening message queue");
        // Clean up shared memory and semaphore before exiting
        munmap(system_state, sizeof(SystemState));
        sem_close(sem);
        // No need to unlink semaphore or shm here, VMU does that
        exit(EXIT_FAILURE);
    }

    printf("IEC Module Running\n");
}

void receive_cmd() {
    EngineCommand received_cmd;
    // Receive commands from the VMU through the message queue (non-blocking)
    if (mq_receive(iec_mq_receive, (char *)&received_cmd, sizeof(received_cmd), NULL) != -1) {
        sem_wait(sem); // Acquire the semaphore to protect shared memory
        // Process the received command
        switch (received_cmd.type) {
            case CMD_START:
                // VMU sets iec_on, but we can print confirmation
                system_state->iec_on = true; // VMU already sets this
                printf("[IEC] Motor a Combustão: START command received.\n");
                 // When starting, immediately set RPM to idle to simulate engine turning over
                 system_state->rpm_iec = IEC_IDLE_RPM;
                break;
            case CMD_STOP:
                 // VMU sets iec_on, but we can print confirmation
                system_state->iec_on = false; // VMU already sets this
                // RPM reduction handled in engine() loop
                printf("[IEC] Motor a Combustão: STOP command received.\n");
                break;
            case CMD_SET_POWER:
                // The VMU updates system_state->iec_power_level *before* sending this message.
                // We just need to receive the message. The engine() loop will use the value from shared memory.
                // printf("[IEC] Received SET_POWER command (level: %.2f)\n", received_cmd.power_level); // Optional print
                break;
            case CMD_END:
                running = 0; // Terminate the main loop
                printf("[IEC] Motor a Combustão: END command received.\n");
                break;
            default:
                fprintf(stderr, "[IEC] Comando desconhecido recebido (%d)\n", received_cmd.type);
                break;
        }
        sem_post(sem); // Release the semaphore
    }
    // No sleep here, the main loop will handle the tick rate
}

// Função engine otimizada
void engine() {
    // Variáveis locais para armazenar valores do estado compartilhado
    bool engine_on;
    int current_rpm;
    double power_level;
    double current_temp;
    
    // Leitura dos valores necessários com o semáforo
    sem_wait(sem);
    engine_on = system_state->iec_on;
    current_rpm = system_state->rpm_iec;
    power_level = system_state->iec_power_level;
    current_temp = system_state->temp_iec;
    sem_post(sem);
    
    // Cálculos realizados fora da seção crítica
    int new_rpm = current_rpm;
    double new_temp = current_temp;
    
    if (engine_on) {
        // Calcular RPM alvo com base no nível de potência comandado
        int target_rpm = IEC_IDLE_RPM + (int)(power_level * (MAX_IEC_RPM - IEC_IDLE_RPM));
        
        // Transição suave de RPM
        if (current_rpm < target_rpm) {
            new_rpm += (int)((MAX_IEC_RPM - IEC_IDLE_RPM) * POWER_INCREASE_RATE * 0.8);
            if (new_rpm > target_rpm) new_rpm = target_rpm;
        } else if (current_rpm > target_rpm) {
            new_rpm -= (int)((MAX_IEC_RPM - IEC_IDLE_RPM) * POWER_DECREASE_RATE * 0.1);
            if (new_rpm < target_rpm) new_rpm = target_rpm;
        }
        
        // Garantir RPM mínimo é IEC_IDLE_RPM se o motor estiver ligado
        if (engine_on && new_rpm < IEC_IDLE_RPM) {
            new_rpm = IEC_IDLE_RPM;
        }
        
        // Aumentar temperatura com base no nível de potência
        new_temp += new_rpm * 0.001 * IEC_TEMP_INCREASE_RATE;
        if (new_temp > 105.0) new_temp = 105.0; // Temperatura máxima
    } else {
        int target_rpm = (int)(power_level * (MAX_IEC_RPM - IEC_IDLE_RPM));
        
        // Transição suave de RPM
        if (current_rpm > target_rpm) {
            new_rpm -= (int)((MAX_IEC_RPM - IEC_IDLE_RPM) * POWER_DECREASE_RATE * 0.7);
            if (new_rpm < target_rpm) new_rpm = target_rpm;
        }
        
        // Resfriar o motor se estiver acima da temperatura ambiente
        if (current_temp > 25.0) {
            new_temp -= IEC_TEMP_DECREASE_RATE;
            if (new_temp < 25.0) new_temp = 25.0;
        }
    }
    
    // Atualizar o estado compartilhado apenas uma vez, com o semáforo
    sem_wait(sem);
    system_state->rpm_iec = new_rpm;
    system_state->temp_iec = new_temp;
    sem_post(sem);
}

// Function to cleanup resources before exiting
void cleanup() {
    // Cleanup resources before exiting
     // Ensure shared state reflects IEC is off and RPM is 0 on shutdown
    sem_wait(sem);
    system_state->iec_on = false;
    system_state->rpm_iec = 0;
    sem_post(sem);

    if (iec_mq_receive != (mqd_t)-1) mq_close(iec_mq_receive);
     // No need to unlink queue, VMU does that
    if (system_state != MAP_FAILED) munmap(system_state, sizeof(SystemState));
     // No need to unlink shm, VMU does that
    if (sem != SEM_FAILED) sem_close(sem);
     // No need to unlink sem, VMU does that

    printf("[IEC] Shut down complete.\n");
}

