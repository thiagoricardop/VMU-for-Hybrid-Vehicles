// VMU (Vehicle Management Unit) - Main control system for hybrid vehicle
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
#include <pthread.h> 
#include <string.h>  
#include "vmu.h"

/*
To use the VMU, you need first to make the executable files using `make` on VMU-FOR-HYBRID-VEHICLES/src terminal. After that, run each file in a different terminal, starting by ./vmu, then the ./ev and ./iec.
The vmu.c is the main module that controls the vehicle's powertrain, while the ev.c and iec.c are the electric and internal combustion engine modules, respectively.
The VMU communicates with the EV and IEC modules through POSIX message queues, and controls the engines based on the current speed and user input.
*/

/*
After running all executables in different terminals, go to the vmu terminal, press `1` to accelerate and press `enter`.
*/

/*
To shut down the system, press `Ctrl+C` on vmu terminal.
*/

// Global variables
SystemState *system_state; // Pointer to the shared memory structure holding the system state
sem_t *sem;                // Pointer to the semaphore for synchronizing access to shared memory
mqd_t ev_mq, iec_mq;      // Message queue descriptors for communication with EV and IEC modules
volatile sig_atomic_t running = 1; // Flag to control the main loop, volatile to ensure visibility across threads
volatile sig_atomic_t paused = 0;  // Flag to indicate if the simulation is paused

// Function to handle signals (SIGUSR1 for pause, SIGINT/SIGTERM for shutdown)
void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        paused = !paused;
        printf("[VMU] Paused: %s\n", paused ? "true" : "false");
    } else if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
        printf("[VMU] Shutting down...\n");
    }
}

// Function to display the current state of the system
void display_status(const SystemState *state) {
    printf("\033[H"); 
    printf("\n\n=== System State ===\n");
    printf("Speed: %06.2f km/h\n", state->speed);
    printf("RPM EV: %d\n", state->rpm_ev); 
    printf("RPM IEC: %d\n", state->rpm_iec);
    printf("EV Activated: %s\n", state->ev_on ? "Y" : "N");
    printf("IEC Activated: %s\n", state->iec_on ? "Y" : "N");
    printf("Temperature EV: %.2f C\n", state->temp_ev);
    printf("Temperature IEC: %.2f C\n", state->temp_iec);
    printf("Battery: %.2f%%\n", state->battery); 
    printf("Fuel: %.2f%%\n", state->fuel);

    printf("Power mode: %s\n", 
           state->power_mode == 0 ? "E" : 
           state->power_mode == 1 ? "H" : 
           state->power_mode == 2 ? "C" :
           state->power_mode == 3 ? "R" : "P");
    
    printf("Accelerator Activated: %s\n", state->accelerator ? "Y" : "N"); 
    printf("Brake Activated: %s\n", state->brake ? "Y" : "N");


    printf("\nLabels - Power mode");
    printf("\nE -> Electric Only\nH -> Hybrid\nC -> Combustion Only\nR -> Regenerative Braking\nP -> Parked");
    printf("\nType `1` for accelerate, `2` for brake, or `0` for none, and press Enter:\n");

    fflush(stdout); // Garante que a saída seja atualizada imediatamente
}

// Function to initialize the system state
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

// Function to set the accelerator state
void set_acceleration(bool accelerate) {
    sem_wait(sem); // Acquire the semaphore to protect shared memory
    system_state->accelerator = accelerate;
    if (accelerate) {
        system_state->brake = false; // If accelerating, brake is off
    }
    sem_post(sem); // Release the semaphore
}

// Function to set the braking state
void set_braking(bool brake) {
    sem_wait(sem); // Acquire the semaphore
    system_state->brake = brake;
    if (brake) {
        system_state->accelerator = false; // If braking, accelerator is off
    }
    sem_post(sem); // Release the semaphore
}

// Function to calculate the current speed based on engine RPM and transition factor
double calculate_speed(SystemState *state) {
    double fator_variacao = 0.0;
    sem_wait(sem); // Acquire the semaphore
    if (state->accelerator) {
        // Calculate speed increase based on RPM of both engines and the transition factor
        fator_variacao = (state->rpm_ev * (1.0 - state->transition_factor) + state->rpm_iec * state->transition_factor) * 0.0001;
        state->speed = fmin(state->speed + fator_variacao, MAX_SPEED); // Ensure speed does not exceed the maximum
    } else if (!state->brake) {
        // Coasting - apply a slight deceleration
        state->speed = fmax(state->speed - 0.3, MIN_SPEED); // Ensure speed does not go below the minimum
    } else {
        // Braking - apply a stronger deceleration
        double fator_variacao_freio = 2.0;
        state->speed = fmax(state->speed - fator_variacao_freio, MIN_SPEED);
    }
    double current_speed = state->speed;
    sem_post(sem); // Release the semaphore
    return current_speed;
}

// Function to control the engines based on the current speed and system state
void vmu_control_engines() {
    EngineCommand cmd;
    sem_wait(sem); // Acquire the semaphore
    double current_speed = system_state->speed;
    double current_battery = system_state->battery;
    double current_fuel = system_state->fuel;
    bool current_accelerator = system_state->accelerator;
    sem_post(sem); // Release the semaphore

    // Logic for controlling engine transitions based on speed and user input
    if (current_speed >= TRANSITION_SPEED_THRESHOLD - (TRANSITION_ZONE_WIDTH / 2.0) &&
        current_speed <= TRANSITION_SPEED_THRESHOLD + (TRANSITION_ZONE_WIDTH / 2.0)
        && current_accelerator && current_battery > 10.0 && current_fuel > 5.0) {
        sem_wait(sem);
        // Calculate the transition factor based on the distance from the threshold
        double distancia_do_limite = fabs(current_speed - TRANSITION_SPEED_THRESHOLD);
        system_state->transition_factor = distancia_do_limite / (TRANSITION_ZONE_WIDTH / 2.0);
        system_state->transition_factor = fmin(1.0, fmax(0.0, system_state->transition_factor)); // Clamp the value between 0 and 1
        double transition_factor = system_state->transition_factor;
        sem_post(sem);
        
        // Send power commands to EV and IEC based on the transition factor
        cmd.type = CMD_SET_POWER;
        cmd.power_level = 1.0 - transition_factor; // EV power decreases as transition factor increases
        mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);

        cmd.type = CMD_SET_POWER;
        cmd.power_level = transition_factor; // IEC power increases as transition factor increases
        mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);
        
        // Start/stop IEC based on the transition factor
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
        
        // Start/stop IEC based on the transition factor
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

    } else if (current_speed < TRANSITION_SPEED_THRESHOLD - (TRANSITION_ZONE_WIDTH / 2.0) && current_accelerator && current_battery > 10.0) {
        // Below transition zone, use only EV
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
    } else if (current_speed > TRANSITION_SPEED_THRESHOLD + (TRANSITION_ZONE_WIDTH / 2.0) && current_accelerator && current_fuel > 5.0) {
        // Above transition zone, use only IEC
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

    // Emergency mode: If battery is low, switch to IEC if fuel is available
    sem_wait(sem);
    if (current_battery <= 10.0 && current_speed < TRANSITION_SPEED_THRESHOLD && current_fuel > 5.0 && current_accelerator == true) {
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
    } else if (current_fuel <= 5.0 && current_speed >= TRANSITION_SPEED_THRESHOLD && current_accelerator == true && current_battery > 10.0) {
        // Emergency mode: If fuel is low, switch to EV if battery is available
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
    } else if (current_battery <= 10.0 && current_fuel <= 5.0 && current_accelerator == true) {
        // If both battery and fuel are low, stop both engines (for safety or simulation end)
        system_state->transition_factor = 0.0;
        cmd.type = CMD_SET_POWER;
        cmd.power_level = 0.0;
        mq_send(ev_mq, (const char *)&cmd, sizeof(cmd), 0);
        mq_send(iec_mq, (const char *)&cmd, sizeof(cmd), 0);
        
    }

    // Smoother consumption of battery when EV is on and accelerating
    if (system_state->ev_on && current_accelerator) {
        system_state->battery -= 0.2;
        if (current_battery < 0.0) system_state->battery = 0.0; // Ensure battery doesn't go below 0
    }
    if (system_state->iec_on && current_accelerator) {
        system_state->fuel -= 0.01;
        if (current_fuel < 0.0) system_state->fuel = 0.0;
    }

    // Smoother HV battery recharging logic
    if (system_state->iec_on) {
        system_state->battery += 0.01; // Recharge slowly when IEC is running
        if (current_battery > MAX_BATTERY) system_state->battery = MAX_BATTERY; // Ensure battery doesn't exceed max
    } else if (current_speed > MIN_SPEED && !current_accelerator && !system_state->brake) {
        system_state->battery += 0.05; // Regenerative recharge during coasting
        if (current_battery > MAX_BATTERY) system_state->battery = MAX_BATTERY;
    } else if (system_state->brake && current_speed > MIN_SPEED) {
        system_state->battery += 0.2; // More aggressive recharge during braking
        if (current_battery > MAX_BATTERY) system_state->battery = MAX_BATTERY;
    }

    // Update the power mode based on the engine states
    if (system_state->ev_on && !system_state->iec_on) {
        system_state->power_mode = 0; // Electric Only
    } else if (system_state->ev_on && system_state->iec_on) {
        system_state->power_mode = 1; // Hybrid
    } else if (!system_state->ev_on && system_state->iec_on) {
        system_state->power_mode = 2; // Combustion Only
    } else if (!system_state->ev_on && !system_state->iec_on && current_speed > MIN_SPEED && system_state->brake) {
        system_state->power_mode = 3; // Regenerative Braking
    } else {
        system_state->power_mode = 4; // Parked (neither engine on, or stopped)
    }

    sem_post(sem); // Release the semaphore
}

// Function to read user input for pedal control in a separate thread
void *read_input(void *arg) {
    char input[10];
    while (running) {
        fgets(input, sizeof(input), stdin);
        // Remove trailing newline character from input
        input[strcspn(input, "\n")] = 0;

        // Process the user input to control acceleration and braking
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
    // Configure signal handlers for graceful shutdown and pause
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

    // Configuration of POSIX message queues for communication with the EV module
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

    // Configuration of POSIX message queues for communication with the IEC module
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

    // Create a separate thread to read user input for pedal control
    pthread_t input_thread;
    if (pthread_create(&input_thread, NULL, read_input, NULL) != 0) {
        perror("[VMU] Error creating input thread");
        running = 0; // Exit main loop if thread creation fails
    }

    // Main loop of the VMU module
    while (running) {
        if (!paused) {
            vmu_control_engines(); // Control the engines based on the system state
            calculate_speed(system_state); // Calculate the current speed
            display_status(system_state);  // Display the current system status
            usleep(200000); // Sleep for 200 milliseconds
        } else {
            sleep(1); // Sleep for 1 second if paused
        }
    }

    // Cleanup resources before exiting
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