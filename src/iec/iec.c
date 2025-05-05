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


int iec_initializer(void)
{
    int32_t      shm_fd   = -1;
    uintptr_t    shm_addr = (uintptr_t)MAP_FAILED;
    sem_t       *tmp_sem  = SEM_FAILED;
    mqd_t        tmp_mq   = (mqd_t)-1;
    int          status   = EXIT_SUCCESS;
    struct mq_attr attr    = {
        .mq_flags   = 0U,
        .mq_maxmsg  = IEC_MQ_MAXMSG,
        .mq_msgsize = sizeof(EngineCommandIEC),
        .mq_curmsgs = 0U
    };

    /* 1) Abrir shared memory */
    shm_fd = shm_open(SHARED_MEM_NAME,
                      O_RDWR | O_CREAT,
                      (mode_t)IEC_SHM_PERMS);
    if (shm_fd < 0) {
        status = EXIT_FAILURE;      /* <<< retorna 1, não errno */
        goto cleanup;
    }

    /* 2) Mapear */
    shm_addr = (uintptr_t)mmap(NULL,
                               (size_t)IEC_SHM_SIZE,
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED,
                               shm_fd,
                               (off_t)0);
    if ((void *)shm_addr == MAP_FAILED) {
        status = EXIT_FAILURE;      /* <<< retorno de falha padronizado */
        goto cleanup;
    }

    (void)close(shm_fd);
    shm_fd = -1;

    /* 3) Abrir semáforo */
    tmp_sem = sem_open(SEMAPHORE_NAME,
                       O_CREAT,
                       (mode_t)IEC_SHM_PERMS,
                       1U);
    if (tmp_sem == SEM_FAILED) {
        status = EXIT_FAILURE;
        goto cleanup;
    }

    /* 4) Abrir/​criar message queue */
    tmp_mq = mq_open(IEC_COMMAND_QUEUE_NAME,
                     O_RDWR | O_CREAT | O_NONBLOCK,
                     (mode_t)IEC_MQ_PERMS,
                     &attr);
    if (tmp_mq == (mqd_t)-1) {
        status = EXIT_FAILURE;
        goto cleanup;
    }

    /* 5) Sucesso: atribui aos globais */
    system_state = (SystemState *)shm_addr;
    sem          = tmp_sem;
    iec_mq       = tmp_mq;

cleanup:
    if (status != EXIT_SUCCESS) {
        /* cleanup em caso de falha */
        if (tmp_mq   != (mqd_t)-1)        (void)mq_close(tmp_mq);
        if (tmp_sem  != SEM_FAILED)       (void)sem_close(tmp_sem);
        if ((void *)shm_addr != MAP_FAILED) (void)munmap((void *)shm_addr, (size_t)IEC_SHM_SIZE);
        if (shm_fd  >= 0)                 (void)close(shm_fd);
    }
    return status;
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
    
    if (mq_close(iec_mq) == 0) {
        iec_mq = (mqd_t) - 1;
    }
    
    if (munmap(system_state, sizeof(SystemState)) == 0) {
        system_state = NULL;
    }

    if (sem_close(sem) == 0) {
        sem = NULL;
    }
}
