#include "vmu_cleanUp.h"

void cleanUp () {
    
    EngineCommandEV cmdEV;
    EngineCommandIEC cmdIEC;

    // Cleanup resources before exiting
    cmdEV.type = CMD_END;
    cmdIEC.type = CMD_END;
    cmdEV.toVMU = false;
    cmdIEC.toVMU = false;
    mq_send(ev_mq, (const char *)&cmdEV, sizeof(cmdEV), 0);
    mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);
    pthread_cancel(input_thread); // Request the input thread to terminate
    pthread_join(input_thread, NULL); // Wait for the input thread to finish

    mq_close(ev_mq);
    mq_unlink(EV_COMMAND_QUEUE_NAME);
    mq_close(iec_mq);
    mq_unlink(IEC_COMMAND_QUEUE_NAME);
    munmap(system_state, sizeof(SystemState));
    shm_unlink(SHARED_MEM_NAME);
    sem_close(sem);
    sem_unlink(SEMAPHORE_NAME);

}