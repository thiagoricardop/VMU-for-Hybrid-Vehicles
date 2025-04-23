#ifndef EV_VARIABLES_H
#define EV_VARIABLES_H

#include <mqueue.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include "../../vmu/vmu.h"

extern SystemState *system_state;
extern sem_t *sem;
extern mqd_t ev_mq;
extern EngineCommandEV cmd;
extern volatile sig_atomic_t running;
extern volatile sig_atomic_t paused;
extern double BatteryEV;
extern double evPercentage;
extern bool firstReceive;
extern double localVelocity;
extern double lastLocalVelocity;
extern double distance;
extern double rpmEV;
extern double tireCircunferenceRatio;
extern bool evActive;
extern bool accelerator;
extern unsigned char counter;
extern double fuel;

#endif 
