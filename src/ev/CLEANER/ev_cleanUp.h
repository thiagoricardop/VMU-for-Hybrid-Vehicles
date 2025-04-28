#ifndef EV_CLEANUP_H
#define EV_CLEANUP_H

#include <mqueue.h>       
#include <sys/mman.h>     
#include <semaphore.h>    
#include "../ev.h"
#include "../VARIABLES/ev_variables.h"

void ev_cleanUp(void);


#endif