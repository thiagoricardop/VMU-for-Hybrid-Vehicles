#ifndef VARIABLES_H
#define VARIABLES_H

#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <mqueue.h>

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

#endif // VARIABLES_H