#include "iec_cleanUp.h"

void iecCleanUp () {
    mq_close(iec_mq);
    munmap(system_state, sizeof(SystemState));
    sem_close(sem);
}