#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdio.h>
#include "../../src/ev/ev.h"
#include "../../src/vmu/vmu.h"

// Global variables declared in ev.c that we need to override for testing.
extern SystemState *system_state;
extern sem_t *sem;
extern volatile sig_atomic_t running;

// For testing, we will simulate the message queue behavior by overriding mq_receive.
// We define a flag and a fake command that our overridden function will use.
int fake_mq_receive_enabled = 0;
EngineCommand fake_cmd;

// Override of mq_receive to simulate receiving a message from the queue.
// When fake_mq_receive_enabled is set, the function copies fake_cmd into the provided pointer and returns the size.
ssize_t mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio) {
    if (fake_mq_receive_enabled) {
        memcpy(msg_ptr, &fake_cmd, sizeof(EngineCommand));
        return sizeof(EngineCommand);
    }
    return -1; // Simulate no message received.
}

// We create dummy versions of the shared resources for testing.
static SystemState *test_system_state = NULL;
static sem_t *test_sem = NULL;

// Setup function: allocate and initialize test resources.
void setup(void) {
    // Allocate and initialize a dummy SystemState structure.
    test_system_state = malloc(sizeof(SystemState));
    if (!test_system_state) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    test_system_state->ev_on = false;
    test_system_state->rpm_ev = 100;  // Arbitrary initial value.
    test_system_state->temp_ev = 30.0; // Above ambient.
    test_system_state->transition_factor = 0.0;
    system_state = test_system_state;

    // Create and initialize a semaphore.
    test_sem = malloc(sizeof(sem_t));
    if (sem_init(test_sem, 0, 1) != 0) {
        perror("sem_init");
        exit(EXIT_FAILURE);
    }
    sem = test_sem;

    // Set the running flag to 1.
    running = 1;

    // Initially disable fake mq_receive.
    fake_mq_receive_enabled = 0;
}

// Teardown function: free allocated resources.
void teardown(void) {
    sem_destroy(test_sem);
    free(test_sem);
    free(test_system_state);
}

// Test case: verify that CMD_START sets ev_on to true.
START_TEST(test_receive_cmd_start) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = CMD_START;
    // Ensure initial state is off.
    system_state->ev_on = false;

    receive_cmd();

    ck_assert(system_state->ev_on == true);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test case: verify that CMD_STOP sets ev_on to false and resets rpm_ev.
START_TEST(test_receive_cmd_stop) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = CMD_STOP;
    // Set initial state: motor is on and RPM is nonzero.
    system_state->ev_on = true;
    system_state->rpm_ev = 5000;

    receive_cmd();

    ck_assert(system_state->ev_on == false);
    ck_assert_int_eq(system_state->rpm_ev, 0);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test case: verify that CMD_SET_POWER does not change state (only prints output).
START_TEST(test_receive_cmd_set_power) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = CMD_SET_POWER;
    fake_cmd.power_level = 0.75;
    // Record the initial state.
    bool initial_ev_on = system_state->ev_on;
    int initial_rpm = system_state->rpm_ev;
    float initial_temp = system_state->temp_ev;

    receive_cmd();

    // For CMD_SET_POWER, only system("clear") and printing occur,
    // so no changes to the system state are expected.
    ck_assert(system_state->ev_on == initial_ev_on);
    ck_assert_int_eq(system_state->rpm_ev, initial_rpm);
    ck_assert(system_state->temp_ev == initial_temp);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test case: verify that CMD_END sets running to 0.
START_TEST(test_receive_cmd_end) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = CMD_END;
    // Ensure running is initially 1.
    running = 1;

    receive_cmd();

    ck_assert_int_eq(running, 0);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test case: verify that an unknown command does not alter the state.
START_TEST(test_receive_cmd_default) {
    fake_mq_receive_enabled = 1;
    // Use an undefined command type (e.g., 999).
    fake_cmd.type = 999;
    // Record the initial state.
    bool initial_ev_on = system_state->ev_on;
    int initial_rpm = system_state->rpm_ev;
    float initial_temp = system_state->temp_ev;
    int initial_running = running;

    receive_cmd();

    // For an unknown command, no state changes should occur.
    ck_assert(system_state->ev_on == initial_ev_on);
    ck_assert_int_eq(system_state->rpm_ev, initial_rpm);
    ck_assert(system_state->temp_ev == initial_temp);
    ck_assert_int_eq(running, initial_running);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Create the test suite for receive_cmd().
Suite* receive_cmd_suite(void) {
    Suite *s;
    TCase *tc_core;
    
    s = suite_create("ReceiveCmd");
    tc_core = tcase_create("Core");
    
    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_receive_cmd_start);
    tcase_add_test(tc_core, test_receive_cmd_stop);
    tcase_add_test(tc_core, test_receive_cmd_set_power);
    tcase_add_test(tc_core, test_receive_cmd_end);
    tcase_add_test(tc_core, test_receive_cmd_default);
    
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    Suite *s;
    SRunner *sr;
    
    s = receive_cmd_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
