// ev.h
#ifndef EV_H
#define EV_H

void handle_signal(int sig);
int get_signal();
int init_communication_ev(char * shared_mem_name, char * semaphore_name, char * ev_queue_name);
void receive_cmd();
void engine();
void cleanup();

#endif