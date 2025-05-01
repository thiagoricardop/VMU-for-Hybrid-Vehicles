// iec.h
#ifndef IEC_H
#define IEC_H

void handle_signal(int sig);
int get_signal();
int init_communication_iec(char * shared_mem_name, char * semaphore_name, char * iec_queue_name);
void receive_cmd();
void engine();
void cleanup();

#endif