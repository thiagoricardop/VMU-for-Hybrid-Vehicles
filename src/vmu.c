// vmu.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <mqueue.h>
#include <signal.h>
#include <errno.h>
#include "vmu.h"

SharedData *shared_data;
sem_t *sem;
mqd_t ev_mq, iec_mq;
volatile sig_atomic_t running = 1;
volatile sig_atomic_t paused = 0;

void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        paused = !paused;
        printf("[VMU] Paused: %s\n", paused ? "true" : "false");
    } else if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
        printf("[VMU] Shutting down...\n");
    }
}

int main() {
    // Configure signals
    signal(SIGUSR1, handle_signal);
    signal(SIGINT, handle_signal); // Handle Ctrl+C
    signal(SIGTERM, handle_signal); // Handle termination signal

    // Configuration of shared memory for VMU
    int shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[VMU] Error opening shared memory");
        exit(EXIT_FAILURE);
    }

    // Configure the size of the shared memory
    if (ftruncate(shm_fd, sizeof(SharedData)) == -1) {
        perror("[VMU] Error configuring shared memory size");
        exit(EXIT_FAILURE);
    }

    // Map shared memory into VMU's address space
    shared_data = (SharedData *)mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        perror("[VMU] Error mapping shared memory");
        exit(EXIT_FAILURE);
    }
    close(shm_fd);

    // Create semaphore for synchronization
    sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("[VMU] Error creating semaphore");
        exit(EXIT_FAILURE);
    }

    // Configuration of POSIX message queues for EV
    struct mq_attr ev_mq_attributes;
    ev_mq_attributes.mq_flags = 0;
    ev_mq_attributes.mq_maxmsg = 10;
    ev_mq_attributes.mq_msgsize = sizeof(EngineCommand);
    ev_mq_attributes.mq_curmsgs = 0;

    ev_mq = mq_open(EV_COMMAND_QUEUE_NAME, O_WRONLY | O_CREAT | O_NONBLOCK, 0666, &ev_mq_attributes);
    if (ev_mq == (mqd_t)-1) {
        perror("[VMU] Error creating/opening EV message queue");
        munmap(shared_data, sizeof(SharedData));
        shm_unlink(SHARED_MEM_NAME);
        sem_close(sem);
        sem_unlink(SEMAPHORE_NAME);
        exit(EXIT_FAILURE);
    }

    // Configuration of POSIX message queues for IEC
    struct mq_attr iec_mq_attributes;
    iec_mq_attributes.mq_flags = 0;
    iec_mq_attributes.mq_maxmsg = 10;
    iec_mq_attributes.mq_msgsize = sizeof(EngineCommand);
    iec_mq_attributes.mq_curmsgs = 0;

    iec_mq = mq_open(IEC_COMMAND_QUEUE_NAME, O_WRONLY | O_CREAT | O_NONBLOCK, 0666, &iec_mq_attributes);
    if (iec_mq == (mqd_t)-1) {
        perror("[VMU] Error creating/opening IEC message queue");
        mq_close(ev_mq);
        mq_unlink(EV_COMMAND_QUEUE_NAME);
        munmap(shared_data, sizeof(SharedData));
        shm_unlink(SHARED_MEM_NAME);
        sem_close(sem);
        sem_unlink(SEMAPHORE_NAME);
        exit(EXIT_FAILURE);
    }

    printf("VMU Module Running\n");

    int counter = 0;
    EngineCommand tick_cmd = {CMD_TICK, 0};

    while (running) {
        if (!paused) {
            sem_wait(sem); // Exclusive access to shared memory
            shared_data->counter = counter++;
            printf("[VMU] Counter updated to: %d\n", shared_data->counter);
            sem_post(sem); // Release shared memory

            tick_cmd.value = shared_data->counter; // Example of sending data

            if (mq_send(ev_mq, (const char *)&tick_cmd, sizeof(tick_cmd), 0) == -1) {
                perror("[VMU] Error sending TICK to EV");
            } else {
                printf("[VMU] Sent TICK to EV.\n");
            }

            if (mq_send(iec_mq, (const char *)&tick_cmd, sizeof(tick_cmd), 0) == -1) {
                perror("[VMU] Error sending TICK to IEC");
            } else {
                printf("[VMU] Sent TICK to IEC.\n");
            }
        }
        sleep(1); // Simulate some work or update interval
    }

    // Cleanup
    mq_close(ev_mq);
    mq_unlink(EV_COMMAND_QUEUE_NAME);
    mq_close(iec_mq);
    mq_unlink(IEC_COMMAND_QUEUE_NAME);
    munmap(shared_data, sizeof(SharedData));
    shm_unlink(SHARED_MEM_NAME);
    sem_close(sem);
    sem_unlink(SEMAPHORE_NAME);

    printf("[VMU] Shut down complete.\n");
    return 0;
}