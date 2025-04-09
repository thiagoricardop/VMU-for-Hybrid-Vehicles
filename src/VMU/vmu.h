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
#define MAX_BATTERY 100.0
#define MAX_FUEL 100.0
#define MAX_ROTACAO_EV 20000
#define MAX_ROTACAO_IEC 7000
#define TRANSITION_SPEED_THRESHOLD 80.0
#define TRANSITION_ZONE_WIDTH 20.0


// Structure for system state
typedef struct {
    bool accelerator;
    bool brake;
    double speed;
    double rpm_ev;
    double rpm_iec;
    bool ev_on;
    bool iec_on;
    bool safety;
    double temp_ev;
    double temp_iec;
    double battery; 
    double fuel;    
    int power_mode; // 0: Hybrid, 1: Electric Only, 2: Combustion Only, 3: Regenerative Braking, 4: Parked
    double transition_factor;
    unsigned char transitionCicles;
    bool startComunication;
    double evPercentage;
    double iecPercentage;
    char debg[45];
} SystemState;

// Structure for messages (if needed for communication beyond commands)
typedef struct {
    char command;
    int value; // Example value
} Message;

// Enumeration for engine commands
typedef enum {
    CMD_START,
    CMD_STOP,
    CMD_SET_POWER,
    CMD_END
} CommandType;

// Structure for engine commands
typedef struct {
    CommandType type;
    double power_level;
    char check[30];
    double batteryEV;
    double globalVelocity;
    bool evActive;
    double rpm_ev;
    bool toVMU;
    bool accelerator;
} EngineCommandEV;

// Structure for engine commands
typedef struct {
    CommandType type;
    double power_level;
    char check[3];
    double fuelIEC;
    double temperatureIEC;
    double globalVelocity;
    bool iecActive;
    double rpm_iec;
    bool toVMU;
    bool ev_on;
} EngineCommandIEC;

// Signal handler function (prototype)
void handle_signal(int sig);

// Function to display system status
void display_status(const SystemState *state);

// Function to initialize system state
void init_system_state();

#endif
