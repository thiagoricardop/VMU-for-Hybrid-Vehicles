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

// Include your project headers (adjust paths as needed)
#include "../../src/ev/ev.h"
#include "../../src/vmu/vmu.h"

// Global variables declared in ev.c that we need to override for testing.
extern SystemState *system_state;
extern sem_t *sem;
extern volatile sig_atomic_t running;
extern mqd_t ev_mq_receive; // Message queue descriptor

// For testing, we simulate the message queue behavior by overriding mq_receive.
// A flag and a fake command will control the behavior.
int fake_mq_receive_enabled = 0;
EngineCommand fake_cmd;

// Override of mq_receive to simulate receiving a message from the queue.
// When fake_mq_receive_enabled is set, this function copies fake_cmd into msg_ptr.
ssize_t mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio) {
    if (fake_mq_receive_enabled) {
        memcpy(msg_ptr, &fake_cmd, sizeof(EngineCommand));
        return sizeof(EngineCommand);
    }
    return -1; // Simulate no message received.
}

/************************************
 * Receive Command Test Cases
 ************************************/

// Dummy shared resource pointers for receive_cmd() tests.
static SystemState *test_system_state = NULL;
static sem_t *test_sem = NULL;

// Setup for receive_cmd() tests.
void receive_cmd_setup(void) {
    test_system_state = malloc(sizeof(SystemState));
    if (!test_system_state) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    test_system_state->ev_on = false;
    test_system_state->rpm_ev = 100;      // Arbitrary initial value.
    test_system_state->temp_ev = 30.0;      // Above ambient.
    test_system_state->transition_factor = 0.0;
    system_state = test_system_state;

    test_sem = malloc(sizeof(sem_t));
    if (sem_init(test_sem, 0, 1) != 0) {
        perror("sem_init");
        exit(EXIT_FAILURE);
    }
    sem = test_sem;

    running = 1;
    fake_mq_receive_enabled = 0;
}

// Teardown for receive_cmd() tests.
void receive_cmd_teardown(void) {
    sem_destroy(test_sem);
    free(test_sem);
    free(test_system_state);
}

// Test: CMD_START sets ev_on to true.
START_TEST(test_receive_cmd_start) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = CMD_START;
    system_state->ev_on = false;
    receive_cmd();
    ck_assert(system_state->ev_on == true);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test: CMD_STOP sets ev_on to false and resets rpm_ev.
START_TEST(test_receive_cmd_stop) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = CMD_STOP;
    system_state->ev_on = true;
    system_state->rpm_ev = 5000;
    receive_cmd();
    ck_assert(system_state->ev_on == false);
    ck_assert_int_eq(system_state->rpm_ev, 0);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test: CMD_SET_POWER does not alter state.
START_TEST(test_receive_cmd_set_power) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = CMD_SET_POWER;
    fake_cmd.power_level = 0.75;
    bool initial_ev_on = system_state->ev_on;
    int initial_rpm = system_state->rpm_ev;
    float initial_temp = system_state->temp_ev;
    receive_cmd();
    ck_assert(system_state->ev_on == initial_ev_on);
    ck_assert_int_eq(system_state->rpm_ev, initial_rpm);
    ck_assert(system_state->temp_ev == initial_temp);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test: CMD_END sets running to 0.
START_TEST(test_receive_cmd_end) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = CMD_END;
    running = 1;
    receive_cmd();
    ck_assert_int_eq(running, 0);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test: Unknown command does not alter state.
START_TEST(test_receive_cmd_default) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = 999; // Undefined command
    bool initial_ev_on = system_state->ev_on;
    int initial_rpm = system_state->rpm_ev;
    float initial_temp = system_state->temp_ev;
    int initial_running = running;
    receive_cmd();
    ck_assert(system_state->ev_on == initial_ev_on);
    ck_assert_int_eq(system_state->rpm_ev, initial_rpm);
    ck_assert(system_state->temp_ev == initial_temp);
    ck_assert_int_eq(running, initial_running);
    fake_mq_receive_enabled = 0;
}
END_TEST

/************************************
 * init_communication() Test Cases
 ************************************/

// Setup for init_communication() tests.
void init_comm_setup(void) {
    int shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open in init_comm_setup");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd, sizeof(SystemState)) == -1) {
        perror("ftruncate in init_comm_setup");
        exit(EXIT_FAILURE);
    }
    close(shm_fd);
    sem_unlink(SEMAPHORE_NAME);
    sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open in init_comm_setup");
        exit(EXIT_FAILURE);
    }
    mq_unlink(EV_COMMAND_QUEUE_NAME);
}

// Teardown for init_communication() tests.
void init_comm_teardown(void) {
    if (system_state != NULL && system_state != MAP_FAILED) {
        munmap(system_state, sizeof(SystemState));
        system_state = NULL;
    }
    sem_close(sem);
    sem_unlink(SEMAPHORE_NAME);
    mq_close(ev_mq_receive);
    mq_unlink(EV_COMMAND_QUEUE_NAME);
    shm_unlink(SHARED_MEM_NAME);
}

// Test: init_communication() initializes shared memory, semaphore, and message queue.
START_TEST(test_init_communication_success) {
    init_communication();
    ck_assert_ptr_nonnull(system_state);
    ck_assert_ptr_ne(system_state, MAP_FAILED);
    ck_assert_ptr_nonnull(sem);
    ck_assert_int_ne(ev_mq_receive, (mqd_t)-1);
}
END_TEST

/************************************
 * Engine Test Cases
 ************************************/

// Setup for engine() tests.
void engine_setup(void) {
    test_system_state = malloc(sizeof(SystemState));
    if (!test_system_state) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    // Set default ambient temperature
    test_system_state->temp_ev = 25.0;
    test_system_state->rpm_ev = 0;
    test_system_state->transition_factor = 0.0;
    // For engine tests, we will modify ev_on and transition_factor as needed.
    system_state = test_system_state;

    test_sem = malloc(sizeof(sem_t));
    if (sem_init(test_sem, 0, 1) != 0) {
        perror("sem_init");
        exit(EXIT_FAILURE);
    }
    sem = test_sem;
}

// Teardown for engine() tests.
void engine_teardown(void) {
    sem_destroy(test_sem);
    free(test_sem);
    free(test_system_state);
}

// Test: When engine is on, rpm and temperature are updated accordingly.
START_TEST(test_engine_on) {
    system_state->ev_on = true;
    system_state->transition_factor = 0.0; // Expect full power: rpm = 8000, temp increases by 0.05
    system_state->temp_ev = 25.0;
    engine();
    ck_assert_int_eq(system_state->rpm_ev, 8000);
    ck_assert_double_eq(system_state->temp_ev, 25.0 + 0.05);
}
END_TEST

// Test: When engine is off and temperature > ambient, rpm is 0 and temperature cools.
START_TEST(test_engine_off_cooling) {
    system_state->ev_on = false;
    system_state->temp_ev = 30.0;
    engine();
    ck_assert_int_eq(system_state->rpm_ev, 0);
    ck_assert_double_eq(system_state->temp_ev, 30.0 - 0.01);
}
END_TEST

// Test: When engine is off and temperature is at ambient, temperature remains unchanged.
START_TEST(test_engine_off_no_cooling) {
    system_state->ev_on = false;
    system_state->temp_ev = 25.0;
    engine();
    ck_assert_int_eq(system_state->rpm_ev, 0);
    ck_assert_double_eq(system_state->temp_ev, 25.0);
}
END_TEST

/************************************
 * Cleanup Test Cases
 ************************************/

// Setup for cleanup() tests.
// This fixture creates real shared memory, semaphore, and a message queue so that cleanup() has valid resources to release.
void cleanup_setup(void) {
    int shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open in cleanup_setup");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd, sizeof(SystemState)) == -1) {
        perror("ftruncate in cleanup_setup");
        exit(EXIT_FAILURE);
    }
    system_state = mmap(NULL, sizeof(SystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (system_state == MAP_FAILED) {
        perror("mmap in cleanup_setup");
        exit(EXIT_FAILURE);
    }
    close(shm_fd);

    sem_unlink(SEMAPHORE_NAME);
    sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open in cleanup_setup");
        exit(EXIT_FAILURE);
    }

    mq_unlink(EV_COMMAND_QUEUE_NAME);
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(EngineCommand);
    attr.mq_curmsgs = 0;
    ev_mq_receive = mq_open(EV_COMMAND_QUEUE_NAME, O_CREAT | O_RDONLY | O_NONBLOCK, 0666, &attr);
    if (ev_mq_receive == (mqd_t)-1) {
        perror("mq_open in cleanup_setup");
        exit(EXIT_FAILURE);
    }
}

// Teardown for cleanup() tests.
void cleanup_teardown(void) {
    shm_unlink(SHARED_MEM_NAME);
    sem_unlink(SEMAPHORE_NAME);
    mq_unlink(EV_COMMAND_QUEUE_NAME);
}

// Test: cleanup() properly releases resources and prints the shutdown message.
START_TEST(test_cleanup) {
    // Redirect stdout to a temporary file to capture the output.
    fflush(stdout);
    int stdout_backup = dup(fileno(stdout));
    FILE *temp_file = tmpfile();
    if (!temp_file) {
        perror("tmpfile");
        exit(EXIT_FAILURE);
    }
    dup2(fileno(temp_file), fileno(stdout));

    // Call cleanup().
    cleanup();

    fflush(stdout);
    // Restore stdout.
    dup2(stdout_backup, fileno(stdout));
    close(stdout_backup);

    // Read the output from the temporary file.
    rewind(temp_file);
    char buffer[256];
    fgets(buffer, sizeof(buffer), temp_file);
    fclose(temp_file);

    ck_assert_msg(strstr(buffer, "[EV] Shut down complete.") != NULL,
                  "Cleanup did not print the expected shutdown message");
}
END_TEST

/************************************
 * Combined Suite: AllTests
 ************************************/
Suite* all_tests_suite(void) {
    Suite *s;
    TCase *tc_receive_cmd, *tc_init_comm, *tc_engine, *tc_cleanup;

    s = suite_create("AllTests");

    /* TCase for receive_cmd() tests */
    tc_receive_cmd = tcase_create("ReceiveCmd");
    tcase_add_checked_fixture(tc_receive_cmd, receive_cmd_setup, receive_cmd_teardown);
    tcase_add_test(tc_receive_cmd, test_receive_cmd_start);
    tcase_add_test(tc_receive_cmd, test_receive_cmd_stop);
    tcase_add_test(tc_receive_cmd, test_receive_cmd_set_power);
    tcase_add_test(tc_receive_cmd, test_receive_cmd_end);
    tcase_add_test(tc_receive_cmd, test_receive_cmd_default);
    suite_add_tcase(s, tc_receive_cmd);

    /* TCase for init_communication() tests */
    tc_init_comm = tcase_create("InitCommunication");
    tcase_add_checked_fixture(tc_init_comm, init_comm_setup, init_comm_teardown);
    tcase_add_test(tc_init_comm, test_init_communication_success);
    suite_add_tcase(s, tc_init_comm);

    /* TCase for engine() tests */
    tc_engine = tcase_create("Engine");
    tcase_add_checked_fixture(tc_engine, engine_setup, engine_teardown);
    tcase_add_test(tc_engine, test_engine_on);
    tcase_add_test(tc_engine, test_engine_off_cooling);
    tcase_add_test(tc_engine, test_engine_off_no_cooling);
    suite_add_tcase(s, tc_engine);

    /* TCase for cleanup() tests */
    tc_cleanup = tcase_create("Cleanup");
    tcase_add_checked_fixture(tc_cleanup, cleanup_setup, cleanup_teardown);
    tcase_add_test(tc_cleanup, test_cleanup);
    suite_add_tcase(s, tc_cleanup);

    return s;
}

/************************************
 * Main function: Run All Tests
 ************************************/
int main(void) {
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = all_tests_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
