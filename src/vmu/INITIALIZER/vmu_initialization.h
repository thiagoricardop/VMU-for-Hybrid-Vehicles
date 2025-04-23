// vmu_initialization.h
#ifndef VMU_INITIALIZATION_H
#define VMU_INITIALIZATION_H

#include "../VARIABLES/vmu_variables.h"
#include "../vmu.h"      // Contém definições de SystemState, EngineCommandEV, EngineCommandIEC, etc.
#include <semaphore.h>
#include <mqueue.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Função que inicializa memória compartilhada, semáforo, filas de mensagens e thread de entrada
void vmu_initialization(void);

#endif // VMU_INITIALIZATION_H