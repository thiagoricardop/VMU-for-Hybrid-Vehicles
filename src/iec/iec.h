// iec.h
#ifndef IEC_H
#define IEC_H


#define IEC_TEMP_INCREASE_RATE 0.05 // Rate of temperature increase per loop iteration at full power
#define IEC_TEMP_DECREASE_RATE 0.01 // Rate of temperature decrease per loop iteration when off/idling

void handle_signal(int sig);
int init_communication_iec(char * shared_mem_name, char * semaphore_name, char * iec_queue_name);
void receive_cmd();
void engine();
void cleanup();

#endif