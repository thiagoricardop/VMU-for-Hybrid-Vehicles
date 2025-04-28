#include "iec_variables.h"

// Ponteiros de recurso compartilhado
SystemState *system_state = NULL;
EngineCommandIEC cmd;
sem_t *sem = NULL;
mqd_t iec_mq = (mqd_t)-1;

// Flags de controle de execução
volatile sig_atomic_t running = 1;
volatile sig_atomic_t paused  = 0;

// Estados e parâmetros do motor de combustão (IEC)
double fuel = 45.0;
double iecPercentage = 0.0;
double localVelocity = 0.0;
bool iecActive = false;
double iecRPM = 0.0;

// Consumo médio e relações de marcha
const double averageConsumeKMl = 14.7;
const float gearRatio[5] = { 3.83f, 2.36f, 1.69f, 1.31f, 1.00f };
double tireCircunferenceRatio = 2.19912;

// Contadores e índices
unsigned char gear = 0;
unsigned char counter = 0;

// Estado do motor elétrico
bool ev_on = false;