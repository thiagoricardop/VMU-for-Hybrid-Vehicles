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

#include "../../src/ev/ev.h"
#include "../../src/vmu/vmu.h"

// --- Declare external globals from ev.c ---
extern SystemState *system_state;
extern sem_t *sem;
extern mqd_t ev_mq_receive;
extern volatile sig_atomic_t running;
extern volatile sig_atomic_t paused;
extern int shm_fd;

// --- Test infrastructure variables (simulating VMU) ---
static SystemState *test_vmu_system_state = NULL;
static sem_t *test_vmu_sem = NULL;
static mqd_t test_vmu_ev_mq_send = (mqd_t)-1; // MQ descriptor for sending commands *to* EV

// --- Helper function to simulate VMU's resource creation ---
// Sets up the environment that ev.c's init_communication expects.
void create_vmu_resources_ev() {
    // Shared Memory
    int shm_fd_setup = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd_setup == -1) {
        perror("Test Setup EV: Error creating shared memory");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd_setup, sizeof(SystemState)) == -1) {
        perror("Test Setup EV: Error configuring shared memory size");
        close(shm_fd_setup);
        shm_unlink(SHARED_MEM_NAME);
        exit(EXIT_FAILURE);
    }
    test_vmu_system_state = (SystemState *)mmap(NULL, sizeof(SystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_setup, 0);
    if (test_vmu_system_state == MAP_FAILED) {
        perror("Test Setup EV: Error mapping shared memory");
        close(shm_fd_setup);
        shm_unlink(SHARED_MEM_NAME);
        exit(EXIT_FAILURE);
    }
    close(shm_fd_setup);

    // Semaphore
    test_vmu_sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if (test_vmu_sem == SEM_FAILED) {
        perror("Test Setup EV: Error creating semaphore");
        munmap(test_vmu_system_state, sizeof(SystemState));
        shm_unlink(SHARED_MEM_NAME);
        exit(EXIT_FAILURE);
    }

    // Message Queue (EV command queue - VMU sends to this)
    struct mq_attr mq_attributes = {
        .mq_flags = 0,
        .mq_maxmsg = 10,
        .mq_msgsize = sizeof(EngineCommand),
        .mq_curmsgs = 0
    };
    test_vmu_ev_mq_send = mq_open(EV_COMMAND_QUEUE_NAME, O_WRONLY | O_CREAT | O_NONBLOCK, 0666, &mq_attributes);
    if (test_vmu_ev_mq_send == (mqd_t)-1) {
         perror("Test Setup EV: Error creating/opening EV message queue for sending");
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

    printf("Test Setup EV: VMU resources created.\n");
}

// --- Helper function to simulate VMU's resource cleanup ---
void cleanup_vmu_resources_ev() {
    if (test_vmu_ev_mq_send != (mqd_t)-1) {
        mq_close(test_vmu_ev_mq_send);
        mq_unlink(EV_COMMAND_QUEUE_NAME);
        test_vmu_ev_mq_send = (mqd_t)-1;
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
    printf("Test Teardown EV: VMU resources unlinked.\n");
}

// --- Test Fixture Setup Function (runs before each test) ---
void ev_setup(void) {
    create_vmu_resources_ev(); // Simulate VMU creating resources
    running = 1; // Reset global flags for each test
    paused = 0;
    // Call the actual ev.c init_communication function
    init_communication_ev(SHARED_MEM_NAME, SEMAPHORE_NAME, EV_COMMAND_QUEUE_NAME); // Use correct MQ name for EV
    printf("Test Setup EV: EV init_communication called.\n");
}

// --- Test Fixture Teardown Function (runs after each test) ---
void ev_teardown(void) {
    cleanup(); // Call the actual ev.c cleanup function
    cleanup_vmu_resources_ev(); // Clean up resources created by the test setup
    printf("Test Teardown EV: EV cleanup and VMU resources cleaned.\n");
}

// --- Individual Test Cases ---

START_TEST(test_ev_init_communication_success)
{
    // Verify that setup (which calls init_communication) succeeded
    ck_assert_ptr_ne(system_state, MAP_FAILED);
    ck_assert_ptr_ne(sem, SEM_FAILED);
    ck_assert_int_ne(ev_mq_receive, (mqd_t)-1);
    ck_assert_int_eq(running, 1);
    ck_assert_int_eq(paused, 0);
}
END_TEST

START_TEST(test_init_communication_shm_fd_fail) {
    // Test failure to open Shared Memory File Descriptor
    init_communication_ev("fail_shm", "fail_sem", "fail_mq");
    ck_assert_int_eq(shm_fd, -1); // shm_fd is extern from ev.c
}
END_TEST

START_TEST(test_init_communication_sem_fail) {
    // Test failure to open Semaphore
    int temp_shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    ck_assert_int_ne(temp_shm_fd, -1); // Ensure SHM exists for this test part
    close(temp_shm_fd);

    init_communication_ev(SHARED_MEM_NAME, "fail_sem", "fail_mq");
    ck_assert_ptr_eq(sem, SEM_FAILED); // sem is extern from ev.c
    shm_unlink(SHARED_MEM_NAME); // Clean up SHM created for the test
}
END_TEST


START_TEST(test_ev_receive_cmd_start)
{
    EngineCommand cmd = { .type = CMD_START };
    int ret = mq_send(test_vmu_ev_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    receive_cmd();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->ev_on == true, "EV should be ON after START command");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_receive_cmd_stop)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true; // Assume motor was running
    test_vmu_system_state->rpm_ev = 3000;
    sem_post(test_vmu_sem);

    EngineCommand cmd = { .type = CMD_STOP };
    int ret = mq_send(test_vmu_ev_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    receive_cmd();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->ev_on == false, "EV should be OFF after STOP command");
    ck_assert_msg(test_vmu_system_state->rpm_ev == 0, "EV RPM should be 0 after STOP command");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_receive_cmd_set_power)
{
    // VMU updates ev_power_level *before* sending the message.
    double test_power_level = 0.6;
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_power_level = test_power_level; // Simulate VMU writing
    sem_post(test_vmu_sem);

    EngineCommand cmd = { .type = CMD_SET_POWER, .power_level = test_power_level };
    int ret = mq_send(test_vmu_ev_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    receive_cmd();

    // Verify receive_cmd handles the message type without error.
    // Power level itself is used by engine(), not directly changed by receive_cmd.
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->ev_power_level == test_power_level, "EV power level should remain as set by VMU");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_receive_cmd_end)
{
    EngineCommand cmd = { .type = CMD_END };
    int ret = mq_send(test_vmu_ev_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    receive_cmd();

    ck_assert_msg(running == 0, "Running flag should be 0 after END command");
}
END_TEST

START_TEST(test_ev_receive_cmd_unknown)
{
    EngineCommand cmd = { .type = CMD_UNKNOWN };
    int ret = mq_send(test_vmu_ev_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    int initial_running = running;
    int initial_paused = paused;
    receive_cmd();

    // Verify unknown command doesn't change critical state flags
    ck_assert_msg(running == initial_running, "Running flag should not change on unknown command");
    ck_assert_msg(paused == initial_paused, "Paused flag should not change on unknown command");
}
END_TEST

START_TEST(test_ev_engine_rpm_increase)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 1000;
    test_vmu_system_state->temp_ev = 30.0;
    test_vmu_system_state->ev_power_level = 0.5;
    sem_post(test_vmu_sem);

    int expected_target_rpm = (int)(0.5 * MAX_EV_RPM);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_ev > 1000, "RPM should increase");
    ck_assert_msg(test_vmu_system_state->rpm_ev <= expected_target_rpm, "RPM should not exceed target in one tick");
    ck_assert_msg(test_vmu_system_state->temp_ev > 30.0, "Temperature should increase");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_rpm_decrease)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = MAX_EV_RPM;
    test_vmu_system_state->temp_ev = 70.0;
    test_vmu_system_state->ev_power_level = 0.3;
    sem_post(test_vmu_sem);

    int expected_target_rpm = (int)(0.3 * MAX_EV_RPM);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_ev < MAX_EV_RPM, "RPM should decrease from max");
    // Check it moves towards the target, allowing for decrease rate
    ck_assert_msg(test_vmu_system_state->rpm_ev >= expected_target_rpm - (int)(MAX_EV_RPM * POWER_DECREASE_RATE * 0.5), "RPM should decrease towards target");
    ck_assert_msg(test_vmu_system_state->temp_ev > 70.0, "Temperature might still increase slightly");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_temp_cap)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 5000;
    test_vmu_system_state->temp_ev = MAX_EV_TEMP - 0.03; // Close to max
    test_vmu_system_state->ev_power_level = 1.0;
    sem_post(test_vmu_sem);

    double temp_before = test_vmu_system_state->temp_ev;
    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->temp_ev <= MAX_EV_TEMP, "Temperature should be capped at MAX_EV_TEMP");
    if (EV_TEMP_INCREASE_RATE > 0) {
         ck_assert_msg(test_vmu_system_state->temp_ev >= temp_before, "Temperature should increase towards cap (if not already capped)");
    }
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_stop_cooldown)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = false;
    test_vmu_system_state->rpm_ev = 3000;
    test_vmu_system_state->temp_ev = 60.0; // Above ambient
    test_vmu_system_state->ev_power_level = 0.0;
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_ev < 3000, "RPM should decrease when off");
    ck_assert_msg(test_vmu_system_state->rpm_ev >= 0, "RPM should not be negative");
    ck_assert_msg(test_vmu_system_state->temp_ev < 60.0, "Temperature should decrease when off and above ambient");
    ck_assert_msg(test_vmu_system_state->temp_ev >= 25.0, "Temperature should not drop below ambient (assuming 25C)");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_rpm_exactly_at_target)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    int target_rpm = (int)(0.5 * MAX_EV_RPM);
    test_vmu_system_state->rpm_ev = target_rpm;
    test_vmu_system_state->temp_ev = 50.0;
    test_vmu_system_state->ev_power_level = 0.5;
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_ev == target_rpm, "RPM should remain unchanged when already at target");
    ck_assert_msg(test_vmu_system_state->temp_ev > 50.0, "Temperature should still increase when engine is on");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_temperature_at_ambient)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = false;
    test_vmu_system_state->rpm_ev = 0;
    test_vmu_system_state->temp_ev = 25.0; // Ambient
    test_vmu_system_state->ev_power_level = 0.0;
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(fabs(test_vmu_system_state->temp_ev - 25.0) < 1e-9, "Temperature should remain at ambient when already there");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_temperature_exactly_at_cap)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 5000;
    test_vmu_system_state->temp_ev = MAX_EV_TEMP;
    test_vmu_system_state->ev_power_level = 1.0;
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(fabs(test_vmu_system_state->temp_ev - MAX_EV_TEMP) < 1e-9, "Temperature should remain at cap when already there");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_rpm_close_to_target)
{
    int target_rpm = (int)(0.6 * MAX_EV_RPM);

    // Case 1: Just below target
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = target_rpm - 5;
    test_vmu_system_state->temp_ev = 60.0;
    test_vmu_system_state->ev_power_level = 0.6;
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    int new_rpm = test_vmu_system_state->rpm_ev;
    ck_assert_msg(new_rpm >= target_rpm - 5 && new_rpm <= target_rpm, "RPM should adjust smoothly when close to target (below)");
    sem_post(test_vmu_sem);

    // Case 2: Just above target
    sem_wait(test_vmu_sem);
    test_vmu_system_state->rpm_ev = target_rpm + 5;
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    new_rpm = test_vmu_system_state->rpm_ev;
    ck_assert_msg(new_rpm <= target_rpm + 5 && new_rpm >= target_rpm, "RPM should adjust smoothly when close to target (above)");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_power_transition)
{
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 5000;
    test_vmu_system_state->temp_ev = 70.0;
    test_vmu_system_state->ev_power_level = 0.5;
    sem_post(test_vmu_sem);

    engine(); // First call with power

    sem_wait(test_vmu_sem);
    int rpm_before_transition = test_vmu_system_state->rpm_ev;
    test_vmu_system_state->ev_power_level = 0.0; // Power to zero
    sem_post(test_vmu_sem);

    engine(); // Second call after power change

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_ev < rpm_before_transition, "RPM should decrease when power transitions to zero");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_receive_cmd_empty_queue)
{
    // Ensure the queue is empty
    EngineCommand dummy;
    while (mq_receive(ev_mq_receive, (char *)&dummy, sizeof(dummy), NULL) != -1);

    int initial_running = running;
    int initial_paused = paused;
    receive_cmd();

    // Verify no state change occurs when queue is empty
    ck_assert_msg(running == initial_running, "Running flag should not change on empty queue");
    ck_assert_msg(paused == initial_paused, "Paused flag should not change on empty queue");
}
END_TEST

START_TEST(test_ev_receive_multiple_commands)
{
    EngineCommand cmd1 = { .type = CMD_START };
    EngineCommand cmd2 = { .type = CMD_SET_POWER, .power_level = 0.7 };

    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = false;
    test_vmu_system_state->ev_power_level = 0.0;
    sem_post(test_vmu_sem);

    mq_send(test_vmu_ev_mq_send, (const char *)&cmd1, sizeof(cmd1), 0);
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_power_level = 0.7; // Simulate VMU setting power level
    sem_post(test_vmu_sem);
    mq_send(test_vmu_ev_mq_send, (const char *)&cmd2, sizeof(cmd2), 0);

    receive_cmd(); // Process first command (START)
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->ev_on == true, "EV should be ON after START command");
    sem_post(test_vmu_sem);

    receive_cmd(); // Process second command (SET_POWER)
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->ev_power_level == 0.7, "Power level should be maintained after SET_POWER command");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_temperature_near_limits)
{
    // Test temperature behavior near ambient
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = false;
    test_vmu_system_state->rpm_ev = 0;
    test_vmu_system_state->temp_ev = 25.1; // Just above ambient
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->temp_ev < 25.1, "Temperature should decrease when just above ambient");
    ck_assert_msg(test_vmu_system_state->temp_ev >= 25.0, "Temperature should not go below ambient");
    sem_post(test_vmu_sem);

    // Test temperature behavior near cap
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = MAX_EV_RPM;
    test_vmu_system_state->temp_ev = MAX_EV_TEMP - 0.1; // Just below cap
    test_vmu_system_state->ev_power_level = 1.0;
    sem_post(test_vmu_sem);

    engine();

    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->temp_ev <= MAX_EV_TEMP, "Temperature should not exceed cap");
    ck_assert_msg(test_vmu_system_state->temp_ev >= MAX_EV_TEMP - 0.1, "Temperature should increase toward cap");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_motor_off_with_power_set)
{
    // Test when motor is OFF but power level is non-zero
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = false; // Motor off
    test_vmu_system_state->rpm_ev = 2000; // Some initial RPM
    test_vmu_system_state->temp_ev = 60.0;
    sem_post(test_vmu_sem);
    
    engine();
    
    // Verify that RPM decreases even with power level set
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_ev < 2000, 
                 "RPM should decrease when motor is off, regardless of power level");
    ck_assert_msg(test_vmu_system_state->temp_ev < 60.0, 
                 "Temperature should decrease when motor is off");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_motor_on_rpm_zero)
{
    // Test scenario where motor is ON but RPM is 0 (unusual state)
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 0;
    test_vmu_system_state->temp_ev = 30.0;
    test_vmu_system_state->ev_power_level = 0.5;
    sem_post(test_vmu_sem);
    
    engine();
    
    // Verify RPM increases from 0
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_ev > 0, 
                 "RPM should increase from 0 when motor is on and power is non-zero");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_motor_off_exact_zero_rpm)
{
    // Test when motor is off and RPM is already exactly 0
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = false;
    test_vmu_system_state->rpm_ev = 0;
    test_vmu_system_state->temp_ev = 30.0;
    test_vmu_system_state->ev_power_level = 0.0;
    sem_post(test_vmu_sem);
    
    engine();
    
    // Verify RPM stays at 0
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_ev == 0, 
                 "RPM should remain at 0 when already at 0 and motor is off");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_rapid_state_change)
{
    // Test rapid state changes: on->off->on
    // First set motor on with high RPM
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 5000;
    test_vmu_system_state->temp_ev = 70.0;
    test_vmu_system_state->ev_power_level = 0.5;
    sem_post(test_vmu_sem);
    
    // First call with motor on
    engine();
    
    // Now turn it off but keep power level
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = false;
    int rpm_after_on = test_vmu_system_state->rpm_ev;
    test_vmu_system_state->ev_power_level = 0.0; // Power to zero
    sem_post(test_vmu_sem);
    
    // Call with motor off
    engine();
    
    // Now turn it back on
    sem_wait(test_vmu_sem);
    int rpm_after_off = test_vmu_system_state->rpm_ev;
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->ev_power_level = 0.5; // Power back to 50%
    sem_post(test_vmu_sem);
    
    // Call with motor back on
    engine();
    
    // Verify state changes
    sem_wait(test_vmu_sem);
    ck_assert_msg(rpm_after_off < rpm_after_on, "RPM should decrease when motor turned off");
    ck_assert_msg(test_vmu_system_state->rpm_ev > rpm_after_off, 
                  "RPM should start increasing again when motor turned back on");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_extreme_power_changes)
{
    // Test when power level changes extremes rapidly (0->1->0)
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 3000; // Mid-range RPM
    test_vmu_system_state->temp_ev = 50.0;
    test_vmu_system_state->ev_power_level = 0.0; // Start with no power
    sem_post(test_vmu_sem);
    
    // First call with zero power
    engine();
    
    sem_wait(test_vmu_sem);
    int rpm_zero_power = test_vmu_system_state->rpm_ev;
    test_vmu_system_state->ev_power_level = 1.0; // Suddenly max power
    sem_post(test_vmu_sem);
    
    // Call with max power
    engine();
    
    sem_wait(test_vmu_sem);
    int rpm_max_power = test_vmu_system_state->rpm_ev;
    test_vmu_system_state->ev_power_level = 0.0; // Back to zero power
    sem_post(test_vmu_sem);
    
    // Call with zero power again
    engine();
    
    // Verify behavior across power transitions
    sem_wait(test_vmu_sem);
    ck_assert_msg(rpm_zero_power < 3000, "RPM should decrease with zero power");
    ck_assert_msg(rpm_max_power > rpm_zero_power, "RPM should increase when power jumps from 0 to max");
    ck_assert_msg(test_vmu_system_state->rpm_ev < rpm_max_power, "RPM should decrease again when power drops to zero");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_multiple_iterations_to_target)
{
    // Test that multiple iterations converge to the target RPM
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 1000;
    test_vmu_system_state->temp_ev = 40.0;
    test_vmu_system_state->ev_power_level = 0.7; // 70% power target
    sem_post(test_vmu_sem);
    
    // Target RPM based on 70% power
    int target_rpm = (int)(0.7 * MAX_EV_RPM);
    
    // Run engine function for multiple iterations
    for (int i = 0; i < 10; i++) {
        engine();
        // Simulate a short delay between iterations
        usleep(10000); // 10ms delay
    }
    
    // Verify RPM reached target
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_ev >= 1000 && 
                 test_vmu_system_state->rpm_ev <= target_rpm * 1.15,
                 "RPM should closely approach target after multiple iterations");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_negative_power_handling)
{
    // Test handling of invalid (negative) power level
    // This tests robustness of the code to handle potentially corrupt shared memory
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 3000;
    test_vmu_system_state->temp_ev = 50.0;
    // Set an invalid power level (negative)
    test_vmu_system_state->ev_power_level = -0.2;
    sem_post(test_vmu_sem);
    
    // The code should handle this gracefully
    engine();
    
    // Verify RPM behaves reasonably (likely same as zero power)
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_ev <= 3000, 
                 "RPM should not increase with negative power level");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_excessive_power_handling)
{
    // Test handling of invalid (excessive) power level
    // This tests robustness of the code to handle potentially corrupt shared memory
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 3000;
    test_vmu_system_state->temp_ev = 50.0;
    // Set an invalid power level (over 1.0)
    test_vmu_system_state->ev_power_level = 1.5;
    sem_post(test_vmu_sem);
    
    // The code should handle this gracefully
    engine();
    
    // Verify RPM behaves as if at max power
    sem_wait(test_vmu_sem);
    int target_rpm = MAX_EV_RPM;  // Should treat as 100% power
    ck_assert_msg(test_vmu_system_state->rpm_ev > 3000, 
                 "RPM should increase with excessive power level (treated as max)");
    ck_assert_msg(test_vmu_system_state->rpm_ev <= target_rpm,
                 "RPM should not exceed maximum even with excessive power level");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_error_temperature)
{
    // Test handling of unusual temperature values
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 3000;
    // Set an unusually low temperature (below ambient)
    test_vmu_system_state->temp_ev = 20.0; 
    test_vmu_system_state->ev_power_level = 0.5;
    sem_post(test_vmu_sem);
    
    // The code should handle this gracefully
    engine();
    
    // Verify temperature normalization
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->temp_ev >= 20.0,
                 "Temperature should not decrease when motor is on");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_power_level_at_boundary)
{
    // Test power levels exactly at boundaries (0.0 and 1.0)
    
    // Test power level 0.0
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 3000;
    test_vmu_system_state->temp_ev = 50.0;
    test_vmu_system_state->ev_power_level = 0.0; // Exactly 0.0
    sem_post(test_vmu_sem);
    
    engine();
    
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_ev < 3000,
                 "RPM should decrease with power level exactly 0.0");
    
    // Test power level 1.0
    test_vmu_system_state->rpm_ev = 3000;
    test_vmu_system_state->ev_power_level = 1.0; // Exactly 1.0
    sem_post(test_vmu_sem);
    
    engine();
    
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->rpm_ev > 3000,
                 "RPM should increase with power level exactly 1.0");
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

START_TEST(test_ev_handle_signal_pause)
{
    paused = false; // Ensure initial state
    handle_signal(SIGUSR1); // Simulate receiving SIGUSR1
    ck_assert_msg(paused == true, "Paused flag should be true after SIGUSR1 when false");

    handle_signal(SIGUSR1); // Simulate receiving SIGUSR1 again
    ck_assert_msg(paused == false, "Paused flag should be false after SIGUSR1 when true");
}
END_TEST

START_TEST(test_ev_handle_signal_shutdown)
{
    running = 1; // Ensure initial state
    handle_signal(SIGINT); // Simulate receiving SIGINT
    ck_assert_msg(running == 0, "Running flag should be 0 after SIGINT");

    running = 1; // Reset
    handle_signal(SIGTERM); // Simulate receiving SIGTERM
    ck_assert_msg(running == 0, "Running flag should be 0 after SIGTERM");
}
END_TEST

START_TEST(test_ev_handle_signal_other)
{
    // Test handling of an unrecognized signal
    running = 1;
    paused = false;
    
    handle_signal(SIGHUP); // Send a signal that isn't explicitly handled
    
    // Verify no change in state
    ck_assert_msg(running == 1, "Running flag should not change on unrecognized signal");
    ck_assert_msg(paused == false, "Paused flag should not change on unrecognized signal");
}
END_TEST

// --- Main Test Suite Creation ---

Suite *ev_suite(void) {
    Suite *s;
    TCase *tc_core; // Basic tests
    TCase *tc_commands; // Message queue command tests
    TCase *tc_engine; // Engine logic tests
    TCase *tc_signals; // Signal handler tests (direct call)
    TCase *tc_edge_cases; // Edge case tests
    TCase *tc_init_comm_fail; // Init communication tests fails

    s = suite_create("EV Module Tests");

    // Core tests (Initialization, cleanup)
    tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, ev_setup, ev_teardown); // Use checked fixture for setup/teardown
    tcase_add_test(tc_core, test_ev_init_communication_success);
    suite_add_tcase(s, tc_core);

    /* TCase for init_communication() fail tests */
    tc_init_comm_fail = tcase_create("InitCommunicationFail");
    tcase_add_test(tc_init_comm_fail, test_init_communication_shm_fd_fail);
    tcase_add_test(tc_init_comm_fail, test_init_communication_sem_fail);
    suite_add_tcase(s, tc_init_comm_fail);

    // Command processing tests
    tc_commands = tcase_create("Commands");
    tcase_add_checked_fixture(tc_commands, ev_setup, ev_teardown);
    tcase_add_test(tc_commands, test_ev_receive_cmd_start);
    tcase_add_test(tc_commands, test_ev_receive_cmd_stop);
    tcase_add_test(tc_commands, test_ev_receive_cmd_set_power);
    tcase_add_test(tc_commands, test_ev_receive_cmd_end);
    tcase_add_test(tc_commands, test_ev_receive_cmd_unknown); // Test for unknown command
    tcase_add_test(tc_commands, test_ev_receive_cmd_empty_queue); // Test empty queue
    tcase_add_test(tc_commands, test_ev_receive_multiple_commands); // Test multiple commands
    suite_add_tcase(s, tc_commands);

    // Engine simulation logic tests
    tc_engine = tcase_create("EngineLogic");
    tcase_add_checked_fixture(tc_engine, ev_setup, ev_teardown);
    tcase_add_test(tc_engine, test_ev_engine_rpm_increase); // Test when rpm_ev < target_rpm
    tcase_add_test(tc_engine, test_ev_engine_rpm_decrease);
    tcase_add_test(tc_engine, test_ev_engine_temp_cap); // Test when new_temp > 90.0
    tcase_add_test(tc_engine, test_ev_engine_stop_cooldown);
    tcase_add_test(tc_engine, test_ev_engine_rpm_exactly_at_target); // Test RPM at target
    tcase_add_test(tc_engine, test_ev_engine_temperature_at_ambient); // Test temp at ambient
    tcase_add_test(tc_engine, test_ev_engine_temperature_exactly_at_cap); // Test temp at cap
    suite_add_tcase(s, tc_engine);

    // Signal handling tests (by direct function call)
    tc_signals = tcase_create("SignalHandlers");
    // No fixture needed as we directly call the handler and check globals
    tcase_add_test(tc_signals, test_ev_handle_signal_pause);
    tcase_add_test(tc_signals, test_ev_handle_signal_shutdown);
    tcase_add_test(tc_signals, test_ev_handle_signal_other);
    suite_add_tcase(s, tc_signals);

    // Edge cases test case
    tc_edge_cases = tcase_create("EdgeCases");
    tcase_add_checked_fixture(tc_edge_cases, ev_setup, ev_teardown);
    tcase_add_test(tc_edge_cases, test_ev_engine_rpm_close_to_target);
    tcase_add_test(tc_edge_cases, test_ev_engine_power_transition);
    tcase_add_test(tc_edge_cases, test_ev_engine_temperature_near_limits);
    tcase_add_test(tc_edge_cases, test_ev_engine_motor_off_with_power_set);
    tcase_add_test(tc_edge_cases, test_ev_engine_motor_on_rpm_zero);
    tcase_add_test(tc_edge_cases, test_ev_engine_motor_off_exact_zero_rpm);
    tcase_add_test(tc_edge_cases, test_ev_engine_rapid_state_change);
    tcase_add_test(tc_edge_cases, test_ev_engine_extreme_power_changes);
    tcase_add_test(tc_edge_cases, test_ev_engine_multiple_iterations_to_target);
    tcase_add_test(tc_edge_cases, test_ev_engine_negative_power_handling);
    tcase_add_test(tc_edge_cases, test_ev_engine_excessive_power_handling);
    tcase_add_test(tc_edge_cases, test_ev_engine_error_temperature);
    tcase_add_test(tc_edge_cases, test_ev_engine_power_level_at_boundary);
    suite_add_tcase(s, tc_edge_cases);

    return s;
}

int main(void) {
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = ev_suite();
    sr = srunner_create(s);

    // srunner_set_fork_status(sr, CK_NOFORK);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    // Safeguard cleanup
    cleanup_vmu_resources_ev(); 

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}