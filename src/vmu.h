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
#define UPDATE_INTERVAL 1
#define MAX_SPEED 200.0
#define MIN_SPEED 0.0
#define MAX_TEMP_EV 120
#define MAX_TEMP_IEC 140
#define MAX_BATTERY 100
#define MAX_FUEL 100
#define MAX_ROTACAO_EV 20000
#define MAX_ROTACAO_IEC 7000


// Structure for system state
typedef struct {
    bool accelerator;
    bool brake;
    double speed;
    int rpm_ev;
    int rpm_iec;
    bool ev_on;
    bool iec_on;
    double temp_ev;
    double temp_iec;
    int battery;
    int fuel;
    int power_mode;
    double transition_factor;
} SystemState;

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

// Function to display system status
void display_status(const SystemState *state);

// Function to initialize system state
void init_system_state();

#endif