// vmu.h
#ifndef VMU_H
#define VMU_H

#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>
#include <mqueue.h>

// Define names for shared memory, semaphore, and message queues
#define SHARED_MEM_NAME "/hybrid_car_shared_data"
#define SEMAPHORE_NAME "/hybrid_car_semaphore"
#define EV_COMMAND_QUEUE_NAME "/ev_command_queue"
#define IEC_COMMAND_QUEUE_NAME "/iec_command_queue"
#define UPDATE_INTERVAL 1

// Constants
#define MAX_SPEED 160.0         // Maximum vehicle speed (km/h)
#define MIN_SPEED 0.0           // Minimum vehicle speed (km/h)
#define MAX_BATTERY 100.0       // Maximum battery charge (%)
#define MAX_FUEL 100.0          // Maximum fuel level (%)

// Hybrid Logic Constants
#define EV_ONLY_SPEED_LIMIT 60.0 // Max speed when on EV only due to low fuel
#define ELECTRIC_ONLY_SPEED_THRESHOLD 40.0 // Speed below which EV is primary/only source (km/h)
#define IEC_MAX_POWER_SPEED         160.0 // Speed at which IEC reaches 100% power in relevant modes (km/h)
#define EV_ONLY_FUEL_LOW_SPEED_LIMIT 60.0 // Max speed when on EV only due to low fuel (user requirement)
#define EV_ASSIST_SPEED_LIMIT 70.0 // Max speed for EV assistance in general (user requirement)


#define BATTERY_CRITICAL_THRESHOLD  10.0 // Battery level below which EV is limited (%)
#define FUEL_CRITICAL_THRESHOLD     5.0  // Fuel level below which IEC is limited (%)
#define BATTERY_RECHARGE_THRESHOLD 70.0 // Battery percentage to re-enable EV motor

#define POWER_INCREASE_RATE       0.02    // Rate at which power levels increase per control loop iteration
#define POWER_DECREASE_RATE       0.05    // Rate at which power levels decrease per control loop iteration
#define BATTERY_CONSUMPTION_RATE    0.01   // Base battery consumption per power unit per iteration
#define FUEL_CONSUMPTION_RATE       0.005  // Base fuel consumption per power unit per iteration
#define REGEN_COAST_RATE            0.005   // Battery regen rate during coasting
#define REGEN_BRAKE_RATE            0.02    // Battery regen rate during braking
#define IEC_RECHARGE_RATE           0.002   // Battery recharge rate when IEC is running (e.g., charging battery)


#define ENGINE_BRAKE_EV_FACTOR     2.0   // Electric motor resistance when not powered
#define ENGINE_BRAKE_IEC_FACTOR    6.0   // ICE engine braking force factor
#define DRIVETRAIN_LOSS_FACTOR     0.15  // Power loss in drivetrain at max speed (15%)
#define SPEED_CHANGE_SMOOTHING     0.5  // Smoothing factor for speed changes (0-1)

// Vehicle Dynamics and Engine Torque Curve Constants (Simplified)
#define EV_BASE_RPM             2000    // RPM where EV transitions from constant torque to constant power
#define MAX_EV_RPM              10000    // Maximum RPM for EV
#define MAX_EV_TORQUE_NM        350.0   // Maximum torque of the EV motor (Nm)
#define MAX_EV_POWER_KW         (MAX_EV_TORQUE_NM * 1000) // Calculate max EV power

#define IEC_IDLE_RPM            800     // Idle RPM for IEC
#define IEC_TORQUE_AT_IDLE_NM   50.0    // Small torque at idle if engine is on (Nm)
#define IEC_PEAK_TORQUE_RPM     3500    // RPM where IEC torque peaks
#define MAX_IEC_TORQUE_NM       250.0   // Maximum torque of the IEC engine (Nm)
#define MAX_IEC_RPM             6000    // Maximum RPM for IEC

// Other constants
#define PI 3.141592653589              // Pi constant

// Structure for system state
typedef struct {
    bool accelerator; // True if accelerator is pressed
    bool brake;       // True if brake is pressed
    double speed;     // Current vehicle speed (km/h)
    int rpm_ev;       // EV motor RPM
    int rpm_iec;      // IEC engine RPM
    bool ev_on;       // True if EV motor is running
    bool iec_on;      // True if IEC engine is running
    double temp_ev;   // EV motor temperature (C)
    double temp_iec;  // IEC engine temperature (C)
    double battery;   // Battery level (%)
    double fuel;      // Fuel level (%) 
    int power_mode; // 0: Hybrid, 1: Electric Only, 2: Combustion Only, 3: Regenerative Braking, 4: Parked
    double ev_power_level; // Commanded power level for EV (0.0 to 1.0)
    double iec_power_level; // Commanded power level for IEC (0.0 to 1.0)
    bool was_accelerating;
} SystemState;

// Structure for messages (if needed for communication beyond commands)
typedef struct {
    char command;
    int value; // Example value
} Message;

// Enumeration for engine commands
typedef enum {
    CMD_START,
    CMD_STOP,
    CMD_SET_POWER,
    CMD_END
} CommandType;

// Structure for engine commands
typedef struct {
    CommandType type;
    double power_level;
} EngineCommand;

// Function prototypes
void set_acceleration(bool accelerate);
void set_braking(bool brake);
double calculate_speed(SystemState *state);
void vmu_control_engines();
void init_system_state(SystemState *state);
void display_status(const SystemState *state);
void init_communication();
void cleanup();
void *read_input(void *arg);
void handle_signal(int sig);

// Declare global variables as extern
extern SystemState *system_state;
extern sem_t *sem;
extern mqd_t ev_mq;
extern mqd_t iec_mq;

#endif