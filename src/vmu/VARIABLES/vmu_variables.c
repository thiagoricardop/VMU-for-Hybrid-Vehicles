#include "vmu_variables.h"

// Definições das variáveis globais
SystemState *system_state = NULL;
sem_t *sem = NULL;
mqd_t ev_mq = (mqd_t)-1;
mqd_t iec_mq = (mqd_t)-1;
volatile sig_atomic_t running = 1;
pthread_t input_thread;

volatile sig_atomic_t paused = 0;
unsigned char cont = 0;
char lastmsg[35] = {0};
unsigned char safetyCount = 0;
int ciclesQuantity = 0;
double iecTransitionRatio = 0.0;
double evTransitionRatio = 0.0;
bool transitionIEC = false;
bool transitionEV = false;
long int elapsed = 0;
long int remaining = 0;
bool start = true;
long delay_ms = 0;
int transition = 0;
double expectedvalueEV = 0.0;
double expectedValueIEC = 0.0;
bool finish = false;
bool carStop = false;