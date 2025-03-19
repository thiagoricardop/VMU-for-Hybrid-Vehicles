#ifndef VARIABLES_H
#define VARIABLES_H

#define TAM_BUFFER 3
#define TIMEOUT_USEC 80000  // 80 ms 

#include <pthread.h>
#include <signal.h>

extern char input_buffer[TAM_BUFFER];
extern char stored_value[TAM_BUFFER];
extern char lastStoredValue[TAM_BUFFER];
extern char brake[20];
extern char accelerator[20];
extern char running[11];
extern char powertrain[20];
extern char events[30];
extern char eventsDisplayed[30];

extern unsigned char cont;

extern float vehicle_speed; 
extern float iec_temperature;
extern float iec_percentage;
extern float ev_percentage; 
extern float Battery; 
extern float fuel_level;

extern volatile sig_atomic_t continuar;  
extern pthread_mutex_t mutex;

#endif