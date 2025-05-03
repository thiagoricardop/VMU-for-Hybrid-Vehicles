// ev.h
#ifndef EV_H
#define EV_H

// Engine Simulation Constants - Valores ajustados para maior realismo
#define EV_TEMP_INCREASE_RATE 0.05      // Taxa de aumento de temperatura
#define EV_TEMP_DECREASE_RATE 0.01      // Taxa de diminuição de temperatura

void handle_signal(int sig);
int init_communication_ev(char * shared_mem_name, char * semaphore_name, char * iec_queue_name);
void receive_cmd();
void engine();
void cleanup();

#endif