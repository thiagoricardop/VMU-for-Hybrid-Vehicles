// vmu.h
#ifndef VMU_H
#define VMU_H

#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>
#include <mqueue.h>

// Define names for shared memory, semaphore, and message queues
#define SHARED_MEM_NAME "/hybrid_car_shared_data"
#define SEMAPHORE_NAME "/hybrid_car_semaphore"
#define EV_COMMAND_QUEUE_NAME "/ev_command_queue"
#define IEC_COMMAND_QUEUE_NAME "/iec_command_queue"

// Structure for shared data
typedef struct {
    int counter; // Example data
    // Add other shared data as needed
} SharedData;

// Structure for messages (if needed for communication beyond commands)
typedef struct {
    char command;
    int value; // Example value
} Message;

// Structure for engine commands
typedef enum {
    CMD_TICK
} CommandType;

typedef struct {
    CommandType type;
    int value; // Example value
} EngineCommand;

// Signal handler function (prototype)
void handle_signal(int sig);

#endif