// ev.h
#ifndef EV_H
#define EV_H

// Engine Simulation Constants - Valores ajustados para maior realismo
#define EV_TEMP_INCREASE_RATE 0.05      // Taxa de aumento de temperatura
#define EV_TEMP_DECREASE_RATE 0.01      // Taxa de diminuição de temperatura

void handle_signal(int sig);
void init_communication();
void receive_cmd();
void engine();
void cleanup();

#endif