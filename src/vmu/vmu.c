// VMU (Vehicle Management Unit) - Main control system for hybrid vehicle
#include "vmu.h"

#define TIMEOUT_SECONDS 5

// Definições das variáveis globais
SystemState *system_state = NULL;
sem_t *sem = NULL;
mqd_t ev_mq = (mqd_t)-1;
mqd_t iec_mq = (mqd_t)-1;
volatile sig_atomic_t running = 1;
pthread_t input_thread;

volatile sig_atomic_t paused = 0;
unsigned char cont = 0;
char lastmsg[35] = {0};
unsigned char safetyCount = 0;
int ciclesQuantity = 0;
double iecTransitionRatio = 0.0;
double evTransitionRatio = 0.0;
bool transitionIEC = false;
bool transitionEV = false;
long int elapsed = 0;
long int remaining = 0;
bool start = true;
long delay_ms = 0;
int transition = 0;
double expectedvalueEV = 0.0;
double expectedValueIEC = 0.0;
bool finish = false;
bool carStop = false;

bool batteryEmpty = false;
bool fuelEmpty = false;
bool batteryNotEmpty = false;
bool evNotFull = false;
bool carStopped = false;
bool iecTransitionActive = false;
bool evTransitionActive = false;
bool speedAtEVRange = false;
bool speedAboveEVRange = false;
bool fuelNotEmpty = false;
bool batteryAtFull = false;
bool evPercentZero = false;
bool vehicleIsParked = false;



void atributeBooleanValues (double current_speed, double current_battery, double current_fuel, bool current_accelerator);


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

int calculateCicleEstimated( SystemState *state ) {
    double localSpeed = 0;
    int cicles = 0;

    while(localSpeed <= state->speed) {
        
        localSpeed = cicles * (1.13 - (0.00162 * cicles));
        if ( cicles == 350 ) {
            break;
        }

        cicles++;

    }

    return cicles;
}

// Function to calculate the current speed based on engine RPM and transition factor
void calculate_speed(SystemState *state) {
    double fator_variacao = 0.0;
    sem_wait(sem); // Acquire the semaphore
    
    if (state->accelerator) {
        ciclesQuantity = calculateCicleEstimated(state);
        // Calculate speed increase based on RPM of both engines and the transition factor
        state->speed = ciclesQuantity*(1.13 - (0.00162 * ciclesQuantity));
        

    } 
    
    else if (!state->brake) {
        // Coasting - apply a slight deceleration
        state->speed = fmax(state->speed - 0.3, MIN_SPEED); // Ensure speed does not go below the minimum
    } 
    
    else {
        // Braking - apply a stronger deceleration
        double fator_variacao_freio = 2.0;
        state->speed = fmax(state->speed - fator_variacao_freio, MIN_SPEED);
    }

    sem_post(sem); // Release the semaphore
    
}

static inline bool nearly_equal(double a, double b) {
    return (fabs(a - b) < NEAR_ZERO);
}

void atributeBooleanValues (double current_speed, double current_battery, double current_fuel, bool current_accelerator) {

    batteryEmpty = nearly_equal(current_battery, IS_EMPTY);
    fuelEmpty = nearly_equal(current_fuel, IS_EMPTY);
    batteryNotEmpty = (current_battery > IS_EMPTY); // R13.3 replaced ==/!= with nearly_equal; relational OK */
    evNotFull = (system_state->evPercentage < ACT_ALONE);
    carStopped = carStop; 
    iecTransitionActive = transitionIEC;
    evTransitionActive = transitionEV;
    speedAtEVRange = (current_speed <= MAX_EV_SPEED);
    speedAboveEVRange = !speedAtEVRange;                            
    fuelNotEmpty = !fuelEmpty;
    batteryAtFull = ( (current_battery > CHARGE_FULL) || nearly_equal(current_battery, CHARGE_FULL) );
    evPercentZero = nearly_equal(system_state->evPercentage, INACTIVE);
    vehicleIsParked = nearly_equal(current_speed, PARKED);

}

void dbg_clear(SystemState *s) {
    
    // turn the string empty  
    s->debg[0] = '\0';

}

void dbg_char(SystemState *s, char c) {
    
    size_t used = strlen(s->debg);
    size_t rem  = MSG_SIZE - used;
    
    // Only write if the character + NUL fits
    if (rem > 1U) {
        s->debg[used] = c;
        s->debg[used + 1U] = '\0';
    }

}

void dbg_string(SystemState *s, const char *src) {

    while ((*src != '\0') && (strlen(s->debg) < MSG_SIZE - 1U)) {

        dbg_char(s, *src++);
    
    }

}

// MISRA-compliant simple itoa
static void my_itoa(int32_t val, char *buf, size_t bufsize) {
    
    if (bufsize == 0U) return;
    
    uint32_t u = (val < 0) ? (uint32_t)(-val) : (uint32_t)val;
    char *p = buf;
    

    do {
        *p++ = (char)('0' + (u % 10U));
        u /= 10U;

    } while ((u > 0U) && ((size_t)(p - buf) < bufsize - 1U));
    

    if ((val < 0) && ((size_t)(p - buf) < bufsize - 1U)) {
        *p++ = '-';
    }
    
    *p = '\0';
    
    // inverts string 
    for (char *q = buf, *r = p - 1; q < r; ++q, --r) {
        char tmp = *q; *q = *r; *r = tmp;
    }
}

// Emits int32_t decimal into debg */
void dbg_int(SystemState *s, int32_t v) {

    char num[12U] = { 0 };       
    my_itoa(v, num, sizeof(num));
    dbg_string(s, num);

}

// Main function
void vmu_control_engines(void) {
    
    EngineCommandEV  cmdEV;
    EngineCommandIEC cmdIEC;

    sem_wait(sem);  // Acquire the semaphore

    // Load current system state 
    double current_speed = system_state->speed;
    double current_battery = system_state->battery;
    double current_fuel = system_state->fuel;
    bool current_accelerator = system_state->accelerator;

    // Recalculate boolean variables to fit MISRA-C rules
    atributeBooleanValues(current_speed, current_battery, current_fuel, current_accelerator);
    
    // START condition after stop when battery recharged */
    if (carStopped && batteryAtFull) {
        carStop = false;
    }

    // Handle stop condition when both battery and fuel depleted 
    else if (batteryEmpty && fuelEmpty) {
        current_accelerator = false;
        carStop = true;
    }

    // No-fuel, start EV-only transition 
    else if (batteryNotEmpty && fuelEmpty && !evTransitionActive && evNotFull) {
        
        dbg_clear(system_state);
        dbg_string(system_state, "Fuel empty, starting EV transition cycle %d                               ");
        dbg_int(system_state, cont);

        transitionEV = true;
    }


    // Combustion-only - hybrid or EV-only when battery full or fuel empty
    else if ((iecTransitionActive && batteryAtFull) || fuelEmpty) {
        transitionIEC = false;
        transitionEV  = true;
    }


    // Gradual transition to IEC mode (combustion only)
    if (iecTransitionActive) {

        dbg_clear(system_state);
        dbg_string(system_state, "IEC transition in progress %d                                             ");
        dbg_int(system_state, cont);

        system_state->evPercentage  -= HALF_PERCENT; // HALF_PERCENT = 0.005
        system_state->iecPercentage += HALF_PERCENT;
        system_state->power_mode     = HYBRID;   // Power mode set to Hybrid 

        if (system_state->iecPercentage >= ACT_ALONE) {

            system_state->evPercentage = INACTIVE; // INACTIVE = 0.0
            system_state->iecPercentage = ACT_ALONE; // ACT_ALONE = 1.0
            system_state->power_mode = COMBUSTION_ONLY; // COMBUSTION_ONLY = 2 (INT)
            system_state->ev_on = false;
        
        }
    }


    // Gradual transition to EV or hybrid mode 
    else if (evTransitionActive && evNotFull) {
        
        dbg_clear(system_state);
        dbg_string(system_state, "EV transition in progress %d                                              ");
        dbg_int(system_state, cont);

        // speedAtEVRange = 45.0
        if (speedAtEVRange && fuelNotEmpty) {

            // Smoothly transition (0.5% per cycle) 
            system_state->evPercentage += HALF_PERCENT;
            system_state->iecPercentage -= HALF_PERCENT;
            system_state->power_mode = HYBRID;

            // Calculate EV RPM estimated
            double evRpm = system_state->evPercentage * ((current_speed * CIRCUNFERENCE_RATIO) / TIRE_PERIMETER);

            // If RPM estimated of EV is above than limit, recalculate evPercentage based current speed
            if (evRpm > MAX_EV_RPM) {

                system_state->evPercentage  = MAX_EV_RPM / ((current_speed * CIRCUNFERENCE_RATIO) / TIRE_PERIMETER);
                system_state->iecPercentage = ACT_ALONE - system_state->evPercentage;
            }

            // End trantion if evPercentage reaches 1.0 (ACT_ALONE)
            if (system_state->evPercentage >= ACT_ALONE) {

                system_state->evPercentage = ACT_ALONE;
                system_state->iecPercentage = INACTIVE;
                system_state->power_mode = ELETRIC_ONLY;
                transitionEV = false;
            }
        }

        else if (speedAboveEVRange && fuelNotEmpty) {

            // Calculate expected EV and IEC percentage based current speed
            double evp  = MAX_EV_SPEED / current_speed;
            double iecp = (current_speed - MAX_EV_SPEED) / current_speed;
            
            // Apply smooth transitiom
            system_state->power_mode = HYBRID;
            system_state->evPercentage += HALF_PERCENT;
            system_state->iecPercentage -= HALF_PERCENT;

            dbg_clear(system_state);
            dbg_string(system_state, "Hybrid EV transition activated %d                                                  ");
            dbg_int(system_state, cont);

            // If current EV percentage surpass the expected value, ends transition
            if (system_state->evPercentage >= evp) {

                system_state->evPercentage = evp;
                system_state->iecPercentage = iecp;
                transitionEV = false;
            }
        
        }

        // If fuel is empty, the system do other transition algorithm
        else if (fuelEmpty) {

            if (speedAtEVRange) {
                system_state->evPercentage = ACT_ALONE;
                system_state->iecPercentage = INACTIVE;
            }

            else {
                system_state->evPercentage += TWO_PERCENT;
                system_state->iecPercentage -= TWO_PERCENT;
            }

            if (system_state->evPercentage >= ACT_ALONE) {
                transitionEV = false;
                system_state->evPercentage = ACT_ALONE;
                system_state->iecPercentage = INACTIVE;
            }

            system_state->power_mode = ELETRIC_ONLY;
        }
    }

    // Hybrid driving logic when both sources available 
    else if (speedAboveEVRange && (current_battery >= TEN_PERCENT) && fuelNotEmpty && !evTransitionActive) {
        
        dbg_clear(system_state);
        dbg_string(system_state, "Car running at standard hybrid mode (hybrid) %d                        ");
        dbg_int(system_state, cont);

        if (!iecTransitionActive) {

            double evp  = MAX_EV_SPEED / current_speed;
            double iecp = (current_speed - MAX_EV_SPEED) / current_speed;

            system_state->power_mode = HYBRID;
            system_state->evPercentage = evp;
            system_state->iecPercentage = iecp;
        }

        else {
        
            system_state->power_mode = COMBUSTION_ONLY;
        
        }
    }

    // EV-only when below speed threshold and battery sufficient
    else if (speedAtEVRange && (current_battery > TEN_PERCENT) && !evTransitionActive) {
        
        dbg_clear(system_state);
        dbg_string(system_state, "Car running at standard hybrid mode (EV only) %d                                ");
        dbg_int(system_state, cont);

        system_state->evPercentage = ACT_ALONE;
        system_state->iecPercentage = INACTIVE;
        system_state->power_mode = ELETRIC_ONLY;
    
    }
    
    // Activate transition to IEC-only when battery low 
    else if ((current_battery <= TEN_PERCENT) && fuelNotEmpty && !evTransitionActive) {
        
        if (!iecTransitionActive) {
        
            if (vehicleIsParked) {

                system_state->evPercentage = INACTIVE;
                system_state->iecPercentage = ACT_ALONE;
                system_state->power_mode = COMBUSTION_ONLY;
                system_state->ev_on = false;
            }
            
            else {
                transitionIEC = true;
            }
        }
    }

    // Fallback default
    else {
        dbg_clear(system_state);
        dbg_string(system_state, "Default engine command %d                                   ");
        dbg_int(system_state, cont);
    }

    // Disable Accelerator if fuel is empty and speed above EV speed limit
    if (fuelEmpty && (current_speed >= MAX_EV_SPEED)) {
        system_state->accelerator = false;
    }


    // Assign and send commands
    cmdEV.globalVelocity = current_speed;
    cmdEV.toVMU          = false;
    cmdEV.accelerator    = current_accelerator;
    cmdEV.power_level    = system_state->evPercentage;
    cmdEV.type           = CMD_START;
    cmdEV.iec_fuel       = system_state->fuel;

    cmdIEC.globalVelocity = current_speed;
    cmdIEC.toVMU          = false;
    cmdIEC.ev_on          = system_state->ev_on;
    cmdIEC.power_level    = system_state->iecPercentage;
    cmdIEC.type           = CMD_START;

    mq_send(ev_mq,  (const char *)&cmdEV,  sizeof(cmdEV),  0);
    mq_send(iec_mq, (const char *)&cmdIEC, sizeof(cmdIEC), 0);

    sem_post(sem);  // Release the semaphore

}


// This function verify if the message received is to VMU and update the system values from EV and IEC
void vmu_check_queue (unsigned char counter, mqd_t mqd, bool ev) {
    
    unsigned char localcount = 0;
    unsigned char ret;
    EngineCommandEV cmdEV;
    EngineCommandIEC cmdIEC;
    
    sem_wait(sem);

    // Receive message from EV
    if (ev) {

        // Wait for message
        while (mq_receive(mqd, (char *)&cmdEV, sizeof(cmdEV), NULL) != -1) {
            
            if (cmdEV.toVMU) {
                localcount++;
                strcpy(lastmsg, cmdEV.check);
                system_state->rpm_ev = cmdEV.rpm_ev;
                system_state->battery = cmdEV.batteryEV;
                system_state->ev_on = cmdEV.evActive;  
                safetyCount = 0;
            }

            else {
                safetyCount += 1;
            } 
        }

        if (localcount == 0 && cmdEV.toVMU) {
            strcpy(lastmsg, "nt");
            safetyCount++;
            if (safetyCount == 5) {
                system_state->safety = true;
            }
            
        }
        
        else if (!cmdEV.toVMU) {
            mq_send(mqd, (const char *)&cmdEV, sizeof(cmdEV), 0);
        }
    }
    
    else {

        while (mq_receive(mqd, (char *)&cmdIEC, sizeof(cmdIEC), NULL) != -1) {
            if (cmdIEC.toVMU) {
                localcount++;
                strcpy(lastmsg, cmdIEC.check);
                system_state->rpm_iec = cmdIEC.rpm_iec;
                system_state->fuel = cmdIEC.fuelIEC;
                system_state->iec_on = cmdIEC.iecActive;
                safetyCount = 0;
                
                if (system_state->fuel < 0.0) {
                    system_state->fuel = 0.0;
                
                }

            } 
        }

        if (localcount == 0 && cmdIEC.toVMU) {
            strcpy(lastmsg, "nt");
            safetyCount++;
            if (safetyCount == 5) {
                system_state->safety = true;
            }
            
        }

        else if (!cmdIEC.toVMU) {
            mq_send(mqd, (const char *)&cmdIEC, sizeof(cmdIEC), 0);
        }
    }
    sem_post(sem);

}


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