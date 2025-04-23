// vmu.h
#ifndef VMU_H
#define VMU_H

#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>
#include <mqueue.h>
#include <stdint.h>  
#include "./VARIABLES/vmu_variables.h"
#include "./INTERFACE/interface.h"

// Define names for shared memory, semaphore, and message queues
#define SHARED_MEM_NAME "/hybrid_car_shared_data"
#define SEMAPHORE_NAME "/hybrid_car_semaphore"
#define EV_COMMAND_QUEUE_NAME "/ev_command_queue"
#define IEC_COMMAND_QUEUE_NAME "/iec_command_queue"
#define UPDATE_INTERVAL 1
#define MAX_SPEED 200.0
#define MIN_SPEED 0.0
#define MAX_TEMP_EV 120
#define MAX_TEMP_IEC 140
#define MAX_BATTERY 100.0
#define MAX_FUEL 100.0
#define MAX_ROTACAO_EV 20000
#define MAX_ROTACAO_IEC 7000
#define TRANSITION_SPEED_THRESHOLD 80.0
#define TRANSITION_ZONE_WIDTH 20.0


// Signal handler function (prototype)
void handle_signal(int sig);

#endif
