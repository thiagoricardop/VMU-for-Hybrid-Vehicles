#ifndef VMU_CONTROL_ENGINES_H
#define VMU_CONTROL_ENGINES_H

#include <stdbool.h>
#include <mqueue.h>
#include <semaphore.h>

#include "../vmu.h"           
#include "../INTERFACE/interface.h"
#include "../VARIABLES/vmu_variables.h"      

#define TIRE_PERIMETER 2.19912f
#define MAX_EV_SPEED 45.0f
#define IS_EMPTY 0.0f
#define TEN_PERCENT 10.0f
#define CHARGE_FULL 100.0f
#define INACTIVE 0.0f
#define MAX_EV_RPM 341.113716f
#define ACT_ALONE 1.0f
#define HALF_PERCENT 0.005f
#define CIRCUNFERENCE_RATIO 16.67f
#define PARKED 0.0f

#define ELETRIC_ONLY 0;
#define HYBRID 1;
#define COMBUSTION_ONLY 2;

void vmu_control_engines(void);

#endif 
