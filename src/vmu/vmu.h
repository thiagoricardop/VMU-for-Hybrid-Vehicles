// vmu.h
#ifndef VMU_H
#define VMU_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <semaphore.h>
#include <mqueue.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <pthread.h> 
#include <string.h>
#include <time.h> 
#include <stdbool.h>
#include <stdint.h> 

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

#define TIRE_PERIMETER 2.19912f
#define MAX_EV_SPEED 45.0f
#define IS_EMPTY 0.0f
#define TEN_PERCENT 10.0f
#define CHARGE_FULL 100.0f
#define INACTIVE 0.0f
#define MAX_EV_RPM 341.113716f
#define ACT_ALONE 1.0f
#define HALF_PERCENT 0.005f
#define CIRCUNFERENCE_RATIO 16.67f
#define PARKED 0.0f
#define TWO_PERCENT 0.02f
#define NEAR_ZERO 1e-6f
#define MSG_SIZE 80

#define ELETRIC_ONLY 0;
#define HYBRID 1;
#define COMBUSTION_ONLY 2;

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
    int power_mode; // 0: Electric Only, 1: Hybrid, 2: Combustion Only, 3: Regenerative Braking, 4: Parked
    double transition_factor;
    unsigned char transitionCicles;
    bool startComunication;
    double evPercentage;
    double iecPercentage;
    char debg[80];
} SystemState;

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
    double iec_fuel;
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

// Structure for messages (if needed for communication beyond commands)
typedef struct {
    char command;
    int value; // Example value
} Message;

// Declarações das variáveis globais
extern SystemState *system_state;           // Ponteiro para a estrutura de estado do sistema
extern sem_t *sem;                          // Ponteiro para o semáforo para sincronização
extern mqd_t ev_mq, iec_mq;                 // Descritores de filas de mensagens para EV e IEC
extern volatile sig_atomic_t running;       // Flag para controlar o loop principal
extern pthread_t input_thread;              // Thread de entrada

extern volatile sig_atomic_t paused;        // Flag para indicar se a simulação está pausada
extern unsigned char cont;
extern char lastmsg[35];
extern unsigned char safetyCount;
extern int ciclesQuantity;
extern double iecTransitionRatio;
extern double evTransitionRatio;
extern bool transitionIEC;
extern bool transitionEV;
extern long int elapsed;
extern long int remaining;
extern bool start;
extern long delay_ms;
extern int transition;
extern double expectedvalueEV;
extern double expectedValueIEC;
extern bool finish;
extern bool carStop;

// Signal handler function (prototype)
void handle_signal(int sig);
int calculateCicleEstimated(SystemState *state);
void calculate_speed(SystemState *state);
void vmu_initialization(void);
void vmu_control_engines(void);
void vmu_check_queue(unsigned char counter, mqd_t mqd, bool ev);
void init_system_state(SystemState *state);
void set_acceleration(bool accelerate);
void set_braking(bool brake);
void display_status(const SystemState *state);
void *read_input(void *arg);
void cleanUp(void);

#endif
