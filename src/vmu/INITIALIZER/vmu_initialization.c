#include "vmu_initialization.h"

void vmu_initialization () {

    shm_unlink(SHARED_MEM_NAME); 

    // Configuration of shared memory for VMU
    int shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[VMU] Error opening shared memory");
        exit(EXIT_FAILURE);
    }

    // Configure the size of the shared memory
    if (ftruncate(shm_fd, sizeof(SystemState)) == -1) {
        perror("[VMU] Error configuring shared memory size");
        exit(EXIT_FAILURE);
    }

    // Map shared memory into VMU's address space
    system_state = (SystemState *)mmap(NULL, sizeof(SystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (system_state == MAP_FAILED) {
        perror("[VMU] Error mapping shared memory");
        exit(EXIT_FAILURE);
    }


    sem_unlink(SEMAPHORE_NAME);

    // Create semaphore for synchronization
    sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("[VMU] Error creating semaphore");
        exit(EXIT_FAILURE);
    }

    // Initialize system state
    init_system_state(system_state);

    mq_unlink(EV_COMMAND_QUEUE_NAME);

    // Configuration of POSIX message queues for communication with the EV module
    struct mq_attr ev_mq_attributes;
    ev_mq_attributes.mq_flags = 0;
    ev_mq_attributes.mq_maxmsg = 10;
    ev_mq_attributes.mq_msgsize = sizeof(EngineCommandEV);
    ev_mq_attributes.mq_curmsgs = 0;

    ev_mq = mq_open(EV_COMMAND_QUEUE_NAME, O_RDWR| O_CREAT | O_NONBLOCK, 0666, &ev_mq_attributes);
    if (ev_mq == (mqd_t)-1) {
        perror("[VMU] Error creating/opening EV message queue");
        munmap(system_state, sizeof(SystemState));
        shm_unlink(SHARED_MEM_NAME);
        sem_close(sem);
        sem_unlink(SEMAPHORE_NAME);
        exit(EXIT_FAILURE);
    }

    mq_unlink(IEC_COMMAND_QUEUE_NAME);

    // Configuration of POSIX message queues for communication with the IEC module
    struct mq_attr iec_mq_attributes;
    iec_mq_attributes.mq_flags = 0;
    iec_mq_attributes.mq_maxmsg = 10;
    iec_mq_attributes.mq_msgsize = sizeof(EngineCommandIEC);
    iec_mq_attributes.mq_curmsgs = 0;

    iec_mq = mq_open(IEC_COMMAND_QUEUE_NAME, O_RDWR | O_CREAT | O_NONBLOCK, 0666, &iec_mq_attributes);
    if (iec_mq == (mqd_t)-1) {
        perror("[VMU] Error creating/opening IEC message queue");
        mq_close(ev_mq);
        mq_unlink(EV_COMMAND_QUEUE_NAME);
        munmap(system_state, sizeof(SystemState));
        shm_unlink(SHARED_MEM_NAME);
        sem_close(sem);
        sem_unlink(SEMAPHORE_NAME);
        exit(EXIT_FAILURE);
    }

    // Create a separate thread to read user input for pedal control
    if (pthread_create(&input_thread, NULL, read_input, NULL) != 0) {
        perror("[VMU] Error creating input thread");
        running = 0; // Exit main loop if thread creation fails
    }

    system_state->transitionCicles = 0;
}