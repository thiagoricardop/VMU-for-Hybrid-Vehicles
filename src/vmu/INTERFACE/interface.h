#ifndef INTERFACE_H
#define INTERFACE_H

#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include "../VARIABLES/vmu_variables.h"
#include "../vmu.h"

#define DEBUG_MSG_SIZE 256

void *read_input(void *arg);
void display_status(const SystemState *state);
void init_system_state(SystemState *state);
void set_acceleration(bool accelerate);
void set_braking(bool brake);

#endif // INTERFACE_H