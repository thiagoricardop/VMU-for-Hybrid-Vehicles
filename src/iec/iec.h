// iec.h
#ifndef IEC_H
#define IEC_H
#include "../vmu/vmu.h"

/* Constantes MISRA C conforme Rule 7‑1‑1 (@turn0search5) */
static const int32_t  IEC_SHM_FLAGS      = O_RDWR;
static const mode_t   IEC_SHM_PERMS      = 0666;
static const int32_t  IEC_SEM_FLAGS      = 0;
static const uint32_t IEC_MQ_MAXMSG      = 10U;
static const size_t   IEC_MQ_MSGSIZE     = sizeof(EngineCommandEV);
static const int32_t  IEC_MQ_FLAGS       = O_RDWR | O_CREAT | O_NONBLOCK;
static const mode_t   IEC_MQ_PERMS       = 0666U;

/* Constante para tamanho de mapeamento (@turn1search4) */
static const size_t   IEC_SHM_SIZE       = sizeof(SystemState);




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

void handle_signal(int sig);
int iec_initializer(void);
void iecCleanUp ();
void treatValues();
void calculateValues();

EngineCommandIEC iec_receive(EngineCommandIEC cmd);

#endif