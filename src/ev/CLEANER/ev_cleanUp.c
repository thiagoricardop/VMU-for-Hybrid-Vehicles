#include "ev_cleanUp.h"

void ev_cleanUp () {
    
    mq_close(ev_mq);
    munmap(system_state, sizeof(SystemState));
    sem_close(sem);
}