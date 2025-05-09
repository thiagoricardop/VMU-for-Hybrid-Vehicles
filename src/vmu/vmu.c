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
VMU (Vehicle Management Unit) - Main control system for the hybrid vehicle.
Communicates with EV and IEC modules via POSIX message queues and shared memory.
Controls engine states based on speed, user input, battery, and fuel levels.

Usage:
1. Build docker image using `make docker` in VMU-FOR-HYBRID-VEHICLES.
1. Build executables using `docker run --rm -it -v $(pwd):/app vmu-dev make` in VMU-FOR-HYBRID-VEHICLES.
2. Run the executables using tmux with 'make run' in VMU-FOR-HYBRID-VEHICLES.
3. In VMU terminal: '1' to accelerate, '2' to brake, '0' to coast.
4. Press Ctrl+C in VMU terminal to shut down.
*/

// Global variables
SystemState *system_state; // Pointer to the shared memory structure holding the system state
sem_t *sem;                // Pointer to the semaphore for synchronizing access to shared memory
mqd_t ev_mq, iec_mq;      // Message queue descriptors for communication with EV and IEC modules
// Create a separate thread to read user input for pedal control
pthread_t input_thread;
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

// Displays the current system state to the console
void display_status(const SystemState *state) {
    printf("\033[H"); // ANSI escape code to move cursor to top-left
    printf("\n\n=== System State ===                \n");
    printf("Speed: %06.2f km/h                      \n", state->speed);
    printf("RPM EV: %d                              \n", state->rpm_ev);
    printf("RPM IEC: %d                             \n", state->rpm_iec);
    printf("EV: %s\n", state->ev_on ? "ON " : "OFF");
    printf("IEC: %s\n", state->iec_on ? "ON " : "OFF");
    printf("EV Power: %.2f%%                        \n", state->ev_power_level * 100.0); // Display new power levels
    printf("IEC Power: %.2f%%                       \n", state->iec_power_level * 100.0); // Display new power levels
    printf("Temperature EV: %.2f C                  \n", state->temp_ev);
    printf("Temperature IEC: %.2f C                 \n", state->temp_iec);
    printf("Battery: %.2f%%                         \n", state->battery);
    printf("Fuel: %.2f%%                            \n", state->fuel);
    printf("Power mode: %s\n", 
        state->power_mode == 0 ? "Electric Only        " : 
        state->power_mode == 1 ? "Hybrid               " : 
        state->power_mode == 2 ? "Combustion Only      " : 
        state->power_mode == 3 ? "Regenerative Braking " :
        state->power_mode == 5 ? "IEC Charging/Idle    " : "Parked/Coasting      ");
    printf("Accelerator: %s               \n", state->accelerator ? "ON " : "OFF");
    printf("Brake: %s                     \n", state->brake ? "ON " : "OFF");
    printf("\nType `1` for accelerate, `2` for brake, or `0` for none, and press Enter:\n");

    fflush(stdout); // Ensure output is displayed immediately
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
    state->power_mode = 4; // Parked mode initially
    state->ev_power_level = 0.0;
    state->iec_power_level = 0.0;
    state->was_accelerating = false;
}

// Sets the accelerator state in shared memory (thread-safe)
void set_acceleration(bool accelerate) {
    sem_wait(sem);
    system_state->accelerator = accelerate;
    if (accelerate) {
        system_state->brake = false; // Ensure brake is off if accelerating
    }
    sem_post(sem);
}

// Sets the braking state in shared memory (thread-safe)
void set_braking(bool brake) {
    sem_wait(sem);
    system_state->brake = brake;
    if (brake) {
        system_state->accelerator = false; // Ensure accelerator is off if braking
    }
    sem_post(sem);
}

// Calculates the vehicle speed based on current state and commanded power
// Note: This is a simplified physics model.
double calculate_speed(SystemState *state) {
    // Cache needed values locally to minimize semaphore lock time
    double current_speed_kmh;
    bool is_accelerating, is_braking;
    double ev_power_level, iec_power_level;
    bool ev_on, iec_on;

    sem_wait(sem);
    current_speed_kmh = state->speed;
    is_accelerating = state->accelerator;
    is_braking = state->brake;
    ev_power_level = state->ev_power_level;
    iec_power_level = state->iec_power_level;
    ev_on = state->ev_on;
    iec_on = state->iec_on;
    sem_post(sem);

    double speed_change = 0.0;

    if (is_accelerating) {
        // --- For acceleration: simple linear relationship between power and speed increase ---
        
        // EV contribution - only up to 70 km/h
        double ev_contribution = 0.0;
        if (ev_on && current_speed_kmh <= 70.0) {
            // Linear contribution based on power level
            // Reduce contribution as we approach the 70 km/h limit
            if (current_speed_kmh > 60.0) {
                double fade_factor = 1.0 - ((current_speed_kmh - 60.0) / 10.0);
                ev_contribution = ev_power_level * fade_factor; // Simple linear factor
            } else {
                ev_contribution = ev_power_level* 5; // Simple acceleration rate
            }
        }
        
        // IEC contribution at all speeds
        double iec_contribution = 0.0;
        if (iec_on) {
            iec_contribution = iec_power_level * 5; // Simple linear factor
        }
        
        // Total acceleration is the sum of both contributions
        speed_change = ev_contribution + iec_contribution;
        
        // Simple speed-dependent efficiency loss (slower acceleration at higher speeds)
        double efficiency_factor = 1.0 - (current_speed_kmh / MAX_SPEED) * 0.8;
        speed_change *= efficiency_factor;
        
    } else {
        // --- When not accelerating: simple deceleration ---
        
        // Base deceleration rate (air resistance, rolling resistance, etc.)
        double base_deceleration = 0.05; // Base deceleration rate when coasting
        
        // Speed-dependent deceleration (higher speeds decelerate faster)
        double speed_factor = current_speed_kmh / 50.0; // Normalized to 50 km/h
        double deceleration = base_deceleration * (1.0 + speed_factor * 0.5);
        
        // Engine braking effect
        if (current_speed_kmh > 1.0) {
            if (ev_on) deceleration += 0.2;
            if (iec_on) deceleration += 0.4;
        }
        
        // Apply deceleration
        speed_change = -deceleration;
        
        // Additional braking force if brake is pressed
        if (is_braking && current_speed_kmh > 0.001) {
            // Simple linear braking model
            double brake_force = 10; // Base braking rate
            
            speed_change -= brake_force; 
        }
    }
    
    // Apply smoothing for more natural feel
    speed_change *= SPEED_CHANGE_SMOOTHING;
    
    // Update speed
    double new_speed = current_speed_kmh + speed_change;
    
    // Ensure speed stays within limits
    if (new_speed < MIN_SPEED) new_speed = MIN_SPEED;
    if (new_speed > MAX_SPEED) new_speed = MAX_SPEED;

    // Update shared state with minimal lock time - only update speed
    sem_wait(sem);
    state->speed = new_speed;
    double updated_speed = state->speed;
    sem_post(sem);

}


// Main logic for controlling EV and IEC based on system state
void vmu_control_engines() {
    // Local variables to store shared state values
    double current_speed;
    double current_battery;
    double current_fuel;
    bool current_accelerator;
    bool current_brake;
    bool current_ev_on;
    bool current_iec_on;
    double current_ev_power_level;
    double current_iec_power_level;
    bool was_accelerating;
    int power_mode;

    // Initial reading of necessary values from shared state
    sem_wait(sem);
    current_speed = system_state->speed;
    current_battery = system_state->battery;
    current_fuel = system_state->fuel;
    current_accelerator = system_state->accelerator;
    current_brake = system_state->brake;
    current_ev_on = system_state->ev_on; 
    current_iec_on = system_state->iec_on;
    current_ev_power_level = system_state->ev_power_level; 
    current_iec_power_level = system_state->iec_power_level; 
    was_accelerating = system_state->was_accelerating;
    power_mode = system_state->power_mode; 
    sem_post(sem);

    
    double target_ev_power = 0.0;
    double target_iec_power = 0.0;
    bool battery_ok = (current_battery > BATTERY_CRITICAL_THRESHOLD);
    bool fuel_ok = (current_fuel > FUEL_CRITICAL_THRESHOLD);

    // Determine the *desired* state (on/off) and target power levels based on VMU logic
    bool desired_ev_on;
    bool desired_iec_on;

    // Use local variables for power levels during calculation and ramping
    double calculated_ev_power_level = current_ev_power_level; // Start ramp from current commanded level
    double calculated_iec_power_level = current_iec_power_level; // Start ramp from current commanded level

    bool new_was_accelerating = was_accelerating; // Update was_accelerating locally
    int new_power_mode = power_mode; // Calculate new power mode

    // Commands to be sent - initialize with a 'no command' type if needed
    EngineCommand ev_cmd = {0}; // Assuming 0 is an invalid command type
    EngineCommand iec_cmd = {0}; // Assuming 0 is an invalid command type
    bool send_ev_cmd = false;
    bool send_iec_cmd = false;

    // --- VMU Logic to Determine Desired State and Target Power ---
    if (current_accelerator) {
        
        new_was_accelerating = true;

        if (battery_ok && fuel_ok) {
            // Common mode (EV Only or Hybrid)
            if (current_speed < ELECTRIC_ONLY_SPEED_THRESHOLD) {
                // (< 40 km/h)
                new_power_mode = 0; // EV Only Mode
                desired_iec_on = false;
                target_iec_power = 0.0;

                desired_ev_on = true;
                target_ev_power = 0.1 + (current_speed - MIN_SPEED) / (ELECTRIC_ONLY_SPEED_THRESHOLD - MIN_SPEED);
                target_ev_power = fmin(fmax(target_ev_power, 0.0), 1.0);

            } else {
                // (>= 40 km/h)
                new_power_mode = 1; // Hybrid Mode
                desired_ev_on = true;
                target_ev_power = 1.0; // EV provides full power in hybrid acceleration

                desired_iec_on = true;
                // IEC power scales with speed after EV_ONLY_SPEED_THRESHOLD
                target_iec_power = 0.1 + (current_speed - ELECTRIC_ONLY_SPEED_THRESHOLD) / (IEC_MAX_POWER_SPEED - ELECTRIC_ONLY_SPEED_THRESHOLD);
                target_iec_power = fmin(fmax(target_iec_power, 0.0), 1.0);
            }
        } else if (!battery_ok && fuel_ok) {
            
             new_power_mode = 2; // IEC Only mode
             desired_ev_on = false; // Ensure EV motor is off
             target_ev_power = 0.0; // Ensure EV power is zero

             // Use IEC for propulsion and charging
             desired_iec_on = true; // Ensure IEC motor is on
             // IEC power scales with speed for propulsion
             target_iec_power = 0.1 + (current_speed) / (IEC_MAX_POWER_SPEED);
             target_iec_power = fmin(fmax(target_iec_power, 0.0), 1.0);

             // Note: The transition back to hybrid is implicitly handled
             // in the next cycle when battery_ok becomes true.

        } else if (battery_ok && !fuel_ok) {
            new_power_mode = 0; // EV Only mode
            desired_iec_on = false; // Ensure IEC motor is off
            target_iec_power = 0.0; // Ensure IEC power is zero

            desired_ev_on = true; // Ensure EV motor is on
            if (current_speed < EV_ONLY_SPEED_LIMIT) {
                 // EV power scales with speed up to limit
                 target_ev_power = 0.1 + (current_speed - MIN_SPEED) / (EV_ONLY_SPEED_LIMIT - MIN_SPEED);
                 target_ev_power = fmin(fmax(target_ev_power, 0.0), 1.0);
            } else {
                 // Speed limit reached, reduce EV power to maintain speed or slowly decelerate
                 target_ev_power = fmax(0.0, 0.5 - (current_speed - EV_ONLY_SPEED_LIMIT) * 0.05); // Reduce power gradually above limit
            }

        } else {
            new_power_mode = 4; // Emergency/No propulsion
            desired_ev_on = false;
            desired_iec_on = false;
            target_ev_power = 0.0;
            target_iec_power = 0.0;
        }

        // Ramp up power levels towards target when accelerating
        if (calculated_ev_power_level < target_ev_power) {
            calculated_ev_power_level = fmin(calculated_ev_power_level + POWER_INCREASE_RATE, target_ev_power);
        } else if (calculated_ev_power_level > target_ev_power) {
            calculated_ev_power_level = fmax(calculated_ev_power_level - POWER_DECREASE_RATE, target_ev_power);
        }

        if (calculated_iec_power_level < target_iec_power) {
            calculated_iec_power_level = fmin(calculated_iec_power_level + POWER_INCREASE_RATE, target_iec_power);
        } else if (calculated_iec_power_level > target_iec_power) {
            calculated_iec_power_level = fmax(calculated_iec_power_level - POWER_DECREASE_RATE, target_iec_power);
        }


    } else { // Not accelerating
        new_was_accelerating = false;
        target_ev_power = 0.0; // Target is zero when not accelerating
        target_iec_power = 0.0; // Target is zero when not accelerating (except for charging)

        // Decrease power levels towards zero when not accelerating
        calculated_ev_power_level = fmax(calculated_ev_power_level - POWER_DECREASE_RATE, 0.0);
        calculated_iec_power_level = fmax(calculated_iec_power_level - POWER_DECREASE_RATE, 0.0);

        // Determine desired engine states when not accelerating
        // Default to off, exceptions below
        desired_ev_on = false;
        desired_iec_on = false;

        // Keep IEC on for charging if conditions met
        bool keep_iec_for_charge = (!current_accelerator && !current_brake && // Not actively accelerating or braking
                                     current_speed > MIN_SPEED && // Vehicle is moving (coast charging)
                                     current_battery < 100 * 0.8 && // Battery not full
                                     fuel_ok);

        // If in !battery ok && fuel ok state and not accelerating/braking, keep IEC on for charging regardless of speed (if fuel ok)
        if (!battery_ok && fuel_ok && !current_brake) {
             keep_iec_for_charge = true;
             target_iec_power = 0.2; // Use a fixed power level for charging when stationary or coasting
             // Ramp up to charging power if below it
             if (calculated_iec_power_level < target_iec_power) {
                  calculated_iec_power_level = fmin(calculated_iec_power_level + POWER_INCREASE_RATE, target_iec_power);
             }
             desired_iec_on = true; // Ensure IEC is on for charging
        } 


        // Regenerative braking logic determines power mode, but doesn't necessarily keep EV motor 'on' for propulsion
        if (current_brake && current_speed > MIN_SPEED) {
            new_power_mode = 3; // Regenerative Braking mode
             desired_ev_on = false; // VMU is not requesting EV propulsion
             desired_iec_on = false; // IEC is off during regen braking
        } else if (current_speed > MIN_SPEED) {
             // Coasting modes - Engines should ideally be off unless needed for charging
             desired_ev_on = false; // Not requesting EV propulsion while coasting
        } else { // Vehicle is stopped or near stopped
             desired_ev_on = false; // EV is off when stopped and not accelerating/braking
        }

         // If calculated power levels drop very low, set desired_on to false,
         // unless kept on for charging.
         if (!keep_iec_for_charge && calculated_iec_power_level < 0.01) desired_iec_on = false;
         if (calculated_ev_power_level < 0.01) desired_ev_on = false;


        // Determine power mode when not accelerating - based on desired state
        if (current_brake && current_speed > MIN_SPEED) {
            new_power_mode = 3; // Regenerative Braking
        } else if (current_speed > MIN_SPEED) {
             // Coasting modes - based on which engines are desired to be on
             if (desired_ev_on && desired_iec_on) new_power_mode = 1; // Hybrid Coasting
             else if (desired_ev_on) new_power_mode = 0; // EV Only Coasting
             else if (desired_iec_on) new_power_mode = 2; // IEC Only Coasting (possibly charging)
             else new_power_mode = 4; // Coasting without propulsion (engines off)
        } else { // Vehicle stopped
             if(desired_iec_on) { // IEC kept on for charging while stopped
                 new_power_mode = 5; // IEC Charging/Idle
             } else {
                 new_power_mode = 4; // Engines off
             }
        }
    }

    // --- Command Preparation ---
    // Based on current actual state (read from shared memory) and desired state (calculated by VMU), prepare commands to send to modules.

    // EV Commands
    if (desired_ev_on && !current_ev_on) {
        // VMU wants EV ON, but it's currently OFF -> Send START command
        ev_cmd.type = CMD_START;
        send_ev_cmd = true;
        // Don't send SET_POWER in the same cycle as START. Module needs to report ON first.
    } else if (!desired_ev_on && current_ev_on) {
        // VMU wants EV OFF, but it's currently ON -> Send STOP command
        ev_cmd.type = CMD_STOP;
        send_ev_cmd = true;
    }

    // If the EV is currently ON (reported by the module), send the SET_POWER command with the calculated power level.
    // This handles cases where VMU wants it ON (ramping up/stable) or wants it OFF but it's still ON (ramping down by VMU logic).
    // Only send SET_POWER if the engine is actually reported as ON.
    if (current_ev_on) {
         ev_cmd.type = CMD_SET_POWER; // Overwrite START/STOP if already set (prioritize START/STOP for state change)
         
    }

    // Refined EV Command Logic: Prioritize state changes (START/STOP), then send power levels if engine is ON.
    EngineCommand final_ev_cmd = {0};
    bool final_send_ev_cmd = false;

    if (desired_ev_on && !current_ev_on) {
        final_ev_cmd.type = CMD_START;
        final_send_ev_cmd = true;
    } else if (!desired_ev_on && current_ev_on) {
        final_ev_cmd.type = CMD_STOP;
        final_send_ev_cmd = true;
    }

    // If engine is ON, always send the current calculated power level.
    // This will update the module's power even if a START command was sent in a previous cycle
    // and the module is now reporting ON.
    if (current_ev_on) {
         // If a STOP command was prepared in this cycle, don't send SET_POWER.
         if (final_ev_cmd.type != CMD_STOP) {
             final_ev_cmd.type = CMD_SET_POWER; // SET_POWER command
             final_ev_cmd.power_level = calculated_ev_power_level;
             final_send_ev_cmd = true;
         }
    }

    // IEC Commands (Refined Logic similar to EV)
    EngineCommand final_iec_cmd = {0};
    bool final_send_iec_cmd = false;

     if (desired_iec_on && !current_iec_on) {
         final_iec_cmd.type = CMD_START;
         final_send_iec_cmd = true;
     } else if (!desired_iec_on && current_iec_on) {
          final_iec_cmd.type = CMD_STOP;
          final_send_iec_cmd = true;
     }

     if (current_iec_on) {
          if (final_iec_cmd.type != CMD_STOP) {
              final_iec_cmd.type = CMD_SET_POWER; // SET_POWER command
              final_iec_cmd.power_level = calculated_iec_power_level;
              final_send_iec_cmd = true;
          }
     }


    // Calculate battery and fuel consumption/recharge based on *actual* engine state (from shared memory)
    // and *commanded* power levels (calculated by VMU for this cycle).
    double new_battery = current_battery; // Start with current state
    double new_fuel = current_fuel;       // Start with current state

    // Consume battery when EV is actually ON and commanded to provide power (> 0)
    if (current_ev_on && calculated_ev_power_level > 0) {
         new_battery -= calculated_ev_power_level * BATTERY_CONSUMPTION_RATE;
         if (new_battery < 0.0) new_battery = 0.0;
    }

    // Consume fuel only when IEC is actually ON and commanded to provide power (> 0)
    if (current_iec_on && calculated_iec_power_level > 0) {
          new_fuel -= calculated_iec_power_level * FUEL_CONSUMPTION_RATE;
          if (new_fuel < 0.0) new_fuel = 0.0;
    }

    // Recharge when IEC is actually ON and fuel is available.
    if (current_iec_on && fuel_ok && new_battery < 100) {
         new_battery += IEC_RECHARGE_RATE;
         if (new_battery > 100) new_battery = 100;
    }

    // Regenerative braking logic - recharge battery when braking or coasting
    // This logic uses current speed and brake state to calculate the regen amount.
    if (!current_accelerator && current_speed > MIN_SPEED && new_battery < 100) { // Only regenerate if battery is not full and car is moving/braking
         if (current_brake) {
             new_battery += REGEN_BRAKE_RATE * (current_speed / MAX_SPEED);
             if (new_battery > 100) new_battery = 100;
         } else {
             // Regenerative braking can happen slightly even when coasting at speed
             new_battery += REGEN_COAST_RATE * (current_speed / MAX_SPEED);
             if (new_battery > 100) new_battery = 100;
         }
     }


    
    // This represents the mode the VMU is attempting to achieve.
     if (!current_accelerator && !current_brake && current_speed < MIN_SPEED + 0.1 && !desired_ev_on && !desired_iec_on) {
           new_power_mode = 4; // Parked/Coasting
     } else if (current_brake && current_speed > MIN_SPEED) {
           new_power_mode = 3; // Regenerative Braking
     } else if (desired_ev_on && !desired_iec_on) {
           new_power_mode = 0; // EV Only (VMU is requesting EV only)
     } else if (desired_ev_on && desired_iec_on) {
           new_power_mode = 1; // Hybrid (VMU is requesting both)
     } else if (!desired_ev_on && desired_iec_on) {
           // Differentiate between IEC propulsion and IEC charging based on acceleration
           if (current_accelerator) {
               new_power_mode = 2; // IEC Only Propulsion (VMU requesting IEC only while accelerating)
           } else {
               new_power_mode = 5; // IEC Charging/Idle (VMU requesting IEC only while not accelerating, likely for charge)
           }
     } else {
           new_power_mode = 4; // Coasting/Emergency without propulsion (Neither engine desired on)
     }


    sem_wait(sem);
    // Update shared state with new values
    system_state->ev_power_level = calculated_ev_power_level; 
    system_state->iec_power_level = calculated_iec_power_level; 
    system_state->was_accelerating = new_was_accelerating;
    system_state->power_mode = new_power_mode; 
    system_state->battery = new_battery;       
    system_state->fuel = new_fuel;             
    sem_post(sem);

    // --- Send Commands ---
    // Send prepared commands to engine modules via message queues
    if (final_send_ev_cmd) {
        mq_send(ev_mq, (const char *)&final_ev_cmd, sizeof(final_ev_cmd), 0);
    }

    if (final_send_iec_cmd) {
        mq_send(iec_mq, (const char *)&final_iec_cmd, sizeof(final_iec_cmd), 0);
    }
}


// Function to initialize communication with EV and IEC modules
void init_communication(){
    // Configure signal handlers for graceful shutdown and pause
    signal(SIGUSR1, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Configuration of shared memory for VMU
    int shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[VMU] Error opening shared memory");
        running = 0; // Exit main loop
    }

    // Configure the size of the shared memory
    if (ftruncate(shm_fd, sizeof(SystemState)) == -1) {
        perror("[VMU] Error configuring shared memory size");
        running = 0; // Exit main loop
    }

    // Map shared memory into VMU's address space
    system_state = (SystemState *)mmap(NULL, sizeof(SystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (system_state == MAP_FAILED) {
        perror("[VMU] Error mapping shared memory");
        running = 0; // Exit main loop
    }
    close(shm_fd);

    // Create semaphore for synchronization
    sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("[VMU] Error creating semaphore");
        running = 0; // Exit main loop
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
        running = 0; // Exit main loop
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
        running = 0; // Exit main loop
    }

    printf("VMU Module Running\n");

    // Create thread to handle user input
    if (pthread_create(&input_thread, NULL, read_input, NULL) != 0) {
        perror("[VMU] Error creating input thread");
        running = 0; // Exit main loop if thread creation fails
    }
}

void cleanup() {
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
}

void *read_input(void *arg) {
    char input[10];
    while (running) {
        fgets(input, sizeof(input), stdin);
        // Remove trailing newline character from input
        input[strcspn(input, "\n")] = 0;

        // Process the user input to control acceleration and braking
        if (strcmp(input, "0") == 0) {
            // Desligar acelerador e freio
            set_braking(false);
            set_acceleration(false);
            
        } else if (strcmp(input, "1") == 0) {
            set_acceleration(true);
            set_braking(false);
        } else if (strcmp(input, "2") == 0) {
            set_acceleration(false);
            set_braking(true);
            
        }
        usleep(10000); // Small delay to avoid busy-waiting
    }
    return NULL;
}

