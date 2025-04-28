#ifndef IEC_VARIABLES_H
#define IEC_VARIABLES_H

#include <stdbool.h>
#include <signal.h>     // for sig_atomic_t
#include <semaphore.h>  // for sem_t
#include <mqueue.h>     // for mqd_t
#include "../../vmu/vmu.h"        // for SystemState

// Compartilhada entre os módulos
extern SystemState *system_state;
extern sem_t *sem;
extern mqd_t iec_mq;

// Flags de controle de execução
extern volatile sig_atomic_t running;
extern volatile sig_atomic_t paused;

extern EngineCommandIEC cmd;

// Estados e parâmetros do motor de combustão (IEC)
extern double fuel;
extern double iecPercentage;
extern double localVelocity;
extern bool iecActive;
extern double iecRPM;

// Consumo médio e relações de marcha
extern const double averageConsumeKMl;
extern const float gearRatio[5];
extern double tireCircunferenceRatio;

// Contadores e índices
extern unsigned char gear;
extern unsigned char counter;

// Estado do motor elétrico
extern bool ev_on;

#endif // VARIABLES_H