#define _GNU_SOURCE

#include <stdarg.h>
#include <dlfcn.h> 
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

// ------------------------------------------------------------------------
// Test Overrides
// ------------------------------------------------------------------------

// For testing, we simulate the behavior of mq_receive by overriding it.
// A flag and a fake command will control the behavior.
int fake_mq_receive_enabled = 0;
EngineCommandIEC fake_cmd;

/* Ponto de falha controlado nos testes */
static int fail_point = 0;

/* Ponteiros para funções originais */
static int (*real_shm_open)(const char *, int, mode_t) = NULL;
static void *(*real_mmap)(void *, size_t, int, int, int, off_t) = NULL;
static sem_t *(*real_sem_open)(const char *, int, mode_t, unsigned int) = NULL;
static mqd_t (*real_mq_open)(const char *, int, mode_t, struct mq_attr *) = NULL;

// Override of mq_receive: when fake_mq_receive_enabled is set,
// copy fake_cmd into msg_ptr and return its size.
ssize_t mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio) {
    if (fake_mq_receive_enabled) {
        memcpy(msg_ptr, &fake_cmd, sizeof(EngineCommandIEC));
        return sizeof(EngineCommandIEC);
    }
    return -1; // Simulate no message received.
}

// ------------------------------------------------------------------------
// Fixtures for Testing IEC Module Functions
// ------------------------------------------------------------------------

// Fixture for receive_cmd() tests.
static SystemState *test_system_state = NULL;
static sem_t *test_sem = NULL;

/* Stubs que simulam falhas sem --wrap */
int shm_open(const char *name, int oflag, mode_t mode) {
    if (fail_point == 1) {
        errno = EACCES;
        return -1;
    }
    if (!real_shm_open) {
        real_shm_open = dlsym(RTLD_NEXT, "shm_open");
    }
    return real_shm_open(name, oflag, mode);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (fail_point == 2) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    if (!real_mmap) {
        real_mmap = dlsym(RTLD_NEXT, "mmap");
    }
    return real_mmap(addr, length, prot, flags, fd, offset);
}

sem_t *sem_open(const char *name, int oflag, ...) {
    if (fail_point == 3) {
        errno = ENOLCK;
        return SEM_FAILED;
    }
    va_list ap;
    mode_t mode;
    unsigned int value;
    va_start(ap, oflag);
    mode = va_arg(ap, mode_t);
    value = va_arg(ap, unsigned int);
    va_end(ap);
    if (!real_sem_open) {
        real_sem_open = dlsym(RTLD_NEXT, "sem_open");
    }
    return real_sem_open(name, oflag, mode, value);
}

mqd_t mq_open(const char *name, int oflag, ...) {
    if (fail_point == 4) {
        errno = EACCES;
        return (mqd_t)-1;
    }
    va_list ap;
    mode_t mode;
    struct mq_attr *attr;
    va_start(ap, oflag);
    mode = va_arg(ap, mode_t);
    attr = va_arg(ap, struct mq_attr *);
    va_end(ap);
    if (!real_mq_open) {
        real_mq_open = dlsym(RTLD_NEXT, "mq_open");
    }
    return real_mq_open(name, oflag, mode, attr);
}

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
    mq_close(iec_mq);
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
    attr.mq_msgsize = sizeof(EngineCommandIEC);
    attr.mq_curmsgs = 0;
    iec_mq = mq_open(IEC_COMMAND_QUEUE_NAME, O_CREAT | O_RDONLY | O_NONBLOCK, 0666, &attr);
    if (iec_mq == (mqd_t)-1) {
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

/* Testes para cada caminho de falha */
START_TEST(test_shm_open_fail) {
    fail_point = 1;
    ck_assert_int_eq(iec_initializer(), EXIT_FAILURE);
}
END_TEST

START_TEST(test_mmap_fail) {
    fail_point = 2;
    ck_assert_int_eq(iec_initializer(), EXIT_FAILURE);
}
END_TEST

START_TEST(test_sem_open_fail) {
    fail_point = 3;
    ck_assert_int_eq(iec_initializer(), EXIT_FAILURE);
}
END_TEST

START_TEST(test_mq_open_fail) {
    fail_point = 4;
    ck_assert_int_eq(iec_initializer(), EXIT_FAILURE);
}
END_TEST

// Test init_communication(): verifies that shared memory, semaphore, and message queue are set up.
START_TEST(test_init_communication_success) {
    iec_initializer() ;
    ck_assert_ptr_nonnull(system_state);
    ck_assert_ptr_ne(system_state, MAP_FAILED);
    ck_assert_ptr_nonnull(sem);
    ck_assert_int_ne(iec_mq, (mqd_t)-1);
}
END_TEST

// Test receive_cmd() with CMD_START: system_state->iec_on should be set to true.
START_TEST(test_receive_cmd_start) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = CMD_START;
    fake_cmd.power_level = 0.205000; // iec is activated when power level is above than 0.0
    iecActive = false;  // Ensure initial state
    cmd = iec_receive (fake_cmd);
    treatValues();
    ck_assert(iecActive == true);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test receive_cmd() with CMD_STOP: system_state->iec_on should be false and rpm_iec reset to 0.
START_TEST(test_receive_cmd_stop) {
    fake_mq_receive_enabled = 1;
    fake_cmd.type = CMD_STOP;
    iecActive = true;
    cmd = iec_receive (fake_cmd);
    treatValues();
    ck_assert(iecActive == false);
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
    fake_cmd = iec_receive (fake_cmd);
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
    cmd = iec_receive (fake_cmd);
    treatValues();
    ck_assert_int_eq(running, 0);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test engine() when IEC is on: RPM and temperature should be updated.
START_TEST(test_engine_on) {
    fake_mq_receive_enabled = 1;
    iecActive = true;
    fake_cmd.type = CMD_START;
    fake_cmd.globalVelocity = 90.0;
    fake_cmd.power_level = 0.50;
    fake_cmd.ev_on = true;
    cmd = iec_receive(fake_cmd);
    treatValues();
    double expected = 2421.907399;
    double tol = 1e-6;  
    ck_assert_double_eq_tol(iecRPM, expected, tol);
    fake_mq_receive_enabled = 0;
}
END_TEST

// Test engine() when IEC is off and temperature > ambient: engine should cool down.
START_TEST(test_engine_off_cooling) {
    system_state->iec_on = false;
    system_state->temp_iec = 30.0;
    treatValues();
    ck_assert_int_eq(system_state->rpm_iec, 0);
    ck_assert_double_eq(system_state->temp_iec, 30.0 - 0.02);
}
END_TEST

// Test engine() when IEC is off and temperature equals ambient: temperature remains unchanged.
START_TEST(test_engine_off_no_cooling) {
    system_state->iec_on = false;
    system_state->temp_iec = 25.0;
    treatValues();
    ck_assert_int_eq(system_state->rpm_iec, 0);
    ck_assert_double_eq(system_state->temp_iec, 25.0);
}
END_TEST

// Test engine() when IEC is off and temperature equals ambient: temperature remains unchanged.
START_TEST(test_engine_gear1) {
    system_state->iec_on = true;
    system_state->temp_iec = 25.0;
    localVelocity = 10.0;
    treatValues();
    ck_assert_int_eq(gear, 1);
}
END_TEST

START_TEST(test_engine_gear2) {
    system_state->iec_on = true;
    system_state->temp_iec = 25.0;
    localVelocity = 20.0;
    calculateValues();
    ck_assert_int_eq(gear, 2);
}
END_TEST

START_TEST(test_engine_gear3) {
    system_state->iec_on = true;
    system_state->temp_iec = 25.0;
    localVelocity = 35.0;
    calculateValues();
    ck_assert_int_eq(gear, 3);
}
END_TEST

START_TEST(test_engine_gear4) {
    system_state->iec_on = true;
    system_state->temp_iec = 25.0;
    localVelocity = 50.0;
    calculateValues();
    ck_assert_int_eq(gear, 4);
}
END_TEST

START_TEST(test_engine_gear5) {
    system_state->iec_on = true;
    system_state->temp_iec = 25.0;
    localVelocity = 80.0;
    calculateValues();
    ck_assert_int_eq(gear, 5);
}
END_TEST

// Test cleanup(): verifies that resources are released and the shutdown message is printed.
START_TEST(test_cleanup) {

    iecCleanUp();

    ck_assert_int_eq(iec_mq, (mqd_t)-1);
    ck_assert_ptr_eq(system_state, NULL);     /* ck_assert_ptr_eq para ponteiros */ 
    ck_assert_ptr_eq(sem, NULL);              /* idem */     
}
END_TEST

START_TEST(test_pause_signal)
{
    paused = 0;
    handle_signal(SIGUSR1);
    ck_assert_int_eq(paused, 1);

    handle_signal(SIGUSR1);
    ck_assert_int_eq(paused, 0);
}
END_TEST

START_TEST(test_shutdown_signal)
{
    running = 1;
    handle_signal(SIGINT);
    ck_assert_int_eq(running, 0);

    running = 1;
    handle_signal(SIGTERM);
    ck_assert_int_eq(running, 0);
}
END_TEST

// ------------------------------------------------------------------------
// Combined IEC Test Suite
// ------------------------------------------------------------------------

Suite* iec_tests_suite(void) {
    Suite *s;
    TCase *tc_init_comm, *tc_receive_cmd, *tc_engine, *tc_cleanup, *tc_error, *tc_signal;

    s = suite_create("IEC_Module_Tests");

    // TCase for init_communication() tests.
    tc_init_comm = tcase_create("InitCommunication");
    tcase_add_checked_fixture(tc_init_comm, init_comm_setup, init_comm_teardown);
    tcase_add_test(tc_init_comm, test_init_communication_success);
    suite_add_tcase(s, tc_init_comm);

    // TCase for receive_cmd() tests.
    tc_receive_cmd = tcase_create("ReceiveCmd");
    tcase_add_checked_fixture(tc_receive_cmd, receive_cmd_setup, receive_cmd_teardown);
    tcase_add_test(tc_receive_cmd, test_receive_cmd_start);
    tcase_add_test(tc_receive_cmd, test_receive_cmd_stop);
    tcase_add_test(tc_receive_cmd, test_receive_cmd_set_power);
    tcase_add_test(tc_receive_cmd, test_receive_cmd_end);
    suite_add_tcase(s, tc_receive_cmd);

    // TCase for engine() tests.
    tc_engine = tcase_create("Engine");
    tcase_add_checked_fixture(tc_engine, engine_setup, engine_teardown);
    tcase_add_test(tc_engine, test_engine_on);
    tcase_add_test(tc_engine, test_engine_off_cooling);
    tcase_add_test(tc_engine, test_engine_off_no_cooling);
    tcase_add_test(tc_engine, test_engine_gear1);
    tcase_add_test(tc_engine, test_engine_gear2);
    tcase_add_test(tc_engine, test_engine_gear3);
    tcase_add_test(tc_engine, test_engine_gear4);
    tcase_add_test(tc_engine, test_engine_gear5);
    suite_add_tcase(s, tc_engine);

    // TCase for cleanup() tests.
    tc_cleanup = tcase_create("Cleanup");
    tcase_add_checked_fixture(tc_cleanup, cleanup_setup, cleanup_teardown);
    tcase_add_test(tc_cleanup, test_cleanup);
    suite_add_tcase(s, tc_cleanup);

    tc_error = tcase_create("initializerError");
    tcase_add_test(tc_error, test_shm_open_fail);
    tcase_add_test(tc_error, test_mmap_fail);
    tcase_add_test(tc_error, test_sem_open_fail);
    tcase_add_test(tc_error, test_mq_open_fail);
    suite_add_tcase(s, tc_error);

    tc_signal = tcase_create("signal");
    tcase_add_test(tc_signal, test_pause_signal);
    tcase_add_test(tc_signal, test_shutdown_signal);
    suite_add_tcase(s, tc_signal);

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
