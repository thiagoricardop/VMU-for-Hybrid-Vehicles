// src/vmu.c
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
#include <pthread.h> // Include for threads
#include <string.h>  // Include for string comparison
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
    printf("Battery: %.2f%%\n", state->battery);
    printf("Fuel: %.2f%%\n", state->fuel);
    printf("Power mode: %s\n", 
           state->power_mode == 0 ? "Electric Only" : 
           state->power_mode == 1 ? "Hybrid" : 
           state->power_mode == 2 ? "Combustion Only" :
           state->power_mode == 3 ? "Regenerative Braking" : "Parked");
    printf("Accelerator: %s\n", state->accelerator ? "ON" : "OFF");
    printf("Brake: %s\n", state->brake ? "ON" : "OFF");
    printf("\nType `1` for accelerate, `2` for brake, or `0` for none, and press Enter:\n");
}

void init_system_state(SystemState *state) {
    state->accelerator = false;
    state->brake = false;
    state->speed = MIN_SPEED;
    state->rpm_ev = 0;
    state->rpm_iec = 0;
    state->ev_on = false;
    state->iec_on = false;
    state->temp_ev = 25.0;
    state->temp_iec = 25.0;
    state->battery = MAX_BATTERY;
    state->fuel = MAX_FUEL;
    state->power_mode = 5; // Parked mode initially
    state->transition_factor = 0.0;
}

void set_acceleration(bool accelerate) {
    sem_wait(sem);
    system_state->accelerator = accelerate;
    if (accelerate) {
        system_state->brake = false;
    }
    sem_post(sem);
}

void set_braking(bool brake) {
    sem_wait(sem);
    system_state->brake = brake;
    if (brake) {
        system_state->accelerator = false;
    }
    sem_post(sem);
}

double calculate_speed(SystemState *state) {
    double fator_variacao = 0.0;
    sem_wait(sem);
    if (state->accelerator) {
        fator_variacao = (state->rpm_ev * (1.0 - state->transition_factor) + state->rpm_iec * state->transition_factor) * 0.0002;
        state->speed = fmin(state->speed + fator_variacao, MAX_SPEED);
    } else if (!state->brake) {
        // Coasting - slight deceleration
        state->speed = fmax(state->speed - 0.3, MIN_SPEED);
    } else {
        double fator_variacao_freio = 2.0; // Increased braking power
        state->speed = fmax(state->speed - fator_variacao_freio, MIN_SPEED);
    }
    double current_speed = state->speed;
    sem_post(sem);
    return current_speed;
}

void vmu_control_engines() {
    EngineCommand cmd;
    sem_wait(sem);
    double current_speed = system_state->speed;
    sem_post(sem);

    if (current_speed >= TRANSITION_SPEED_THRESHOLD - (TRANSITION_ZONE_WIDTH / 2.0) &&
        current_speed <= TRANSITION_SPEED_THRESHOLD + (TRANSITION_ZONE_WIDTH / 2.0)
        && system_state->accelerator && system_state->battery > 10.0 && system_state->fuel > 5.0) {
        sem_wait(sem);
        double distancia_do_limite = fabs(current_speed - TRANSITION_SPEED_THRESHOLD);
        system_state->transition_factor = distancia_do_limite / (TRANSITION_ZONE_WIDTH / 2.0);
        system_state->transition_factor = fmin(1.0, fmax(0.0, system_state->transition_factor));
        double transition_factor = system_state->transition_factor;
        sem_post(sem);

        cmd.type = CMD_SET_POWER;
        cmd.power_level = 1.0 - transition_factor;
        mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);

        cmd.type = CMD_SET_POWER;
        cmd.power_level = transition_factor;
        mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);

        sem_wait(sem);
        if (transition_factor > 0.0 && !system_state->iec_on) {
            cmd.type = CMD_START;
            mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);
            system_state->iec_on = true;
        } else if (transition_factor == 0.0 && system_state->iec_on) {
            cmd.type = CMD_STOP;
            mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);
            system_state->iec_on = false;
        }

        if (transition_factor < 1.0 && !system_state->ev_on) {
            cmd.type = CMD_START;
            mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
            system_state->ev_on = true;
        } else if (transition_factor == 1.0 && system_state->ev_on) {
            cmd.type = CMD_STOP;
            mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
            system_state->ev_on = false;
        }
        sem_post(sem);
    } else if (current_speed < TRANSITION_SPEED_THRESHOLD - (TRANSITION_ZONE_WIDTH / 2.0) && system_state->accelerator && system_state->battery > 10.0) {
        sem_wait(sem);
        system_state->transition_factor = 0.0;
        sem_post(sem);

        cmd.type = CMD_SET_POWER;
        cmd.power_level = 1.0;
        mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
        cmd.type = CMD_SET_POWER;
        cmd.power_level = 0.0;
        mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);

        sem_wait(sem);
        if (!system_state->ev_on) {
            cmd.type = CMD_START;
            mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
            system_state->ev_on = true;
        }
        if (system_state->iec_on) {
            cmd.type = CMD_STOP;
            mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);
            system_state->iec_on = false;
        }
        sem_post(sem);
    } else if (current_speed > TRANSITION_SPEED_THRESHOLD + (TRANSITION_ZONE_WIDTH / 2.0) && system_state->accelerator && system_state->fuel > 5.0) {
        sem_wait(sem);
        system_state->transition_factor = 1.0;
        sem_post(sem);

        cmd.type = CMD_SET_POWER;
        cmd.power_level = 0.0;
        mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
        cmd.type = CMD_SET_POWER;
        cmd.power_level = 1.0;
        mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);

        sem_wait(sem);
        if (system_state->ev_on) {
            cmd.type = CMD_STOP;
            mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
            system_state->ev_on = false;
        }
        if (!system_state->iec_on) {
            cmd.type = CMD_START;
            mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);
            system_state->iec_on = true;
        }
        sem_post(sem);
    }

    sem_wait(sem);
    if (system_state->battery <= 10.0 && current_speed < TRANSITION_SPEED_THRESHOLD && system_state->fuel > 5.0 && system_state->accelerator == true) {
        system_state->transition_factor = 1.0;
        cmd.type = CMD_SET_POWER;
        cmd.power_level = 0.0;
        mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
        cmd.type = CMD_SET_POWER;
        cmd.power_level = 1.0;
        mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);
        if (system_state->ev_on) {
            cmd.type = CMD_STOP;
            mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
            system_state->ev_on = false;
        }
        if (!system_state->iec_on) {
            cmd.type = CMD_START;
            mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);
            system_state->iec_on = true;
        }
    } else if (system_state->fuel <= 5.0 && current_speed >= TRANSITION_SPEED_THRESHOLD && system_state->accelerator == true && system_state->battery > 10.0) {
        system_state->transition_factor = 0.0;
        cmd.type = CMD_SET_POWER;
        cmd.power_level = 1.0;
        mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
        cmd.type = CMD_SET_POWER;
        cmd.power_level = 0.0;
        mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);
        if (!system_state->ev_on) {
            cmd.type = CMD_START;
            mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
            system_state->ev_on = true;
        }
        if (system_state->iec_on) {
            cmd.type = CMD_STOP;
            mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);
            system_state->iec_on = false;
        }
    } else if (system_state->battery <= 10.0 && system_state->fuel <= 5.0 && system_state->accelerator == true) {
        system_state->transition_factor = 0.0;
        cmd.type = CMD_SET_POWER;
        cmd.power_level = 0.0;
        mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
        mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);
        
    }

    // Smoother consumption
    if (system_state->ev_on && system_state->accelerator) {
        system_state->battery -= 0.2;
        if (system_state->battery < 0.0) system_state->battery = 0.0;
    }
    if (system_state->iec_on && system_state->accelerator) {
        system_state->fuel -= 0.01;
        if (system_state->fuel < 0.0) system_state->fuel = 0.0;
    }

    // Smoother HV battery recharging
    if (system_state->iec_on) {
        system_state->battery += 0.01;
        if (system_state->battery > MAX_BATTERY) system_state->battery = MAX_BATTERY;
    } else if (current_speed > MIN_SPEED && !system_state->accelerator && !system_state->brake) {
        system_state->battery += 0.05;
        if (system_state->battery > MAX_BATTERY) system_state->battery = MAX_BATTERY;
    } else if (system_state->brake && current_speed > MIN_SPEED) {
        system_state->battery += 0.2;
        if (system_state->battery > MAX_BATTERY) system_state->battery = MAX_BATTERY;
    }

    // Check mode
    if (system_state->ev_on && !system_state->iec_on) {
        system_state->power_mode = 0;
    } else if (system_state->ev_on && system_state->iec_on) {
        system_state->power_mode = 1;
    } else if (!system_state->ev_on && system_state->iec_on) {
        system_state->power_mode = 2;
    } else if (!system_state->ev_on && !system_state->iec_on && current_speed > MIN_SPEED) {
        system_state->power_mode = 3;
    } else {
        system_state->power_mode = 4;
    }


    sem_post(sem);
}

// Function to read user input for pedal control
void *read_input(void *arg) {
    char input[10];
    while (running) {
        fgets(input, sizeof(input), stdin);
        // Remove trailing newline
        input[strcspn(input, "\n")] = 0;

        if (strcmp(input, "0") == 0) {
            set_braking(false);
            set_acceleration(false);
            sem_wait(sem);
            system_state->iec_on = false;
            system_state->ev_on = false;
            sem_post(sem);
        } else if (strcmp(input, "1") == 0) {
            set_acceleration(true);
            set_braking(false);
        } else if (strcmp(input, "2") == 0) {
            set_acceleration(false);
            set_braking(true);
            sem_wait(sem);
            system_state->iec_on = false;
            system_state->ev_on = false;
            sem_post(sem);
        }
        usleep(10000); // Small delay to avoid busy-waiting
    }
    return NULL;
}

int main() {
    // Configure signals
    signal(SIGUSR1, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

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
    init_system_state(system_state);

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

    pthread_t input_thread;
    if (pthread_create(&input_thread, NULL, read_input, NULL) != 0) {
        perror("[VMU] Error creating input thread");
        running = 0; // Exit main loop if thread creation fails
    }

    while (running) {
        if (!paused) {
            vmu_control_engines();
            calculate_speed(system_state);

            display_status(system_state);

            usleep(200000); // Update every 200 ms
        } else {
            sleep(1);
        }
    }

    // Cleanup
    EngineCommand cmd;
    cmd.type = CMD_END;
    mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
    mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);
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

    printf("[VMU] Shut down complete.\n");
    return 0;
}