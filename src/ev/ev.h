// ev.h
#ifndef EV_H
#define EV_H

#include "../vmu/vmu.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>           // For O_* constants
#include <sys/stat.h>        // For mode constants
#include <sys/mman.h>        // For mmap
#include <semaphore.h>       // For semaphores
#include <mqueue.h>          // For POSIX message queues
#include <unistd.h>          // For close()

extern SystemState *system_state;
extern sem_t *sem;
extern mqd_t ev_mq;
extern EngineCommandEV cmd;
extern volatile sig_atomic_t running;
extern volatile sig_atomic_t paused;
extern double BatteryEV;
extern double evPercentage;
extern bool firstReceive;
extern double localVelocity;
extern double lastLocalVelocity;
extern double distance;
extern double rpmEV;
extern double tireCircunferenceRatio;
extern bool evActive;
extern bool accelerator;
extern unsigned char counter;
extern double fuel;
extern bool toVMU;

/* Constantes MISRA C conforme Rule 7‑1‑1 (@turn0search5) */
static const int32_t  EV_SHM_FLAGS      = O_RDWR;
static const mode_t   EV_SHM_PERMS      = 0666;
static const int32_t  EV_SEM_FLAGS      = 0;
static const uint32_t EV_MQ_MAXMSG      = 10U;
static const size_t   EV_MQ_MSGSIZE     = sizeof(EngineCommandEV);
static const int32_t  EV_MQ_FLAGS       = O_RDWR | O_CREAT | O_NONBLOCK;
static const mode_t   EV_MQ_PERMS       = 0666U;

/* Constante para tamanho de mapeamento (@turn1search4) */
static const size_t   EV_SHM_SIZE       = sizeof(SystemState);

typedef int32_t EvStatusType;


int32_t ev_initializer(void);
EngineCommandEV ev_receive(EngineCommandEV cmd);

void handle_signal(int sig);
void ev_treatValues();
void calculateValues();
void ev_cleanUp();


#endif