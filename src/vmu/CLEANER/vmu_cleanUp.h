#ifndef VMU_CLEANUP_H
#define VMU_CLEANUP_H

#include <pthread.h>
#include <mqueue.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <stdbool.h>
#include "../VARIABLES/vmu_variables.h"
#include "../vmu.h"

void cleanUp(void);

#endif 
