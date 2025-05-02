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

// Include your project headers (adjust paths as needed)
#include "../../src/iec/iec.h"
#include "../../src/vmu/vmu.h"

// Global variables declared in the IEC module that we need to override for testing.
extern SystemState *system_state;
extern sem_t *sem;
extern mqd_t iec_mq_receive;   // Message queue descriptor for IEC commands
extern volatile sig_atomic_t running;
extern EngineCommand cmd;      // Global command structure
extern int shm_fd; //Shared Memory File Descriptor

// ------------------------------------------------------------------------
// Test Overrides
// ------------------------------------------------------------------------

// For testing, we simulate the behavior of mq_receive by overriding it.
// A flag and a fake command will control the behavior.
int fake_mq_receive_enabled = 0;
EngineCommand fake_cmd;

// Override of mq_receive: when fake_mq_receive_enabled is set,
// copy fake_cmd into msg_ptr and return its size.
ssize_t mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio) {
    if (fake_mq_receive_enabled) {
        memcpy(msg_ptr, &fake_cmd, sizeof(EngineCommand));
        return sizeof(EngineCommand);
    }
    return -1; // Simulate no message received.
}

// ------------------------------------------------------------------------
// Fixtures for Testing IEC Module Functions
// ------------------------------------------------------------------------

// Fixture for receive_cmd() tests.
static SystemState *test_system_state = NULL;
static sem_t *test_sem = NULL;

void receive_cmd_setup(void) {
    test_system_state = malloc(sizeof(SystemState));
    if (!test_system_state) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    // Initialize with default values.
    test_system_state->iec_on = false;
    test_system_state->rpm_iec = 0;
    test_system_state->temp_iec = 25.0;           // Ambient temperature
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

void receive_cmd_teardown(void) {
    sem_destroy(test_sem);
    free(test_sem);
    free(test_system_state);
}

// Fixture for init_communication() tests.
void init_comm_setup(void) {
    // Create (or recreate) the shared memory object.
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

    // Unlink any existing semaphore and open a new one.
    sem_unlink(SEMAPHORE_NAME);
    sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open in init_comm_setup");
        exit(EXIT_FAILURE);
    }
    // Unlink the message queue if it exists.
    mq_unlink(IEC_COMMAND_QUEUE_NAME);
}

void init_comm_teardown(void) {
    if (system_state != NULL && system_state != MAP_FAILED) {
        munmap(system_state, sizeof(SystemState));
        system_state = NULL;
    }
    sem_close(sem);
    sem_unlink(SEMAPHORE_NAME);
    mq_close(iec_mq_receive);
    mq_unlink(IEC_COMMAND_QUEUE_NAME);
    shm_unlink(SHARED_MEM_NAME);
}

// Fixture for engine() tests.
void engine_setup(void) {
    test_system_state = malloc(sizeof(SystemState));
    if (!test_system_state) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    // Default initialization for engine tests.
    test_system_state->temp_iec = 25.0;
    test_system_state->rpm_iec = 0;
    test_system_state->transition_factor = 0.0;
    system_state = test_system_state;

    test_sem = malloc(sizeof(sem_t));
    if (sem_init(test_sem, 0, 1) != 0) {
        perror("sem_init");
        exit(EXIT_FAILURE);
    }
    sem = test_sem;
}

void engine_teardown(void) {
    sem_destroy(test_sem);
    free(test_sem);
    free(test_system_state);
}

// Fixture for cleanup() tests.
// This fixture creates real shared memory, semaphore, and message queue for cleanup.
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

    mq_unlink(IEC_COMMAND_QUEUE_NAME);
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(EngineCommand);
    attr.mq_curmsgs = 0;
    iec_mq_receive = mq_open(IEC_COMMAND_QUEUE_NAME, O_CREAT | O_RDONLY | O_NONBLOCK, 0666, &attr);
    if (iec_mq_receive == (mqd_t)-1) {
        perror("mq_open in cleanup_setup");
        exit(EXIT_FAILURE);
    }
}

void cleanup_teardown(void) {
    shm_unlink(SHARED_MEM_NAME);
    sem_unlink(SEMAPHORE_NAME);
    mq_unlink(IEC_COMMAND_QUEUE_NAME);
}

// ------------------------------------------------------------------------
// Test Cases for IEC Module Functions
// ------------------------------------------------------------------------

// Test init_communication(): verifies that shared memory, semaphore, and message queue are set up.
START_TEST(test_init_communication_success) {
    init_communication_iec(SHARED_MEM_NAME, SEMAPHORE_NAME, IEC_COMMAND_QUEUE_NAME);
    ck_assert_ptr_nonnull(system_state);
    ck_assert_ptr_ne(system_state, MAP_FAILED);
    ck_assert_ptr_nonnull(sem);
    ck_assert_int_ne(iec_mq_receive, (mqd_t)-1);
}
END_TEST

START_TEST(test_init_communication_shm_fd_fail) {
    init_communication_iec("fail 1", "fail 2", "fail 3");
    ck_assert_int_eq(shm_fd, -1);
}
END_TEST

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

//test hangle signals 
START_TEST(test_handle_signal_pause){
    handle_signal(SIGUSR1);
    ck_assert_int_eq(get_signal(), 0);
}
END_TEST

START_TEST(test_handle_signal_kill){
    handle_signal(SIGINT);
    ck_assert_int_eq(get_signal(), 1);
    handle_signal(SIGTERM);
    ck_assert_int_eq(get_signal(), 1);
}
END_TEST

// Test receive_cmd() with CMD_START: system_state->iec_on should be set to true.
START_TEST(test_receive_cmd_start) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = CMD_START;
    system_state->iec_on = false;  // Ensure initial state
    receive_cmd();
    ck_assert(system_state->iec_on == true);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test receive_cmd() with CMD_STOP: system_state->iec_on should be false and rpm_iec reset to 0.
START_TEST(test_receive_cmd_stop) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = CMD_STOP;
    system_state->iec_on = true;
    system_state->rpm_iec = 3000;
    receive_cmd();
    ck_assert(system_state->iec_on == false);
    ck_assert_int_eq(system_state->rpm_iec, 0);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test receive_cmd() with CMD_SET_POWER: check that it prints the expected message (or does not change state).
// Here we only verify that no state change occurs.
START_TEST(test_receive_cmd_set_power) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = CMD_SET_POWER;
    fake_cmd.power_level = 0.65;
    bool initial_on = system_state->iec_on;
    int initial_rpm = system_state->rpm_iec;
    float initial_temp = system_state->temp_iec;
    receive_cmd();
    ck_assert(system_state->iec_on == initial_on);
    ck_assert_int_eq(system_state->rpm_iec, initial_rpm);
    ck_assert(system_state->temp_iec == initial_temp);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test receive_cmd() with CMD_END: running should be set to 0.
START_TEST(test_receive_cmd_end) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = CMD_END;
    running = 1;
    receive_cmd();
    ck_assert_int_eq(running, 0);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test receive_cmd() with an unknown command: state remains unchanged.
START_TEST(test_receive_cmd_unknown) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = 999; // Unknown command type.
    bool initial_on = system_state->iec_on;
    int initial_rpm = system_state->rpm_iec;
    float initial_temp = system_state->temp_iec;
    int initial_running = running;
    receive_cmd();
    ck_assert(system_state->iec_on == initial_on);
    ck_assert_int_eq(system_state->rpm_iec, initial_rpm);
    ck_assert(system_state->temp_iec == initial_temp);
    ck_assert_int_eq(running, initial_running);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test engine() when IEC is on: RPM and temperature should be updated.
START_TEST(test_engine_on) {
    system_state->iec_on = true;
    system_state->transition_factor = 0.8; // Example transition factor.
    system_state->temp_iec = 25.0;
    engine();
    ck_assert_int_eq(system_state->rpm_iec, (int)(0.8 * 5000));
    ck_assert_double_eq(system_state->temp_iec, 25.0 + (0.8 * 0.1));
}
END_TEST

// Test engine() when IEC is off and temperature > ambient: engine should cool down.
START_TEST(test_engine_off_cooling) {
    system_state->iec_on = false;
    system_state->temp_iec = 30.0;
    engine();
    ck_assert_int_eq(system_state->rpm_iec, 0);
    ck_assert_double_eq(system_state->temp_iec, 30.0 - 0.02);
}
END_TEST

// Test engine() when IEC is off and temperature equals ambient: temperature remains unchanged.
START_TEST(test_engine_off_no_cooling) {
    system_state->iec_on = false;
    system_state->temp_iec = 25.0;
    engine();
    ck_assert_int_eq(system_state->rpm_iec, 0);
    ck_assert_double_eq(system_state->temp_iec, 25.0);
}
END_TEST

// Test cleanup(): verifies that resources are released and the shutdown message is printed.
START_TEST(test_cleanup) {
    // Redirect stdout to capture cleanup() output.
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

    // Read the captured output.
    rewind(temp_file);
    char buffer[256];
    fgets(buffer, sizeof(buffer), temp_file);
    fclose(temp_file);

    ck_assert_msg(strstr(buffer, "[IEC] Shut down complete.") != NULL,
                  "Cleanup did not print the expected shutdown message");
}
END_TEST

// ------------------------------------------------------------------------
// Combined IEC Test Suite
// ------------------------------------------------------------------------

Suite* iec_tests_suite(void) {
    Suite *s;
    TCase *tc_init_comm, *tc_receive_cmd, *tc_engine, *tc_cleanup, *tc_init_comm_fail;

    s = suite_create("IEC_Module_Tests");

    // TCase for init_communication() tests.
    tc_init_comm = tcase_create("InitCommunication");
    tcase_add_checked_fixture(tc_init_comm, init_comm_setup, init_comm_teardown);
    tcase_add_test(tc_init_comm, test_init_communication_success);    
    tcase_add_test(tc_init_comm, test_handle_signal_kill);
    tcase_add_test(tc_init_comm, test_handle_signal_pause);
    suite_add_tcase(s, tc_init_comm);

    //TCase for init_communication() with fail
    tc_init_comm_fail = tcase_create("InitCommunicationFail");
    tcase_add_test(tc_init_comm_fail, test_init_communication_shm_fd_fail);
    tcase_add_test(tc_init_comm_fail, test_init_communication_sem_fail);
    tcase_add_test(tc_init_comm_fail, test_init_communication_iec_queue_fail);
    suite_add_tcase(s, tc_init_comm_fail);

    // TCase for receive_cmd() tests.
    tc_receive_cmd = tcase_create("ReceiveCmd");
    tcase_add_checked_fixture(tc_receive_cmd, receive_cmd_setup, receive_cmd_teardown);
    tcase_add_test(tc_receive_cmd, test_receive_cmd_start);
    tcase_add_test(tc_receive_cmd, test_receive_cmd_stop);
    tcase_add_test(tc_receive_cmd, test_receive_cmd_set_power);
    tcase_add_test(tc_receive_cmd, test_receive_cmd_end);
    tcase_add_test(tc_receive_cmd, test_receive_cmd_unknown);
    suite_add_tcase(s, tc_receive_cmd);

    // TCase for engine() tests.
    tc_engine = tcase_create("Engine");
    tcase_add_checked_fixture(tc_engine, engine_setup, engine_teardown);
    tcase_add_test(tc_engine, test_engine_on);
    tcase_add_test(tc_engine, test_engine_off_cooling);
    tcase_add_test(tc_engine, test_engine_off_no_cooling);
    suite_add_tcase(s, tc_engine);

    // TCase for cleanup() tests.
    tc_cleanup = tcase_create("Cleanup");
    tcase_add_checked_fixture(tc_cleanup, cleanup_setup, cleanup_teardown);
    tcase_add_test(tc_cleanup, test_cleanup);
    suite_add_tcase(s, tc_cleanup);

    return s;
}

// ------------------------------------------------------------------------
// Main Function: Run IEC Module Tests
// ------------------------------------------------------------------------

int main(void) {
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = iec_tests_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
