// iec.c
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
#include <math.h>
#include "iec.h"
#include "vmu.h"

SystemState *system_state;
sem_t *sem;
mqd_t iec_mq_receive;
volatile sig_atomic_t running = 1;
volatile sig_atomic_t paused = 0;

void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        paused = !paused;
        printf("[IEC] Paused: %s\n", paused ? "true" : "false");
    } else if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
        printf("[IEC] Shutting down...\n");
    }
}

int main() {
    // Configure signals
    signal(SIGUSR1, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Configuration of shared memory for IEC
    int shm_fd = shm_open(SHARED_MEM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[IEC] Error opening shared memory");
        exit(EXIT_FAILURE);
    }

    system_state = (SystemState *)mmap(NULL, sizeof(SystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (system_state == MAP_FAILED) {
        perror("[IEC] Error mapping shared memory");
        exit(EXIT_FAILURE);
    }
    close(shm_fd);

    // Open semaphore
    sem = sem_open(SEMAPHORE_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("[IEC] Error opening semaphore");
        exit(EXIT_FAILURE);
    }

    // Configuration of POSIX message queue for IEC
    struct mq_attr iec_mq_attributes;
    iec_mq_attributes.mq_flags = 0;
    iec_mq_attributes.mq_maxmsg = 10;
    iec_mq_attributes.mq_msgsize = sizeof(EngineCommand);
    iec_mq_attributes.mq_curmsgs = 0;

    iec_mq_receive = mq_open(IEC_COMMAND_QUEUE_NAME, O_RDONLY | O_CREAT | O_NONBLOCK, 0666, &iec_mq_attributes);
    if (iec_mq_receive == (mqd_t)-1) {
        perror("[IEC] Error creating/opening message queue");
        munmap(system_state, sizeof(SystemState));
        sem_close(sem);
        exit(EXIT_FAILURE);
    }

    printf("IEC Module Running\n");

    EngineCommand cmd;
    while (running) {
        if (!paused) {
            if (mq_receive(iec_mq_receive, (char *)&cmd, sizeof(cmd), NULL) != -1) {
                sem_wait(sem);
                switch (cmd.type) {
                    case CMD_START:
                        system_state->iec_on = true;
                        printf("[IEC] Motor a Combustão Ligado\n");
                        break;
                    case CMD_STOP:
                        system_state->iec_on = false;
                        system_state->rpm_iec = 0;
                        printf("[IEC] Motor a Combustão Desligado\n");
                        break;
                    case CMD_SET_POWER:
                        // IEC power will be controlled by the transition factor directly
                        system("clear");
                        printf("[IEC] Received SET_POWER command (power level: %.2f)\n", cmd.power_level);
                        break;
                    case CMD_END:
                        running = 0;
                        break;
                    default:
                        fprintf(stderr, "[IEC] Comando desconhecido recebido\n");
                        break;
                }
                sem_post(sem);
            }

            sem_wait(sem);
            if (system_state->iec_on) {
                // Increase power based on the transition factor
                system_state->rpm_iec = (int)(system_state->transition_factor * 5000);
                system_state->temp_iec += system_state->transition_factor * 0.1;
            } else {
                system_state->rpm_iec = 0;
                if (system_state->temp_iec > 25.0) {
                    system_state->temp_iec -= 0.02;
                }
            }
            sem_post(sem);

            usleep(70000);
        } else {
            sleep(1);
        }
    }

    // Cleanup
    mq_close(iec_mq_receive);
    munmap(system_state, sizeof(SystemState));
    sem_close(sem);

    printf("[IEC] Shut down complete.\n");
    return 0;
}