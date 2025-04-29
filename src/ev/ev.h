// ev.h
#ifndef EV_H
#define EV_H

void handle_signal(int sig);
int get_signal();
void init_communication();
void receive_cmd();
void engine();
void cleanup();

#endif