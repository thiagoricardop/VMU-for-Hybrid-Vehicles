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

SystemState *system_state;
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


void display_status(const SystemState *state) {
    system("clear");
    printf("=== Estado do Sistema ===\n");
    printf("Speed: %.2f km/h\n", state->speed);
    printf("RPM EV: %d\n", state->rpm_ev);
    printf("RPM IEC: %d\n", state->rpm_iec);
    printf("EV: %s\n", state->ev_on ? "ON" : "OFF");
    printf("IEC: %s\n", state->iec_on ? "ON" : "OFF");
    printf("Temperature EV: %.2f C\n", state->temp_ev);
    printf("Temperature IEC: %.2f C\n", state->temp_iec);
    printf("Battery: %d%%\n", state->battery);
    printf("Fuel: %d%%\n", state->fuel);
    printf("Power mode: %d\n", state->power_mode);
    printf("Accelerator: %s\n", state->accelerator ? "ON" : "OFF");
    printf("Brake: %s\n", state->brake ? "ON" : "OFF");
}

void init_system_state() {
    system_state->accelerator = false;
    system_state->brake = false;
    system_state->speed = MIN_SPEED;
    system_state->rpm_ev = 0;
    system_state->rpm_iec = 0;
    system_state->ev_on = false;
    system_state->iec_on = false;
    system_state->temp_ev = 25.0;
    system_state->temp_iec = 25.0;
    system_state->battery = MAX_BATTERY;
    system_state->fuel = MAX_FUEL;
    system_state->power_mode = 0;
    system_state->transition_factor = 0.0;
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
    close(shm_fd);

    // Create semaphore for synchronization
    sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("[VMU] Error creating semaphore");
        exit(EXIT_FAILURE);
    }

    // Initialize system state
    init_system_state();

    // Configuration of POSIX message queues for EV
    struct mq_attr ev_mq_attributes;
    ev_mq_attributes.mq_flags = 0;
    ev_mq_attributes.mq_maxmsg = 10;
    ev_mq_attributes.mq_msgsize = sizeof(EngineCommand);
    ev_mq_attributes.mq_curmsgs = 0;

    ev_mq = mq_open(EV_COMMAND_QUEUE_NAME, O_WRONLY | O_CREAT | O_NONBLOCK, 0666, &ev_mq_attributes);
    if (ev_mq == (mqd_t)-1) {
        perror("[VMU] Error creating/opening EV message queue");
        munmap(system_state, sizeof(SystemState));
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
        munmap(system_state, sizeof(SystemState));
        shm_unlink(SHARED_MEM_NAME);
        sem_close(sem);
        sem_unlink(SEMAPHORE_NAME);
        exit(EXIT_FAILURE);
    }

    printf("VMU Module Running\n");

    SystemState state = {0};

    int counter = 0;
    EngineCommand tick_cmd = {CMD_TICK, 0};

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

            display_status(&state);

            // Logic for VMU
            if (state.speed > MAX_SPEED) {
                state.brake = true;
                state.accelerator = false;
                sem_wait(sem);
                system_state->accelerator = false;
                system_state->brake = true;
                sem_post(sem);
            } else if (state.speed <= MIN_SPEED) {
                state.accelerator = false;
                state.brake = false;
                sem_wait(sem);
                system_state->accelerator = false;
                system_state->brake = false;
                sem_post(sem);
            }

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
    munmap(system_state, sizeof(SystemState));
    shm_unlink(SHARED_MEM_NAME);
    sem_close(sem);
    sem_unlink(SEMAPHORE_NAME);

    printf("[VMU] Shut down complete.\n");
    return 0;
}