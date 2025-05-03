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
extern EngineCommand cmd;      // Global command structure
extern int shm_fd; //Shared Memory File Descriptor


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
    init_communication_iec(SHARED_MEM_NAME, SEMAPHORE_NAME, EV_COMMAND_QUEUE_NAME);

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
    TCase *tc_init_comm_fail;

    s = suite_create("IEC Module Tests");

    // Core tests (Initialization, cleanup)
    tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, iec_setup, iec_teardown); // Use checked fixture for setup/teardown
    tcase_add_test(tc_core, test_iec_init_communication_success);
    suite_add_tcase(s, tc_core);

    //TCase for init_communication() with fail
    tc_init_comm_fail = tcase_create("InitCommunicationFail");
    tcase_add_test(tc_init_comm_fail, test_init_communication_shm_fd_fail);
    tcase_add_test(tc_init_comm_fail, test_init_communication_sem_fail);
    tcase_add_test(tc_init_comm_fail, test_init_communication_iec_queue_fail);
    suite_add_tcase(s, tc_init_comm_fail);

    // Command processing tests
    tc_commands = tcase_create("Commands");
    tcase_add_checked_fixture(tc_commands, iec_setup, iec_teardown);
    // tcase_add_test(tc_commands, test_iec_receive_cmd_start);
    // tcase_add_test(tc_commands, test_iec_receive_cmd_stop);
    // tcase_add_test(tc_commands, test_iec_receive_cmd_set_power);
    // tcase_add_test(tc_commands, test_iec_receive_cmd_end);
    suite_add_tcase(s, tc_commands);

    // Engine simulation logic tests
    // tc_engine = tcase_create("EngineLogic");
    // tcase_add_checked_fixture(tc_engine, iec_setup, iec_teardown);
    // tcase_add_test(tc_engine, test_iec_engine_idle_to_accelerate);
    // tcase_add_test(tc_engine, test_iec_engine_high_to_decelerate);
    // tcase_add_test(tc_engine, test_iec_engine_stop_cooldown);
    // tcase_add_test(tc_engine, test_iec_engine_temp_cap);
    // suite_add_tcase(s, tc_engine);

    // Signal handling tests (by direct function call)
    tc_signals = tcase_create("SignalHandlers");
    // No fixture needed as we directly call the handler and check globals
    tcase_add_test(tc_signals, test_iec_handle_signal_pause);
    tcase_add_test(tc_signals, test_iec_handle_signal_shutdown);
    suite_add_tcase(s, tc_signals);


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