#ifndef EV_INITIALIZER_H
#define EV_INITIALIZER_H

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>           // For O_* constants
#include <sys/stat.h>        // For mode constants
#include <sys/mman.h>        // For mmap
#include <semaphore.h>       // For semaphores
#include <mqueue.h>          // For POSIX message queues
#include <unistd.h>          // For close()

#include "../../vmu/vmu.h"   
#include "../VARIABLES/ev_variables.h"  

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


EvStatusType ev_initializer(void);

#endif 