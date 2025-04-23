#ifndef CALCULATE_SPEED_H
#define CALCULATE_SPEED_H

#include <math.h>
#include <semaphore.h>
#include "../VARIABLES/vmu_variables.h"
#include "../vmu.h"

int calculateCicleEstimated(SystemState *state);
void calculate_speed(SystemState *state);

#endif 
