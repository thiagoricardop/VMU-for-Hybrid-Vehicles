#ifndef IEC_INITIALIZER_H
#define IEC_INITIALIZER_H

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>           // For O_* constants
#include <sys/stat.h>        // For mode constants
#include <sys/mman.h>        // For mmap
#include <semaphore.h>       // For semaphores
#include <mqueue.h>          // For POSIX message queues
#include <unistd.h>          // For close()

#include "../../vmu/vmu.h"   // Supondo que SystemState está definido aqui
#include "../VARIABLES/iec_variables.h"   // Supondo que você usa as variáveis do IEC aqui

// Declaração da função de inicialização
void iec_initializer(void);

#endif // IEC_INITIALIZER_H
