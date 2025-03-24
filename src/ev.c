// ev.c
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
#include "ev.h"
#include "vmu.h"

SystemState *system_state;
sem_t *sem;
mqd_t ev_mq_receive;
volatile sig_atomic_t running = 1;
volatile sig_atomic_t paused = 0;

void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        paused = !paused;
        printf("[EV] Paused: %s\n", paused ? "true" : "false");
    } else if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
        printf("[EV] Shutting down...\n");
    }
}

int main() {
    // Configure signals
    signal(SIGUSR1, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Configuration of shared memory for EV
    int shm_fd = shm_open(SHARED_MEM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[EV] Error opening shared memory");
        exit(EXIT_FAILURE);
    }

    system_state = (SystemState *)mmap(NULL, sizeof(SystemState), PROT_READ, MAP_SHARED, shm_fd, 0);
    if (system_state == MAP_FAILED) {
        perror("[EV] Error mapping shared memory");
        exit(EXIT_FAILURE);
    }
    close(shm_fd);

    // Open semaphore
    sem = sem_open(SEMAPHORE_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("[EV] Error opening semaphore");
        exit(EXIT_FAILURE);
    }

    // Configuration of POSIX message queue for EV
    struct mq_attr ev_mq_attributes;
    ev_mq_attributes.mq_flags = 0;
    ev_mq_attributes.mq_maxmsg = 10;
    ev_mq_attributes.mq_msgsize = sizeof(EngineCommand);
    ev_mq_attributes.mq_curmsgs = 0;

    ev_mq_receive = mq_open(EV_COMMAND_QUEUE_NAME, O_RDONLY | O_CREAT | O_NONBLOCK, 0666, &ev_mq_attributes);
    if (ev_mq_receive == (mqd_t)-1) {
        perror("[EV] Error creating/opening message queue");
        munmap(system_state, sizeof(SystemState));
        sem_close(sem);
        exit(EXIT_FAILURE);
    }

    printf("EV Module Running\n");

    SystemState state = {0};

    EngineCommand cmd;
    while (running) {
        if (!paused) {
            sem_wait(sem); // Exclusive access to shared memory
            state.speed = system_state->speed;
            state.rpm_ev = system_state->rpm_ev;
            state.rpm_iec = system_state->rpm_iec;
            state.temp_ev= system_state->temp_ev;
            state.temp_iec = system_state->temp_iec;
            state.battery = system_state->battery;
            state.fuel = system_state->fuel;
            state.power_mode = system_state->power_mode;
            state.accelerator = system_state->accelerator;
            state.brake = system_state->brake;
            sem_post(sem); // Release shared memory
            

            if (mq_receive(ev_mq_receive, (char *)&cmd, sizeof(cmd), NULL) != -1) {
                sem_wait(sem);
                switch (cmd.type) {
                    case 0:
                        printf("[EV] Received tick command 0\n");
                        break;
                    
                    case 1:
                        printf("[EV] Received tick command 1\n");
                        break;
                    default:
                        break;
                }
                sem_post(sem);
            }
        }
        sleep(1);
    }

    // Cleanup
    mq_close(ev_mq_receive);
    munmap(system_state, sizeof(SystemState));
    sem_close(sem);

    printf("[EV] Shut down complete.\n");
    return 0;
}