#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>

// Include your project headers (adjust paths as needed)
#include "../../src/iec/iec.h"
#include "../../src/vmu/vmu.h"

// --- Declare external globals from iec.c ---
// These are declared in iec.c, we need to access them for testing setup/teardown
extern SystemState *system_state;
extern sem_t *sem;
extern mqd_t iec_mq_receive;
extern volatile sig_atomic_t running;
extern volatile sig_atomic_t paused;

// --- Declare variables for the resources *created by the test setup* (simulating VMU) ---
// These descriptors/pointers are held by the test suite to interact with the resources
// that iec.c will open.
static SystemState *test_vmu_system_state = NULL;
static sem_t *test_vmu_sem = NULL;
static mqd_t test_vmu_iec_mq_send = (mqd_t)-1; // MQ descriptor for sending commands *to* IEC

// --- Helper function to simulate VMU's resource creation ---
// This sets up the environment that iec.c's init_communication expects.
// This function is NOT part of the IEC code being tested, it's test infrastructure.
void create_vmu_resources() {
    // Shared Memory
    int shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Test Setup: Error creating shared memory");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd, sizeof(SystemState)) == -1) {
        perror("Test Setup: Error configuring shared memory size");
        close(shm_fd);
        shm_unlink(SHARED_MEM_NAME);
        exit(EXIT_FAILURE);
    }
    test_vmu_system_state = (SystemState *)mmap(NULL, sizeof(SystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (test_vmu_system_state == MAP_FAILED) {
        perror("Test Setup: Error mapping shared memory");
        close(shm_fd);
        shm_unlink(SHARED_MEM_NAME);
        exit(EXIT_FAILURE);
    }
    close(shm_fd); // Close fd after mapping

    // Semaphore
    test_vmu_sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1); // Initial value 1
    if (test_vmu_sem == SEM_FAILED) {
        perror("Test Setup: Error creating semaphore");
        munmap(test_vmu_system_state, sizeof(SystemState));
        shm_unlink(SHARED_MEM_NAME);
        exit(EXIT_FAILURE);
    }

    // Message Queue (IEC command queue - VMU sends to this)
    struct mq_attr mq_attributes;
    mq_attributes.mq_flags = 0;
    mq_attributes.mq_maxmsg = 10;
    mq_attributes.mq_msgsize = sizeof(EngineCommand);
    mq_attributes.mq_curmsgs = 0; // Ignored for open/create

    test_vmu_iec_mq_send = mq_open(IEC_COMMAND_QUEUE_NAME, O_WRONLY | O_CREAT | O_NONBLOCK, 0666, &mq_attributes);
    if (test_vmu_iec_mq_send == (mqd_t)-1) {
         perror("Test Setup: Error creating/opening IEC message queue for sending");
         munmap(test_vmu_system_state, sizeof(SystemState));
         shm_unlink(SHARED_MEM_NAME);
         sem_close(test_vmu_sem);
         sem_unlink(SEMAPHORE_NAME);
         exit(EXIT_FAILURE);
    }

    // Initialize System State (as VMU would)
    sem_wait(test_vmu_sem);
    memset(test_vmu_system_state, 0, sizeof(SystemState)); // Zero out state
    test_vmu_system_state->rpm_ev = 0;
    test_vmu_system_state->temp_ev = 25.0; // Ambient
    test_vmu_system_state->ev_on = false;
    test_vmu_system_state->ev_power_level = 0.0;

    test_vmu_system_state->rpm_iec = 0;
    test_vmu_system_state->temp_iec = 25.0; // Ambient
    test_vmu_system_state->iec_on = false;
    test_vmu_system_state->iec_power_level = 0.0;
    sem_post(test_vmu_sem);

    printf("Test Setup: VMU resources created.\n");
}

// --- Helper function to simulate VMU's resource cleanup ---
// This unlinks the resources created in the setup.
void cleanup_vmu_resources() {
    if (test_vmu_iec_mq_send != (mqd_t)-1) {
        mq_close(test_vmu_iec_mq_send);
        mq_unlink(IEC_COMMAND_QUEUE_NAME);
        test_vmu_iec_mq_send = (mqd_t)-1;
    }

    if (test_vmu_system_state != MAP_FAILED && test_vmu_system_state != NULL) {
        munmap(test_vmu_system_state, sizeof(SystemState));
        shm_unlink(SHARED_MEM_NAME);
        test_vmu_system_state = NULL;
    }

     if (test_vmu_sem != SEM_FAILED && test_vmu_sem != NULL) {
        sem_close(test_vmu_sem);
        sem_unlink(SEMAPHORE_NAME);
        test_vmu_sem = NULL;
    }

    printf("Test Teardown: VMU resources unlinked.\n");
}


// --- Test Fixture Setup Function ---
// This runs before each test in the TCase.
void iec_setup(void) {
    // 1. Simulate VMU creating resources
    create_vmu_resources();

    // 2. Call the actual iec.c init_communication function
    // Ensure global flags are reset for each test
    running = 1;
    paused = 0;
    init_communication();

    // Basic check that init_communication succeeded (optional, setup failing is often enough)
    // ck_assert_ptr_ne(system_state, MAP_FAILED);
    // ck_assert_ptr_ne(sem, SEM_FAILED);
    // ck_assert_int_ne(iec_mq_receive, (mqd_t)-1);

    printf("Test Setup: IEC init_communication called.\n");
}

// --- Test Fixture Teardown Function ---
// This runs after each test in the TCase.
void iec_teardown(void) {
    // 1. Call the actual iec.c cleanup function
    cleanup();

    // Basic check that cleanup seems to have run (optional)
    // ck_assert_ptr_eq(system_state, MAP_FAILED); // Assuming cleanup sets global to MAP_FAILED/NULL
    // ck_assert_ptr_eq(sem, SEM_FAILED);
    // ck_assert_int_eq(iec_mq_receive, (mqd_t)-1);

    // 2. Clean up the resources created by the test setup (simulating VMU unlink)
    cleanup_vmu_resources();

    printf("Test Teardown: IEC cleanup and VMU resources cleaned.\n");
}

// --- Individual Test Cases ---

START_TEST(test_iec_init_communication_success)
{
    // This test primarily verifies that the setup function (which calls init_communication)
    // completes without errors and that the global variables are initialized.
    // The assertions in the setup function itself could cover this.
    // You could add more specific checks here if needed, e.g., permissions, flags.
    ck_assert_ptr_ne(system_state, MAP_FAILED);
    ck_assert_ptr_ne(sem, SEM_FAILED);
    ck_assert_int_ne(iec_mq_receive, (mqd_t)-1);
    ck_assert_int_eq(running, 1);
    ck_assert_int_eq(paused, 0);
}
END_TEST

START_TEST(test_iec_receive_cmd_start)
{
    EngineCommand cmd = { .type = CMD_START };

    // Simulate VMU sending the command
    int ret = mq_send(test_vmu_iec_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    // Give a moment for the message to be available, though O_NONBLOCK might not need it
    // usleep(10000); // Short delay if needed, though mq_receive O_NONBLOCK handles EAGAIN

    // Call the function under test
    receive_cmd();

    // Verify state change in shared memory
    sem_wait(test_vmu_sem); // Acquire semaphore to read shared state
    ck_assert_msg(test_vmu_system_state->iec_on == true, "IEC should be ON after START command");
    ck_assert_msg(test_vmu_system_state->rpm_iec == IEC_IDLE_RPM, "IEC RPM should be IDLE after START command");
    sem_post(test_vmu_sem); // Release semaphore
}
END_TEST

START_TEST(test_iec_receive_cmd_stop)
{
    // First, set the state as if the engine was running
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = IEC_IDLE_RPM;
    test_vmu_system_state->temp_iec = 50.0;
    sem_post(test_vmu_sem);

    EngineCommand cmd = { .type = CMD_STOP };

    // Simulate VMU sending the command
    int ret = mq_send(test_vmu_iec_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    // Call the function under test
    receive_cmd();

    // Verify state change in shared memory
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->iec_on == false, "IEC should be OFF after STOP command");
    // RPM reduction is handled in engine(), not receive_cmd, so don't check for 0 RPM here.
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_receive_cmd_set_power)
{
    // According to the code comment, VMU updates iec_power_level *before* sending the message.
    // We simulate this by setting the shared memory state first.

    double test_power_level = 0.75;

    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_power_level = test_power_level; // Simulate VMU writing
    sem_post(test_vmu_sem);

    EngineCommand cmd = { .type = CMD_SET_POWER, .power_level = test_power_level }; // Value in msg is informational for IEC

    // Simulate VMU sending the command
    int ret = mq_send(test_vmu_iec_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    // Call the function under test
    receive_cmd();

    // Verify receive_cmd doesn't change power_level, it just processes the message.
    // The effect is seen in engine(). We just check that the command was received without error.
    // A successful mq_receive and no fprintf indicates success for this message type.
    // This test primarily confirms receive_cmd *handles* CMD_SET_POWER without crashing/erroring.
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->iec_power_level == test_power_level, "IEC power level should remain as set by VMU");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_receive_cmd_end)
{
    EngineCommand cmd = { .type = CMD_END };

    // Simulate VMU sending the command
    int ret = mq_send(test_vmu_iec_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    // Call the function under test
    receive_cmd();

    // Verify the running flag is set to 0
    ck_assert_msg(running == 0, "Running flag should be 0 after END command");
}
END_TEST

START_TEST(test_iec_receive_cmd_unknown)
{
    // Test the default case in the switch statement
    EngineCommand cmd = { .type = CMD_UNKNOWN };

    // Simulate VMU sending the command
    int ret = mq_send(test_vmu_iec_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    // Call the function under test
    receive_cmd();

    // The function should handle the unknown command without crashing
    // No specific state change to verify
    ck_assert(true);
}
END_TEST

START_TEST(test_iec_receive_cmd_empty_queue)
{
    // First, ensure the queue is empty by draining it
    EngineCommand dummy;
    while (mq_receive(iec_mq_receive, (char *)&dummy, sizeof(dummy), NULL) != -1) {
        // Keep reading until queue is empty
    }

    // Now call receive_cmd with an empty queue
    receive_cmd();

    // The function should handle empty queue without errors
    // No state change expected
    ck_assert(true);
}
END_TEST

START_TEST(test_iec_engine_rpm_at_target)
{
    // Set initial state with RPM already at target
    int target_rpm = IEC_IDLE_RPM + (int)(0.5 * (MAX_IEC_RPM - IEC_IDLE_RPM));
    
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = target_rpm;
    test_vmu_system_state->temp_iec = 70.0;
    test_vmu_system_state->iec_power_level = 0.5; // 50% power matches target_rpm
    sem_post(test_vmu_sem);

    // Call the function under test
    engine();

    // Verify RPM stays at target (or very close due to floating point)
    sem_wait(test_vmu_sem);
    ck_assert_msg(abs(test_vmu_system_state->rpm_iec - target_rpm) <= 1, 
                  "RPM should remain at target when already there");
    // Temperature should still increase
    ck_assert_msg(test_vmu_system_state->temp_iec > 70.0, "Temperature should increase even at steady RPM");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_idle_rpm_enforcement)
{
    // Set initial state with engine on but RPM below idle (edge case)
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = IEC_IDLE_RPM - 200; // Below idle RPM
    test_vmu_system_state->temp_iec = 40.0;
    test_vmu_system_state->iec_power_level = 0.0; // No power commanded
    sem_post(test_vmu_sem);

    // Call the function under test
    engine();

    // Verify RPM gets enforced to at least idle
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_iec == IEC_IDLE_RPM, 
                  "RPM should be set to idle when engine is on and RPM below idle");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_temp_already_ambient)
{
    // Set initial state: engine off, already at ambient temperature
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = false;
    test_vmu_system_state->rpm_iec = 0;
    test_vmu_system_state->temp_iec = 25.0; // Ambient temperature
    test_vmu_system_state->iec_power_level = 0.0;
    sem_post(test_vmu_sem);

    // Call the function under test
    engine();

    // Verify temperature remains at ambient
    sem_wait(test_vmu_sem);
    ck_assert_msg(fabs(test_vmu_system_state->temp_iec - 25.0) < 0.001, 
                  "Temperature should remain at ambient when already there");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_max_rpm_limit)
{
    // Set initial state with maximum power commanded and RPM approaching max
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = MAX_IEC_RPM - 100; // Just below max
    test_vmu_system_state->temp_iec = 90.0;
    test_vmu_system_state->iec_power_level = 1.0; // Max power
    sem_post(test_vmu_sem);

    // Call the function under test
    engine();

    // Verify RPM increases but is limited by target (MAX_IEC_RPM)
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_iec > (MAX_IEC_RPM - 100), 
                  "RPM should increase when below target");
    ck_assert_msg(test_vmu_system_state->rpm_iec <= MAX_IEC_RPM, 
                  "RPM should not exceed maximum rpm");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_handle_signal_unrecognized)
{
    // Set initial state of the flags
    running = 1;
    paused = false;
    
    // Send a signal that's not explicitly handled
    handle_signal(SIGHUP); // Using SIGHUP as an example unhandled signal
    
    // Verify flags remain unchanged
    ck_assert_msg(running == 1, "Running flag should remain unchanged for unrecognized signal");
    ck_assert_msg(paused == false, "Paused flag should remain unchanged for unrecognized signal");
}
END_TEST

START_TEST(test_iec_engine_idle_to_accelerate)
{
    // Set initial state: engine on, idle, command acceleration
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = IEC_IDLE_RPM;
    test_vmu_system_state->temp_iec = 50.0;
    test_vmu_system_state->iec_power_level = 0.5; // Command 50% power
    sem_post(test_vmu_sem);

    // Call the function under test (simulates one engine tick)
    engine();

    // Verify state change
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->iec_on == true, "Engine should remain ON");
    ck_assert_msg(test_vmu_system_state->rpm_iec > IEC_IDLE_RPM, "RPM should increase from idle");
    ck_assert_msg(test_vmu_system_state->temp_iec > 50.0, "Temperature should increase");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_high_to_decelerate)
{
    // Set initial state: engine on, high RPM, command lower power
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = MAX_IEC_RPM;
    test_vmu_system_state->temp_iec = 90.0;
    test_vmu_system_state->iec_power_level = 0.1; // Command 10% power
    sem_post(test_vmu_sem);

    // Call the function under test
    engine();

    // Verify state change
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->iec_on == true, "Engine should remain ON");
    ck_assert_msg(test_vmu_system_state->rpm_iec < MAX_IEC_RPM, "RPM should decrease from max");
    ck_assert_msg(test_vmu_system_state->rpm_iec >= IEC_IDLE_RPM, "RPM should not drop below idle while ON"); // Ensure it respects idle limit
    ck_assert_msg(test_vmu_system_state->temp_iec > 90.0, "Temperature might still increase slightly or stabilize"); // Temp change depends on rates
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_stop_cooldown)
{
    // Set initial state: engine just turned off, high temp/RPM
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = false; // VMU set this
    test_vmu_system_state->rpm_iec = 3000; // Some non-zero RPM
    test_vmu_system_state->temp_iec = 85.0; // Above ambient
    test_vmu_system_state->iec_power_level = 0.0; // Power commanded to 0
    sem_post(test_vmu_sem);

    // Call the function under test
    engine();

    // Verify state change
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->iec_on == false, "Engine should remain OFF");
    ck_assert_msg(test_vmu_system_state->rpm_iec < 3000, "RPM should decrease when off");
    ck_assert_msg(test_vmu_system_state->rpm_iec >= 0, "RPM should not be negative");
    ck_assert_msg(test_vmu_system_state->temp_iec < 85.0, "Temperature should decrease when off and above ambient");
    ck_assert_msg(test_vmu_system_state->temp_iec >= 25.0, "Temperature should not drop below ambient");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_temp_cap)
{
    // Set initial state: engine on, high temp, high power
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = 5000;
    test_vmu_system_state->temp_iec = 104.0; // Close to max
    test_vmu_system_state->iec_power_level = 1.0; // Max power
    sem_post(test_vmu_sem);

    // Call the function under test
    engine();

    // Verify temperature is capped
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->temp_iec <= 105.0, "Temperature should be capped at 105.0");
    ck_assert_msg(test_vmu_system_state->temp_iec >= 104.0, "Temperature should increase towards cap"); // Assuming increase rate is positive
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_temperature_boundary_exact)
{
    // Test exact temperature boundary condition (105.0)
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = 5000;
    test_vmu_system_state->temp_iec = 105.0; // Exactly at max temperature
    test_vmu_system_state->iec_power_level = 1.0; // Max power
    sem_post(test_vmu_sem);

    // Call the function under test
    engine();

    // Verify temperature stays capped
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->temp_iec == 105.0, 
                  "Temperature should remain capped exactly at 105.0");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_off_rpm_already_zero)
{
    // Test when engine is off and RPM is already 0
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = false;
    test_vmu_system_state->rpm_iec = 0; // RPM already at zero
    test_vmu_system_state->temp_iec = 30.0; // Above ambient
    test_vmu_system_state->iec_power_level = 0.0;
    sem_post(test_vmu_sem);

    // Call the function under test
    engine();

    // Verify RPM stays at 0 and temperature decreases
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_iec == 0, 
                  "RPM should remain at zero when already zero and engine is off");
    ck_assert_msg(test_vmu_system_state->temp_iec < 30.0, 
                  "Temperature should decrease when above ambient");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_power_level_zero)
{
    // Test when engine is on but power level is zero
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = 1000; // Above idle but will decrease
    test_vmu_system_state->temp_iec = 50.0;
    test_vmu_system_state->iec_power_level = 0.0; // No power requested
    sem_post(test_vmu_sem);

    // Call the function under test
    engine();

    // Verify RPM decreases toward idle but not below
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_iec < 1000, 
                  "RPM should decrease when power level is zero");
    ck_assert_msg(test_vmu_system_state->rpm_iec >= IEC_IDLE_RPM, 
                  "RPM should not drop below idle when engine is on");
    ck_assert_msg(test_vmu_system_state->temp_iec >= 50.0, 
                  "Temperature should still increase when engine is on");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_rapid_power_change)
{
    // Test rapid power change (simulates abrupt acceleration)
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = IEC_IDLE_RPM;
    test_vmu_system_state->temp_iec = 60.0;
    test_vmu_system_state->iec_power_level = 0.0; // Start with no power
    sem_post(test_vmu_sem);
    
    // First iteration - engine at idle
    engine();
    
    // Suddenly apply max power
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_power_level = 0.9; // Jump to high power
    sem_post(test_vmu_sem);
    
    // Second iteration - engine should start accelerating
    engine();
    
    // Verify RPM increases but not immediately to max (testing ramp-up logic)
    sem_wait(test_vmu_sem);
    int rpm_after_power_change = test_vmu_system_state->rpm_iec;
    ck_assert_msg(rpm_after_power_change > IEC_IDLE_RPM, 
                  "RPM should increase when power level jumps from 0 to max");
    ck_assert_msg(rpm_after_power_change < MAX_IEC_RPM, 
                  "RPM should not immediately reach max after power change");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_off_with_non_zero_power_level)
{
    // Test when engine is set off but power level is non-zero
    // This tests the target RPM calculation in the off state
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = false;
    test_vmu_system_state->rpm_iec = 2000;
    test_vmu_system_state->temp_iec = 70.0;
    test_vmu_system_state->iec_power_level = 0.1; // Power level non-zero but engine off
    sem_post(test_vmu_sem);

    // Call the function under test
    engine();

    // Verify RPM decreases even with non-zero power level (engine off takes precedence)
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_iec < 2000, 
                  "RPM should decrease when engine is off regardless of power level");
    ck_assert_msg(test_vmu_system_state->temp_iec < 70.0, 
                  "Temperature should decrease when engine is off");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_temperature_change_rates)
{
    // Test temperature change rates at different power levels
    
    // Setup initial state - low power
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = IEC_IDLE_RPM + 500;
    test_vmu_system_state->temp_iec = 50.0;
    test_vmu_system_state->iec_power_level = 0.1; // Low power
    sem_post(test_vmu_sem);
    
    engine(); // First iteration
    
    double temp_low_power;
    sem_wait(test_vmu_sem);
    temp_low_power = test_vmu_system_state->temp_iec;
    test_vmu_system_state->iec_power_level = 0.9; // High power
    sem_post(test_vmu_sem);
    
    engine(); // Second iteration at high power
    
    // Verify temperature increases more rapidly at high power
    sem_wait(test_vmu_sem);
    double temp_high_power = test_vmu_system_state->temp_iec;
    double temp_increase_high = temp_high_power - temp_low_power;
    
    // Reset and test low power again
    test_vmu_system_state->temp_iec = temp_low_power;
    test_vmu_system_state->iec_power_level = 0.1; // Back to low power
    sem_post(test_vmu_sem);
    
    engine(); // Another iteration at low power
    
    sem_wait(test_vmu_sem);
    double temp_low_power_2 = test_vmu_system_state->temp_iec;
    double temp_increase_low = temp_low_power_2 - temp_low_power;
    
    // Higher power should cause faster temperature increase
    ck_assert_msg(temp_increase_high > temp_increase_low, 
                  "Temperature should increase faster at higher power levels");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_rpm_adjustment_limits)
{
    // Test RPM adjustments near limits
    
    // Just below target (small adjustment up)
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    int target_rpm = IEC_IDLE_RPM + (int)(0.5 * (MAX_IEC_RPM - IEC_IDLE_RPM));
    test_vmu_system_state->rpm_iec = target_rpm - 10; // Just below target
    test_vmu_system_state->temp_iec = 70.0;
    test_vmu_system_state->iec_power_level = 0.5; // 50% power
    sem_post(test_vmu_sem);
    
    engine();
    
    // Verify small adjustment up (should be very close to target)
    sem_wait(test_vmu_sem);
    int rpm_close_below = test_vmu_system_state->rpm_iec;
    ck_assert_msg(rpm_close_below >= target_rpm - 10 && rpm_close_below <= target_rpm,
                  "RPM should adjust smoothly when close to target");
                  
    // Just above target (small adjustment down)
    test_vmu_system_state->rpm_iec = target_rpm + 10; // Just above target
    sem_post(test_vmu_sem);
    
    engine();
    
    // Verify small adjustment down
    sem_wait(test_vmu_sem);
    int rpm_close_above = test_vmu_system_state->rpm_iec;
    ck_assert_msg(rpm_close_above <= target_rpm + 10 && rpm_close_above >= target_rpm,
                  "RPM should adjust smoothly when close to target (above)");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_ambient_temperature_approaches)
{
    // Test approaching ambient temperature when cooling
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = false;
    test_vmu_system_state->rpm_iec = 0;
    test_vmu_system_state->temp_iec = 25.1; // Just above ambient
    sem_post(test_vmu_sem);
    
    engine();
    
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->temp_iec < 25.1,
                  "Temperature should decrease when just above ambient");
    ck_assert_msg(test_vmu_system_state->temp_iec >= 25.0,
                  "Temperature should not go below ambient");
                  
    // Now try again with exact ambient temperature
    test_vmu_system_state->temp_iec = 25.0; // Exactly ambient
    sem_post(test_vmu_sem);
    
    engine();
    
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->temp_iec == 25.0,
                  "Temperature should remain at ambient when already there");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_gradual_changes)
{
    // Test multiple iterations to verify gradual changes
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = IEC_IDLE_RPM;
    test_vmu_system_state->temp_iec = 60.0;
    test_vmu_system_state->iec_power_level = 0.8; // High power
    sem_post(test_vmu_sem);
    
    // Run multiple iterations and track the changes
    int previous_rpm, current_rpm;
    double previous_temp, current_temp;
    
    sem_wait(test_vmu_sem);
    previous_rpm = test_vmu_system_state->rpm_iec;
    previous_temp = test_vmu_system_state->temp_iec;
    sem_post(test_vmu_sem);
    
    // Run several iterations of the engine function
    for (int i = 0; i < 5; i++) {
        engine();
        
        sem_wait(test_vmu_sem);
        current_rpm = test_vmu_system_state->rpm_iec;
        current_temp = test_vmu_system_state->temp_iec;
        
        // Verify changes are gradual
        int rpm_change = current_rpm - previous_rpm;
        double temp_change = current_temp - previous_temp;
        
        // Store for next iteration
        previous_rpm = current_rpm;
        previous_temp = current_temp;
        sem_post(test_vmu_sem);
        
        // These assertions verify the changes are reasonable for each step
        ck_assert_msg(rpm_change >= 0, "RPM change should be non-negative during acceleration");
        ck_assert_msg(rpm_change < 1000, "RPM change per iteration should be limited");
        ck_assert_msg(temp_change > 0, "Temperature should increase when engine is running");
        ck_assert_msg(temp_change < 5.0, "Temperature change per iteration should be limited");
    }
}
END_TEST

START_TEST(test_iec_receive_cmd_mq_error_simulation)
{
    // This test is a bit tricky as we can't easily simulate mq_receive errors
    // without modifying the code or using function mocking.
    // A simplified approach is to verify the function handles empty queue gracefully,
    // which we already test in test_iec_receive_cmd_empty_queue
    
    // Drain any existing messages
    EngineCommand dummy;
    while (mq_receive(iec_mq_receive, (char *)&dummy, sizeof(dummy), NULL) != -1) {
        // Keep reading until queue is empty
    }
    
    // Call the function - it should handle EAGAIN error gracefully
    receive_cmd();
    
    // If we get here without crashing, the test passes
    ck_assert(true);
}
END_TEST

START_TEST(test_iec_multi_signal_sequence)
{
    // Test behavior when multiple signals are received in sequence
    running = 1;
    paused = false;
    
    // First pause
    handle_signal(SIGUSR1);
    ck_assert_msg(paused == true, "Paused flag should be true after SIGUSR1");
    ck_assert_msg(running == 1, "Running flag should still be 1");
    
    // Then shutdown
    handle_signal(SIGINT);
    ck_assert_msg(paused == true, "Paused flag should remain true after SIGINT");
    ck_assert_msg(running == 0, "Running flag should be 0 after SIGINT");
    
    // Toggle pause again (should still work even if shutting down)
    handle_signal(SIGUSR1);
    ck_assert_msg(paused == false, "Paused flag should toggle to false");
    ck_assert_msg(running == 0, "Running flag should remain 0");
}
END_TEST

START_TEST(test_iec_temperature_when_just_started)
{
    // Test temperature behavior when engine is just turned on from cold
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true; // Just turned on
    test_vmu_system_state->rpm_iec = IEC_IDLE_RPM; // Start at idle
    test_vmu_system_state->temp_iec = 25.0; // Cold (ambient)
    test_vmu_system_state->iec_power_level = 0.0; // No power commanded
    sem_post(test_vmu_sem);
    
    engine();
    
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->temp_iec > 25.0, 
                  "Temperature should start increasing immediately when engine is on, even at idle");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_edge_case_rpm_near_zero)
{
    // Test behavior when RPM is near zero but not quite zero
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = false;
    test_vmu_system_state->rpm_iec = 100; // Very low RPM but not zero
    test_vmu_system_state->temp_iec = 40.0;
    sem_post(test_vmu_sem);
    
    engine();
    
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_iec < 20, 
                 "RPM should continue decreasing when near zero");
    // Check if it reached exactly zero in one iteration
    // This depends on the RPM decrease rate in the implementation
    sem_post(test_vmu_sem);
}
END_TEST

// --- Signal handler testing (Optional and Advanced) ---
// Testing signal handlers directly in unit tests is complex.
// You would typically test the *logic* inside the handler or use mocking.
// For handle_signal, it modifies global volatile flags. We can test if those flags
// are modified when a signal *would* be received, but we can't easily send a real signal
// within a single-process unit test *to* the handle_signal function without interfering
// with the test runner's signal handling.
// A simple approach is to call the signal handler function directly with the signal number.

START_TEST(test_iec_handle_signal_pause)
{
    paused = false; // Ensure initial state
    handle_signal(SIGUSR1); // Simulate receiving SIGUSR1
    ck_assert_msg(paused == true, "Paused flag should be true after SIGUSR1 when false");

    handle_signal(SIGUSR1); // Simulate receiving SIGUSR1 again
    ck_assert_msg(paused == false, "Paused flag should be false after SIGUSR1 when true");
}
END_TEST

START_TEST(test_iec_handle_signal_shutdown)
{
    running = 1; // Ensure initial state
    handle_signal(SIGINT); // Simulate receiving SIGINT
    ck_assert_msg(running == 0, "Running flag should be 0 after SIGINT");

    running = 1; // Reset
    handle_signal(SIGTERM); // Simulate receiving SIGTERM
    ck_assert_msg(running == 0, "Running flag should be 0 after SIGTERM");
}
END_TEST


// --- Main Test Suite Creation ---

Suite *iec_suite(void) {
    Suite *s;
    TCase *tc_core; // Basic tests
    TCase *tc_commands; // Message queue command tests
    TCase *tc_engine; // Engine logic tests
    TCase *tc_signals; // Signal handler tests (direct call)
    TCase *tc_advanced; // Advanced/edge case tests

    s = suite_create("IEC Module Tests");

    // Core tests (Initialization, cleanup)
    tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, iec_setup, iec_teardown); // Use checked fixture for setup/teardown
    tcase_add_test(tc_core, test_iec_init_communication_success);
    suite_add_tcase(s, tc_core);

    // Command processing tests
    tc_commands = tcase_create("Commands");
    tcase_add_checked_fixture(tc_commands, iec_setup, iec_teardown);
    tcase_add_test(tc_commands, test_iec_receive_cmd_start);
    tcase_add_test(tc_commands, test_iec_receive_cmd_stop);
    tcase_add_test(tc_commands, test_iec_receive_cmd_set_power);
    tcase_add_test(tc_commands, test_iec_receive_cmd_end);
    tcase_add_test(tc_commands, test_iec_receive_cmd_unknown);
    tcase_add_test(tc_commands, test_iec_receive_cmd_empty_queue);
    tcase_add_test(tc_commands, test_iec_receive_cmd_mq_error_simulation);
    suite_add_tcase(s, tc_commands);

    // Engine simulation logic tests
    tc_engine = tcase_create("EngineLogic");
    tcase_add_checked_fixture(tc_engine, iec_setup, iec_teardown);
    tcase_add_test(tc_engine, test_iec_engine_idle_to_accelerate);
    tcase_add_test(tc_engine, test_iec_engine_high_to_decelerate);
    tcase_add_test(tc_engine, test_iec_engine_stop_cooldown);
    tcase_add_test(tc_engine, test_iec_engine_temp_cap);
    tcase_add_test(tc_engine, test_iec_engine_rpm_at_target);
    tcase_add_test(tc_engine, test_iec_engine_idle_rpm_enforcement);
    tcase_add_test(tc_engine, test_iec_engine_temp_already_ambient);
    tcase_add_test(tc_engine, test_iec_engine_max_rpm_limit);
    tcase_add_test(tc_engine, test_iec_engine_temperature_boundary_exact);
    tcase_add_test(tc_engine, test_iec_engine_off_rpm_already_zero);
    tcase_add_test(tc_engine, test_iec_engine_power_level_zero);
    tcase_add_test(tc_engine, test_iec_engine_rapid_power_change);
    tcase_add_test(tc_engine, test_iec_engine_off_with_non_zero_power_level);
    suite_add_tcase(s, tc_engine);

    // Signal handling tests (by direct function call)
    tc_signals = tcase_create("SignalHandlers");
    // No fixture needed as we directly call the handler and check globals
    tcase_add_test(tc_signals, test_iec_handle_signal_pause);
    tcase_add_test(tc_signals, test_iec_handle_signal_shutdown);
    tcase_add_test(tc_signals, test_iec_handle_signal_unrecognized);
    tcase_add_test(tc_signals, test_iec_multi_signal_sequence);
    suite_add_tcase(s, tc_signals);

    // Advanced/edge case tests
    tc_advanced = tcase_create("AdvancedCases");
    tcase_add_checked_fixture(tc_advanced, iec_setup, iec_teardown);
    tcase_add_test(tc_advanced, test_iec_engine_temperature_change_rates);
    tcase_add_test(tc_advanced, test_iec_engine_rpm_adjustment_limits);
    tcase_add_test(tc_advanced, test_iec_engine_ambient_temperature_approaches);
    tcase_add_test(tc_advanced, test_iec_engine_gradual_changes);
    tcase_add_test(tc_advanced, test_iec_temperature_when_just_started);
    tcase_add_test(tc_advanced, test_iec_edge_case_rpm_near_zero);
    suite_add_tcase(s, tc_advanced);


    return s;
}

int main(void) {
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = iec_suite();
    sr = srunner_create(s);

    // Set nofork to debug tests within the same process
    // srunner_set_fork_status(sr, CK_NOFORK);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    // Unlink resources just in case previous runs failed cleanup
    // This is a safeguard, teardown *should* handle this
    cleanup_vmu_resources(); // Call again in main as safeguard

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}