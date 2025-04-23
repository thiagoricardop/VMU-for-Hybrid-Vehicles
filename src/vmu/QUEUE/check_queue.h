#ifndef CHECK_QUEUE_H
#define CHECK_QUEUE_H

#include <stdbool.h>
#include <mqueue.h>
#include <semaphore.h>
#include "../VARIABLES/vmu_variables.h"
#include "../vmu.h"

void vmu_check_queue(unsigned char counter, mqd_t mqd, bool ev);

#endif 