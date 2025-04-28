#include "interface.h"

// Function to read user input for pedal control in a separate thread
void *read_input(void *arg) {
    char input[10];
    while (running) {
        fgets(input, sizeof(input), stdin);
        // Remove trailing newline character from input
        input[strcspn(input, "\n")] = 0;

        // Process the user input to control acceleration and braking
        if (strcmp(input, "0") == 0 && !finish) {
            set_braking(false);
            set_acceleration(false);
            sem_wait(sem);
            system_state->iec_on = false;
            system_state->ev_on = false;
            sem_post(sem);
        } else if (strcmp(input, "1") == 0 && !finish) {
            set_acceleration(true);
            set_braking(false);
        } else if (strcmp(input, "2") == 0 ) {
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

// Function to display the current state of the system
void display_status(const SystemState *state) {
    printf("\033[H"); 
    printf("\n=== Estado do Sistema ===\n\n");
    printf("Speed: %03.0f km/h\n", state->speed);
    printf("RPM EV: %03.0f\n", state->rpm_ev);
    printf("RPM IEC: %04.0f\n\n", state->rpm_iec);
    printf("Eletric engine ratio: %f \n", state->evPercentage);
    printf("Combustion engine ratio: %f \n", state->iecPercentage);
    printf("EV: %s\n", state->ev_on ? "ON " : "OFF");
    printf("IEC: %s\n\n", state->iec_on ? "ON " : "OFF");
    printf("Temperature EV: %.2f C\n", state->temp_ev);
    printf("Temperature IEC: %.2f C\n\n", state->temp_iec);
    printf("Battery: %05.2f%%\n", state->battery);
    printf("Fuel (liters): %05.3f\n\n", state->fuel);
    printf("Power mode: %s\n", 
           state->power_mode == 0 ? "Electric Only        " : 
           state->power_mode == 1 ? "Hybrid               " : 
           state->power_mode == 2 ? "Combustion Only      " : "None             " );
    printf("Accelerator: %s\n", state->accelerator ? "ON " : "OFF");
    printf("Brake: %s\n", state->brake ? "ON " : "OFF");
    printf("Counter: %d\n", cont);
    printf("Last mensage: %s", state->debg);
    printf("\nTransition EV: %s", transitionEV ? "ON " : "OFF" );
    printf("\nType `1` for accelerate, `2` for brake, or `0` for none, and press Enter:\n");
    cont++;
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
    state->transitionCicles = 0;
    strcpy(state->debg, "Nops");
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