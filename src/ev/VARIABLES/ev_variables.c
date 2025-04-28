#include "ev_variables.h"

// Global variables
SystemState *system_state; // Pointer to the shared memory structure holding the system state
sem_t *sem;                // Pointer to the semaphore for synchronizing access to shared memory
mqd_t ev_mq;      // Message queue descriptor for receiving commands for the EV module
EngineCommandEV cmd; // Structure to hold the received command
volatile sig_atomic_t running = 1; // Flag to control the main loop, volatile to ensure visibility across threads
volatile sig_atomic_t paused = 0;  // Flag to indicate if the simulation is paused
double BatteryEV = 11.5;
double evPercentage = 0.0;
bool firstReceive = true;
double localVelocity = 0.0;
double lastLocalVelocity = 0.0;
double distance = 3000;
double rpmEV;
double tireCircunferenceRatio = 2.19912;
bool evActive;
bool accelerator;
unsigned char counter = 0;
double fuel = 0.0;