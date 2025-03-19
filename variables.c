#include "variables.h"

char input_buffer[TAM_BUFFER] = "";
char stored_value[TAM_BUFFER] = "";
char lastStoredValue[TAM_BUFFER] = "";
char brake[20] = "OFF";
char accelerator[20] = "OFF";
char running[11] = "Running";
char powertrain[20] = "None";
char events[30] = "Vehicle On";
char eventsDisplayed[30] = "Vehicle On";

unsigned char cont = 0;

float vehicle_speed = 0;
float iec_temperature = 0;
float iec_percentage = 0;
float ev_percentage = 0;
float Battery = 100;
float fuel_level = 45;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t continuar = 1;