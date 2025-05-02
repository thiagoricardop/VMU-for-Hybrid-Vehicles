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

#include "../vmu/vmu.h"

// Global variables
SystemState *system_state; // Pointer to the shared memory structure holding the system state
sem_t *sem;                // Pointer to the semaphore for synchronizing access to shared memory
mqd_t ev_mq;      // Message queue descriptor for receiving commands for the EV module
EngineCommandEV cmd; // Structure to hold the received command
volatile sig_atomic_t running = 1; // Flag to control the main loop, volatile to ensure visibility across threads
volatile sig_atomic_t paused = 0;  // Flag to indicate if the simulation is paused
double BatteryEV = 11.5;
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
double fuel = 0.0;

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


EvStatusType ev_initializer(void) {

    EvStatusType status = 0;               /* status de retorno (único ponto de saída) (@turn0search0) */
    int32_t shm_fd = -1;
    uintptr_t shm_addr = 0U;            /* evitar Rule 11.5 (@turn1search1) */
    sem_t *tmp_sem = (sem_t *)0;
    mqd_t  tmp_mq = (mqd_t)-1;

    /* 1) Abrir memória compartilhada */
    shm_fd = shm_open(SHARED_MEM_NAME, EV_SHM_FLAGS | O_CREAT, EV_SHM_PERMS);
    if (shm_fd < 0) {
        perror("[EV] shm_open falhou");
        status = EXIT_FAILURE;
        goto cleanup;
    }

    /* 2) Mapear em void*, depois converter via uintptr_t para obedecer Rule 11.5 (@turn1search1) */
    shm_addr = (uintptr_t)mmap(NULL, EV_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if ((void *)shm_addr == MAP_FAILED) {
        perror("[EV] mmap falhou");
        status = EXIT_FAILURE;
        goto cleanup;
    }
    system_state = (SystemState *)shm_addr;

    (void)close(shm_fd);

    /* 3) Abrir semáforo POSIX (@turn0search1) */
    tmp_sem = sem_open(SEMAPHORE_NAME, EV_SEM_FLAGS | O_CREAT, EV_SHM_PERMS, 1);
    if (tmp_sem == SEM_FAILED) {
        perror("[EV] sem_open falhou");
        status = EXIT_FAILURE;
        goto cleanup;
    }
    sem = tmp_sem;

    /* 4) Configurar atributos da fila de mensagens */
    {
        struct mq_attr ev_attr = {
            .mq_flags   = 0,
            .mq_maxmsg  = EV_MQ_MAXMSG,
            .mq_msgsize = EV_MQ_MSGSIZE,
            .mq_curmsgs = 0
        };

        /* 5) Abrir fila de mensagens (@turn0search1) */
        tmp_mq = mq_open(EV_COMMAND_QUEUE_NAME, EV_MQ_FLAGS, EV_MQ_PERMS, &ev_attr);
        if (tmp_mq == (mqd_t)-1) {
            perror("[EV] mq_open falhou");
            status = EXIT_FAILURE;
            goto cleanup;
        }
        ev_mq = tmp_mq;
    }

cleanup:
    /* 6) Um único ponto de saída retorna status (@turn0search2) */
    return status;
}


EngineCommandEV ev_receive (EngineCommandEV cmd) {
    // Receive commands from the VMU through the message queue
    while (mq_receive(ev_mq, (char *)&cmd, sizeof(cmd), NULL) == -1) {

    }

    
    return cmd;
}

void calculateValues () {

    if (!accelerator && localVelocity != 0.0) {
        BatteryEV += 1.0;
    }

    else if (evActive && localVelocity > 0.0) {
        BatteryEV -= 0.01;
    }

    if (BatteryEV < 0.0) {
        BatteryEV = 0.0;
    } 

    if (BatteryEV > 100.0) {
        BatteryEV = 100.0;
    }
    lastLocalVelocity = localVelocity;

    if (fuel > 0.0) {

        rpmEV = evPercentage*((localVelocity * 16.67) / tireCircunferenceRatio);
    }

    else {
        rpmEV = evPercentage*((localVelocity * 16.67) / tireCircunferenceRatio);
        
        if (rpmEV > 341.113716) {
            rpmEV = 341.113716;
        }
    }
    
}

void ev_treatValues () {

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
    fuel = cmd.iec_fuel;

    if (evPercentage != 0) {
        evActive = true;                
    }
    else {
        evActive = false;            
    }

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

    calculateValues();    
}

void ev_cleanUp () {
    
    if (mq_close(ev_mq) == 0) {
        ev_mq = (mqd_t)-1;
    }
    
    if (munmap(system_state, sizeof(SystemState)) == 0 ){
        system_state = NULL;
    }
    
    if (sem_close(sem) == 0) {
        sem = NULL;
    }

}
