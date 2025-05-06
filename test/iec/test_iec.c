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

#include "../../src/iec/iec.h"
#include "../../src/vmu/vmu.h"

// --- Declare external globals from iec.c ---
extern SystemState *system_state;
extern sem_t *sem;
extern mqd_t iec_mq_receive;
extern volatile sig_atomic_t running;
extern volatile sig_atomic_t paused;
extern int shm_fd; //Shared Memory File Descriptor

// --- Test infrastructure variables (simulating VMU) ---
static SystemState *test_vmu_system_state = NULL;
static sem_t *test_vmu_sem = NULL;
static mqd_t test_vmu_iec_mq_send = (mqd_t)-1; // MQ descriptor for sending commands *to* IEC

// --- Helper function to simulate VMU's resource creation ---
// Sets up the environment that iec.c's init_communication expects.
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
    mq_attributes.mq_curmsgs = 0;

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
    memset(test_vmu_system_state, 0, sizeof(SystemState));
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
void iec_setup(void) {
    // 1. Simulate VMU creating resources
    create_vmu_resources();

    // 2. Call the actual iec.c init_communication function
    running = 1;
    paused = 0;
    init_communication_iec(SHARED_MEM_NAME, SEMAPHORE_NAME, IEC_COMMAND_QUEUE_NAME);

    printf("Test Setup: IEC init_communication called.\n");
}

// --- Test Fixture Teardown Function ---
void iec_teardown(void) {
    // 1. Call the actual iec.c cleanup function
    cleanup();

    // 2. Clean up the resources created by the test setup
    cleanup_vmu_resources();

    printf("Test Teardown: IEC cleanup and VMU resources cleaned.\n");
}

// --- Individual Test Cases ---

START_TEST(test_iec_init_communication_success)
{
    ck_assert_ptr_ne(system_state, MAP_FAILED);
    ck_assert_ptr_ne(sem, SEM_FAILED);
    ck_assert_int_ne(iec_mq_receive, (mqd_t)-1);
    ck_assert_int_eq(running, 1);
    ck_assert_int_eq(paused, 0);
}
END_TEST

// Fail to open the Shared Memory File Descriptor
START_TEST(test_init_communication_shm_fd_fail) {
    init_communication_iec("fail 1", "fail 2", "fail 3");
    ck_assert_int_eq(shm_fd, -1);
}
END_TEST

// Fail to open the Semaphore
START_TEST(test_init_communication_sem_fail) {
    shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if(shm_fd == -1){
        perror("[IEC] Error opening shared Memory");
        exit(EXIT_FAILURE);
    }
    close(shm_fd);

    init_communication_iec(SHARED_MEM_NAME, "fail 2", "fail 3");
    ck_assert_ptr_eq(sem, SEM_FAILED);
    shm_unlink(SHARED_MEM_NAME);
}
END_TEST

// Fail to open IEC's Message Queue
START_TEST(test_init_communication_iec_queue_fail) {
    shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if(shm_fd == -1){
        perror("[IEC] Error opening shared Memory");
        exit(EXIT_FAILURE);
    }
    close(shm_fd);

    sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if(sem == SEM_FAILED){
        perror("[IEC] Error opening semaphore");
        exit(EXIT_FAILURE);
    }
    sem_close(sem);

    init_communication_iec(SHARED_MEM_NAME, SEMAPHORE_NAME, "fail 3");
    ck_assert_int_eq(iec_mq_receive, (mqd_t) - 1);
    shm_unlink(SHARED_MEM_NAME);
    sem_unlink(SEMAPHORE_NAME);
}
END_TEST

START_TEST(test_iec_receive_cmd_start)
{
    EngineCommand cmd = { .type = CMD_START };

    int ret = mq_send(test_vmu_iec_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    receive_cmd();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->iec_on == true, "IEC should be ON after START command");
    ck_assert_msg(test_vmu_system_state->rpm_iec == IEC_IDLE_RPM, "IEC RPM should be IDLE after START command");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_receive_cmd_stop)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = IEC_IDLE_RPM;
    test_vmu_system_state->temp_iec = 50.0;
    sem_post(test_vmu_sem);

    EngineCommand cmd = { .type = CMD_STOP };

    int ret = mq_send(test_vmu_iec_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    receive_cmd();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->iec_on == false, "IEC should be OFF after STOP command");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_receive_cmd_set_power)
{
    double test_power_level = 0.75;

    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_power_level = test_power_level; // Simulate VMU writing
    sem_post(test_vmu_sem);

    EngineCommand cmd = { .type = CMD_SET_POWER, .power_level = test_power_level };

    int ret = mq_send(test_vmu_iec_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    receive_cmd();

    // Verify receive_cmd handles the message type without error.
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->iec_power_level == test_power_level, "IEC power level should remain as set by VMU");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_receive_cmd_end)
{
    EngineCommand cmd = { .type = CMD_END };

    int ret = mq_send(test_vmu_iec_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    receive_cmd();

    ck_assert_msg(running == 0, "Running flag should be 0 after END command");
}
END_TEST

START_TEST(test_iec_receive_cmd_unknown)
{
    EngineCommand cmd = { .type = CMD_UNKNOWN };

    int ret = mq_send(test_vmu_iec_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    receive_cmd();

    // The function should handle the unknown command without crashing
    ck_assert(true);
}
END_TEST

START_TEST(test_iec_receive_cmd_empty_queue)
{
    EngineCommand dummy;
    while (mq_receive(iec_mq_receive, (char *)&dummy, sizeof(dummy), NULL) != -1) {
        // Keep reading until queue is empty
    }

    receive_cmd();

    // The function should handle empty queue without errors
    ck_assert(true);
}
END_TEST

START_TEST(test_iec_engine_rpm_at_target)
{
    int target_rpm = IEC_IDLE_RPM + (int)(0.5 * (MAX_IEC_RPM - IEC_IDLE_RPM));

    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = target_rpm;
    test_vmu_system_state->temp_iec = 70.0;
    test_vmu_system_state->iec_power_level = 0.5;
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(abs(test_vmu_system_state->rpm_iec - target_rpm) <= 1,
                  "RPM should remain at target when already there");
    ck_assert_msg(test_vmu_system_state->temp_iec > 70.0, "Temperature should increase even at steady RPM");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_idle_rpm_enforcement)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = IEC_IDLE_RPM - 200; // Below idle RPM
    test_vmu_system_state->temp_iec = 40.0;
    test_vmu_system_state->iec_power_level = 0.0;
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_iec == IEC_IDLE_RPM,
                  "RPM should be set to idle when engine is on and RPM below idle");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_temp_already_ambient)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = false;
    test_vmu_system_state->rpm_iec = 0;
    test_vmu_system_state->temp_iec = 25.0; // Ambient temperature
    test_vmu_system_state->iec_power_level = 0.0;
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(fabs(test_vmu_system_state->temp_iec - 25.0) < 0.001,
                  "Temperature should remain at ambient when already there");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_max_rpm_limit)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = MAX_IEC_RPM - 100; // Just below max
    test_vmu_system_state->temp_iec = 90.0;
    test_vmu_system_state->iec_power_level = 1.0; // Max power
    sem_post(test_vmu_sem);

    engine();

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
    running = 1;
    paused = false;

    handle_signal(SIGHUP); // Using SIGHUP as an example unhandled signal

    ck_assert_msg(running == 1, "Running flag should remain unchanged for unrecognized signal");
    ck_assert_msg(paused == false, "Paused flag should remain unchanged for unrecognized signal");
}
END_TEST

START_TEST(test_iec_engine_idle_to_accelerate)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = IEC_IDLE_RPM;
    test_vmu_system_state->temp_iec = 50.0;
    test_vmu_system_state->iec_power_level = 0.5; // Command 50% power
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->iec_on == true, "Engine should remain ON");
    ck_assert_msg(test_vmu_system_state->rpm_iec > IEC_IDLE_RPM, "RPM should increase from idle");
    ck_assert_msg(test_vmu_system_state->temp_iec > 50.0, "Temperature should increase");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_high_to_decelerate)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = MAX_IEC_RPM;
    test_vmu_system_state->temp_iec = 90.0;
    test_vmu_system_state->iec_power_level = 0.1; // Command 10% power
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->iec_on == true, "Engine should remain ON");
    ck_assert_msg(test_vmu_system_state->rpm_iec < MAX_IEC_RPM, "RPM should decrease from max");
    ck_assert_msg(test_vmu_system_state->rpm_iec >= IEC_IDLE_RPM, "RPM should not drop below idle while ON");
    ck_assert_msg(test_vmu_system_state->temp_iec > 90.0, "Temperature might still increase slightly or stabilize");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_stop_cooldown)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = false;
    test_vmu_system_state->rpm_iec = 3000;
    test_vmu_system_state->temp_iec = 85.0; // Above ambient
    test_vmu_system_state->iec_power_level = 0.0;
    sem_post(test_vmu_sem);

    engine();

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
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = 5000;
    test_vmu_system_state->temp_iec = 104.0; // Close to max
    test_vmu_system_state->iec_power_level = 1.0; // Max power
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->temp_iec <= 105.0, "Temperature should be capped at 105.0");
    ck_assert_msg(test_vmu_system_state->temp_iec >= 104.0, "Temperature should increase towards cap");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_temperature_boundary_exact)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = 5000;
    test_vmu_system_state->temp_iec = 105.0; // Exactly at max temperature
    test_vmu_system_state->iec_power_level = 1.0; // Max power
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->temp_iec == 105.0,
                  "Temperature should remain capped exactly at 105.0");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_off_rpm_already_zero)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = false;
    test_vmu_system_state->rpm_iec = 0;
    test_vmu_system_state->temp_iec = 30.0; // Above ambient
    test_vmu_system_state->iec_power_level = 0.0;
    sem_post(test_vmu_sem);

    engine();

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
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = 1000;
    test_vmu_system_state->temp_iec = 50.0;
    test_vmu_system_state->iec_power_level = 0.0; // No power requested
    sem_post(test_vmu_sem);

    engine();

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
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = IEC_IDLE_RPM;
    test_vmu_system_state->temp_iec = 60.0;
    test_vmu_system_state->iec_power_level = 0.0;
    sem_post(test_vmu_sem);

    engine(); // First iteration - engine at idle

    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_power_level = 0.9; // Jump to high power
    sem_post(test_vmu_sem);

    engine(); // Second iteration - engine should start accelerating

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
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = false;
    test_vmu_system_state->rpm_iec = 2000;
    test_vmu_system_state->temp_iec = 70.0;
    test_vmu_system_state->iec_power_level = 0.1; // Power level non-zero but engine off
    sem_post(test_vmu_sem);

    engine();

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

    sem_wait(test_vmu_sem);
    double temp_high_power = test_vmu_system_state->temp_iec;
    double temp_increase_high = temp_high_power - temp_low_power;

    test_vmu_system_state->temp_iec = temp_low_power;
    test_vmu_system_state->iec_power_level = 0.1; // Back to low power
    sem_post(test_vmu_sem);

    engine(); // Another iteration at low power

    sem_wait(test_vmu_sem);
    double temp_low_power_2 = test_vmu_system_state->temp_iec;
    double temp_increase_low = temp_low_power_2 - temp_low_power;

    ck_assert_msg(temp_increase_high > temp_increase_low,
                  "Temperature should increase faster at higher power levels");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_rpm_adjustment_limits)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    int target_rpm = IEC_IDLE_RPM + (int)(0.5 * (MAX_IEC_RPM - IEC_IDLE_RPM));
    test_vmu_system_state->rpm_iec = target_rpm - 10; // Just below target
    test_vmu_system_state->temp_iec = 70.0;
    test_vmu_system_state->iec_power_level = 0.5; // 50% power
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    int rpm_close_below = test_vmu_system_state->rpm_iec;
    ck_assert_msg(rpm_close_below >= target_rpm - 10 && rpm_close_below <= target_rpm,
                  "RPM should adjust smoothly when close to target");

    test_vmu_system_state->rpm_iec = target_rpm + 10; // Just above target
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    int rpm_close_above = test_vmu_system_state->rpm_iec;
    ck_assert_msg(rpm_close_above <= target_rpm + 10 && rpm_close_above >= target_rpm,
                  "RPM should adjust smoothly when close to target (above)");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_iec_engine_ambient_temperature_approaches)
{
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
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = IEC_IDLE_RPM;
    test_vmu_system_state->temp_iec = 60.0;
    test_vmu_system_state->iec_power_level = 0.8; // High power
    sem_post(test_vmu_sem);

    int previous_rpm, current_rpm;
    double previous_temp, current_temp;

    sem_wait(test_vmu_sem);
    previous_rpm = test_vmu_system_state->rpm_iec;
    previous_temp = test_vmu_system_state->temp_iec;
    sem_post(test_vmu_sem);

    for (int i = 0; i < 5; i++) {
        engine();

        sem_wait(test_vmu_sem);
        current_rpm = test_vmu_system_state->rpm_iec;
        current_temp = test_vmu_system_state->temp_iec;

        int rpm_change = current_rpm - previous_rpm;
        double temp_change = current_temp - previous_temp;

        previous_rpm = current_rpm;
        previous_temp = current_temp;
        sem_post(test_vmu_sem);

        ck_assert_msg(rpm_change >= 0, "RPM change should be non-negative during acceleration");
        ck_assert_msg(rpm_change < 1000, "RPM change per iteration should be limited");
        ck_assert_msg(temp_change > 0, "Temperature should increase when engine is running");
        ck_assert_msg(temp_change < 5.0, "Temperature change per iteration should be limited");
    }
}
END_TEST

START_TEST(test_iec_receive_cmd_mq_error_simulation)
{
    // Verify the function handles empty queue gracefully
    EngineCommand dummy;
    while (mq_receive(iec_mq_receive, (char *)&dummy, sizeof(dummy), NULL) != -1) {
        // Keep reading until queue is empty
    }

    receive_cmd();

    ck_assert(true);
}
END_TEST

START_TEST(test_iec_multi_signal_sequence)
{
    running = 1;
    paused = false;

    handle_signal(SIGUSR1);
    ck_assert_msg(paused == true, "Paused flag should be true after SIGUSR1");
    ck_assert_msg(running == 1, "Running flag should still be 1");

    handle_signal(SIGINT);
    ck_assert_msg(paused == true, "Paused flag should remain true after SIGINT");
    ck_assert_msg(running == 0, "Running flag should be 0 after SIGINT");

    handle_signal(SIGUSR1);
    ck_assert_msg(paused == false, "Paused flag should toggle to false");
    ck_assert_msg(running == 0, "Running flag should remain 0");
}
END_TEST

START_TEST(test_iec_temperature_when_just_started)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = true;
    test_vmu_system_state->rpm_iec = IEC_IDLE_RPM;
    test_vmu_system_state->temp_iec = 25.0; // Cold (ambient)
    test_vmu_system_state->iec_power_level = 0.0;
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
    sem_wait(test_vmu_sem);
    test_vmu_system_state->iec_on = false;
    test_vmu_system_state->rpm_iec = 100; // Very low RPM but not zero
    test_vmu_system_state->temp_iec = 40.0;
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_iec < 20,
                 "RPM should continue decreasing when near zero");
    sem_post(test_vmu_sem);
}
END_TEST

// --- Signal handler testing (Direct call approach) ---

START_TEST(test_iec_handle_signal_pause)
{
    paused = false;
    handle_signal(SIGUSR1);
    ck_assert_msg(paused == true, "Paused flag should be true after SIGUSR1 when false");

    handle_signal(SIGUSR1);
    ck_assert_msg(paused == false, "Paused flag should be false after SIGUSR1 when true");
}
END_TEST

START_TEST(test_iec_handle_signal_shutdown)
{
    running = 1;
    handle_signal(SIGINT);
    ck_assert_msg(running == 0, "Running flag should be 0 after SIGINT");

    running = 1; // Reset
    handle_signal(SIGTERM);
    ck_assert_msg(running == 0, "Running flag should be 0 after SIGTERM");
}
END_TEST


// --- Main Test Suite Creation ---

Suite *iec_suite(void) {
    Suite *s;
    TCase *tc_core;
    TCase *tc_commands;
    TCase *tc_engine;
    TCase *tc_signals;
    TCase *tc_advanced;
    TCase *tc_init_comm_fail;

    s = suite_create("IEC Module Tests");

    // Core tests
    tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, iec_setup, iec_teardown);
    tcase_add_test(tc_core, test_iec_init_communication_success);
    suite_add_tcase(s, tc_core);

    // Init communication failure tests
    tc_init_comm_fail = tcase_create("InitCommunicationFail");
    tcase_add_test(tc_init_comm_fail, test_init_communication_shm_fd_fail);
    tcase_add_test(tc_init_comm_fail, test_init_communication_sem_fail);
    tcase_add_test(tc_init_comm_fail, test_init_communication_iec_queue_fail);
    suite_add_tcase(s, tc_init_comm_fail);

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

    // Signal handling tests
    tc_signals = tcase_create("SignalHandlers");
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

    // srunner_set_fork_status(sr, CK_NOFORK);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    // Safeguard cleanup
    cleanup_vmu_resources();

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}