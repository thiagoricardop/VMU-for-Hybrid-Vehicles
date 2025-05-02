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
#include "../../src/ev/ev.h"
#include "../../src/vmu/vmu.h" // Assuming vmu.h contains constants like SHM_NAME, SEM_NAME, MQ_NAMES, CMD types, RPM limits, etc.

// --- Declare external globals from ev.c ---
// These are declared in ev.c, we need to access them for testing setup/teardown
extern SystemState *system_state;
extern sem_t *sem;
extern mqd_t ev_mq_receive;
extern volatile sig_atomic_t running;
extern volatile sig_atomic_t paused;

// --- Declare variables for the resources *created by the test setup* (simulating VMU) ---
// These descriptors/pointers are held by the test suite to interact with the resources
// that ev.c will open.
static SystemState *test_vmu_system_state = NULL;
static sem_t *test_vmu_sem = NULL;
static mqd_t test_vmu_ev_mq_send = (mqd_t)-1; // MQ descriptor for sending commands *to* EV

// --- Helper function to simulate VMU's resource creation ---
// This sets up the environment that ev.c's init_communication expects.
// This function is NOT part of the EV code being tested, it's test infrastructure.
void create_vmu_resources_ev() {
    // Shared Memory
    int shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Test Setup EV: Error creating shared memory");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd, sizeof(SystemState)) == -1) {
        perror("Test Setup EV: Error configuring shared memory size");
        close(shm_fd);
        shm_unlink(SHARED_MEM_NAME);
        exit(EXIT_FAILURE);
    }
    test_vmu_system_state = (SystemState *)mmap(NULL, sizeof(SystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (test_vmu_system_state == MAP_FAILED) {
        perror("Test Setup EV: Error mapping shared memory");
        close(shm_fd);
        shm_unlink(SHARED_MEM_NAME);
        exit(EXIT_FAILURE);
    }
    close(shm_fd); // Close fd after mapping

    // Semaphore
    test_vmu_sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1); // Initial value 1
    if (test_vmu_sem == SEM_FAILED) {
        perror("Test Setup EV: Error creating semaphore");
        munmap(test_vmu_system_state, sizeof(SystemState));
        shm_unlink(SHARED_MEM_NAME);
        exit(EXIT_FAILURE);
    }

    // Message Queue (EV command queue - VMU sends to this)
    struct mq_attr mq_attributes;
    mq_attributes.mq_flags = 0;
    mq_attributes.mq_maxmsg = 10;
    mq_attributes.mq_msgsize = sizeof(EngineCommand);
    mq_attributes.mq_curmsgs = 0; // Ignored for open/create

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

    printf("Test Setup EV: VMU resources created.\n");
}

// --- Helper function to simulate VMU's resource cleanup ---
// This unlinks the resources created in the setup.
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


// --- Test Fixture Setup Function ---
// This runs before each test in the TCase.
void ev_setup(void) {
    // 1. Simulate VMU creating resources
    create_vmu_resources_ev();

    // 2. Call the actual ev.c init_communication function
    // Ensure global flags are reset for each test
    running = 1;
    paused = 0;
    init_communication(); // Call the function from ev.c

    // Basic check that init_communication succeeded (optional, setup failing is often enough)
    // ck_assert_ptr_ne(system_state, MAP_FAILED);
    // ck_assert_ptr_ne(sem, SEM_FAILED);
    // ck_assert_int_ne(ev_mq_receive, (mqd_t)-1);

    printf("Test Setup EV: EV init_communication called.\n");
}

// --- Test Fixture Teardown Function ---
// This runs after each test in the TCase.
void ev_teardown(void) {
    // 1. Call the actual ev.c cleanup function
    cleanup(); // Call the cleanup function from ev.c

    // Basic check that cleanup seems to have run (optional)
    // ck_assert_ptr_eq(system_state, MAP_FAILED); // Assuming cleanup sets global to MAP_FAILED/NULL
    // ck_assert_ptr_eq(sem, SEM_FAILED);
    // ck_assert_int_eq(ev_mq_receive, (mqd_t)-1);

    // 2. Clean up the resources created by the test setup (simulating VMU unlink)
    cleanup_vmu_resources_ev();

    printf("Test Teardown EV: EV cleanup and VMU resources cleaned.\n");
}

// --- Individual Test Cases ---

START_TEST(test_ev_init_communication_success)
{
    // This test primarily verifies that the setup function (which calls init_communication)
    // completes without errors and that the global variables are initialized.
    // The assertions in the setup function itself could cover this.
    // You could add more specific checks here if needed, e.g., permissions, flags.
    ck_assert_ptr_ne(system_state, MAP_FAILED);
    ck_assert_ptr_ne(sem, SEM_FAILED);
    ck_assert_int_ne(ev_mq_receive, (mqd_t)-1);
    ck_assert_int_eq(running, 1);
    ck_assert_int_eq(paused, 0);
}
END_TEST

START_TEST(test_ev_receive_cmd_start)
{
    EngineCommand cmd = { .type = CMD_START };

    // Simulate VMU sending the command
    int ret = mq_send(test_vmu_ev_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    // Give a moment for the message to be available, though O_NONBLOCK might not need it
    // usleep(10000); // Short delay if needed, though mq_receive O_NONBLOCK handles EAGAIN

    // Call the function under test
    receive_cmd(); // Call the function from ev.c

    // Verify state change in shared memory
    sem_wait(test_vmu_sem); // Acquire semaphore to read shared state
    ck_assert_msg(test_vmu_system_state->ev_on == true, "EV should be ON after START command");
    // RPM change happens in engine(), not receive_cmd, so don't check RPM here.
    sem_post(test_vmu_sem); // Release semaphore
}
END_TEST

START_TEST(test_ev_receive_cmd_stop)
{
    // First, set the state as if the motor was running
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 3000;
    test_vmu_system_state->temp_ev = 50.0;
    sem_post(test_vmu_sem);

    EngineCommand cmd = { .type = CMD_STOP };

    // Simulate VMU sending the command
    int ret = mq_send(test_vmu_ev_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    // Call the function under test
    receive_cmd(); // Call the function from ev.c

    // Verify state change in shared memory
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->ev_on == false, "EV should be OFF after STOP command");
    // receive_cmd in ev.c explicitly sets rpm_ev to 0 on CMD_STOP
    ck_assert_msg(test_vmu_system_state->rpm_ev == 0, "EV RPM should be 0 after STOP command");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_receive_cmd_set_power)
{
    // According to the code comment, VMU updates ev_power_level *before* sending the message.
    // We simulate this by setting the shared memory state first.

    double test_power_level = 0.6;

    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_power_level = test_power_level; // Simulate VMU writing
    sem_post(test_vmu_sem);

    EngineCommand cmd = { .type = CMD_SET_POWER, .power_level = test_power_level }; // Value in msg is informational for EV

    // Simulate VMU sending the command
    int ret = mq_send(test_vmu_ev_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    // Call the function under test
    receive_cmd(); // Call the function from ev.c

    // Verify receive_cmd doesn't change power_level, it just processes the message.
    // The effect is seen in engine(). We just check that the command was received without error.
    // A successful mq_receive and no fprintf indicates success for this message type.
    // This test primarily confirms receive_cmd *handles* CMD_SET_POWER without crashing/erroring.
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->ev_power_level == test_power_level, "EV power level should remain as set by VMU");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_receive_cmd_end)
{
    EngineCommand cmd = { .type = CMD_END };

    // Simulate VMU sending the command
    int ret = mq_send(test_vmu_ev_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    // Call the function under test
    receive_cmd(); // Call the function from ev.c

    // Verify the running flag is set to 0
    ck_assert_msg(running == 0, "Running flag should be 0 after END command");
}
END_TEST

START_TEST(test_ev_receive_cmd_unknown)
{
    EngineCommand cmd = { .type = CMD_UNKNOWN }; // Assuming CMD_UNKNOWN exists/is defined in vmu.h or similar

    // Simulate VMU sending the unknown command
    int ret = mq_send(test_vmu_ev_mq_send, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_ne(ret, -1);

    // Call the function under test
    // We can't easily capture stderr from a unit test function call
    // but we can verify that global state (like running/paused) isn't
    // unexpectedly changed by an unknown command.
    int initial_running = running;
    int initial_paused = paused;

    receive_cmd(); // Call the function from ev.c

    // Verify that the unknown command didn't change critical state flags
    ck_assert_msg(running == initial_running, "Running flag should not change on unknown command");
    ck_assert_msg(paused == initial_paused, "Paused flag should not change on unknown command");

    // Optional: If you could mock stderr, you would assert that an error message was printed.
}
END_TEST


START_TEST(test_ev_engine_rpm_increase)
{
    // Set initial state: motor on, low RPM, command acceleration
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 1000; // Below target
    test_vmu_system_state->temp_ev = 30.0;
    test_vmu_system_state->ev_power_level = 0.5; // Command 50% power
    sem_post(test_vmu_sem);

    // Get target RPM based on power level for assertion
    int expected_target_rpm = (int)(0.5 * MAX_EV_RPM);
    ck_assert_msg(test_vmu_system_state->rpm_ev < expected_target_rpm, "Initial RPM must be less than target for this test");


    // Call the function under test (simulates one engine tick)
    engine(); // Call the function from ev.c

    // Verify state change
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->ev_on == true, "Motor should remain ON");
    ck_assert_msg(test_vmu_system_state->rpm_ev > 1000, "RPM should increase");
    ck_assert_msg(test_vmu_system_state->rpm_ev <= expected_target_rpm, "RPM should not exceed target in one tick");
    ck_assert_msg(test_vmu_system_state->temp_ev > 30.0, "Temperature should increase");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_rpm_decrease)
{
    // Set initial state: motor on, high RPM, command lower power
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = MAX_EV_RPM; // At max
    test_vmu_system_state->temp_ev = 70.0;
    test_vmu_system_state->ev_power_level = 0.3; // Command 30% power
    sem_post(test_vmu_sem);

    // Get target RPM based on power level for assertion
    int expected_target_rpm = (int)(0.3 * MAX_EV_RPM);
     ck_assert_msg(test_vmu_system_state->rpm_ev > expected_target_rpm, "Initial RPM must be greater than target for this test");

    // Call the function under test
    engine(); // Call the function from ev.c

    // Verify state change
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->ev_on == true, "Motor should remain ON");
    ck_assert_msg(test_vmu_system_state->rpm_ev < MAX_EV_RPM, "RPM should decrease from max");
    ck_assert_msg(test_vmu_system_state->rpm_ev >= expected_target_rpm - (int)(MAX_EV_RPM * POWER_DECREASE_RATE * 0.5), "RPM should decrease towards target");
    ck_assert_msg(test_vmu_system_state->temp_ev > 70.0, "Temperature might still increase slightly depending on rates");
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_temp_cap)
{
    // Set initial state: motor on, high temp, high power
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = true;
    test_vmu_system_state->rpm_ev = 5000;
    test_vmu_system_state->temp_ev = 89.97; // Close to max
    test_vmu_system_state->ev_power_level = 1.0; // Max power
    sem_post(test_vmu_sem);

    // Calculate what the temperature *would* be without the cap
    double temp_before = 89.97;
    double power_level = 1.0;
    double expected_temp_uncapped = temp_before + (power_level * EV_TEMP_INCREASE_RATE);


    // Call the function under test
    engine(); // Call the function from ev.c

    // Verify temperature is capped
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->temp_ev <= 90.0, "Temperature should be capped at 90.0");
    // Check that it increased towards the cap (unless the rate is tiny)
    if (EV_TEMP_INCREASE_RATE > 0) {
         ck_assert_msg(test_vmu_system_state->temp_ev >= temp_before, "Temperature should increase towards cap (if not already capped)");
    }
    sem_post(test_vmu_sem);
}
END_TEST

START_TEST(test_ev_engine_stop_cooldown)
{
    // Set initial state: motor just turned off, high temp/RPM
    sem_wait(test_vmu_sem);
    test_vmu_system_state->ev_on = false; // VMU set this
    test_vmu_system_state->rpm_ev = 3000; // Some non-zero RPM
    test_vmu_system_state->temp_ev = 60.0; // Above ambient
    test_vmu_system_state->ev_power_level = 0.0; // Power commanded to 0
    sem_post(test_vmu_sem);

    // Call the function under test
    engine(); // Call the function from ev.c

    // Verify state change
    sem_wait(test_vmu_sem);
    ck_assert_msg(test_vmu_system_state->ev_on == false, "Motor should remain OFF");
    ck_assert_msg(test_vmu_system_state->rpm_ev < 3000, "RPM should decrease when off");
    ck_assert_msg(test_vmu_system_state->rpm_ev >= 0, "RPM should not be negative");
    ck_assert_msg(test_vmu_system_state->temp_ev < 60.0, "Temperature should decrease when off and above ambient");
    ck_assert_msg(test_vmu_system_state->temp_ev >= 25.0, "Temperature should not drop below ambient");
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


// --- Main Test Suite Creation ---

Suite *ev_suite(void) {
    Suite *s;
    TCase *tc_core; // Basic tests
    TCase *tc_commands; // Message queue command tests
    TCase *tc_engine; // Engine logic tests
    TCase *tc_signals; // Signal handler tests (direct call)

    s = suite_create("EV Module Tests");

    // Core tests (Initialization, cleanup)
    tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, ev_setup, ev_teardown); // Use checked fixture for setup/teardown
    tcase_add_test(tc_core, test_ev_init_communication_success);
    suite_add_tcase(s, tc_core);

    // Command processing tests
    tc_commands = tcase_create("Commands");
    tcase_add_checked_fixture(tc_commands, ev_setup, ev_teardown);
    tcase_add_test(tc_commands, test_ev_receive_cmd_start);
    tcase_add_test(tc_commands, test_ev_receive_cmd_stop);
    tcase_add_test(tc_commands, test_ev_receive_cmd_set_power);
    tcase_add_test(tc_commands, test_ev_receive_cmd_end);
    tcase_add_test(tc_commands, test_ev_receive_cmd_unknown); // Test for unknown command
    suite_add_tcase(s, tc_commands);

    // Engine simulation logic tests
    tc_engine = tcase_create("EngineLogic");
    tcase_add_checked_fixture(tc_engine, ev_setup, ev_teardown);
    tcase_add_test(tc_engine, test_ev_engine_rpm_increase); // Test when rpm_ev < target_rpm
    tcase_add_test(tc_engine, test_ev_engine_rpm_decrease);
    tcase_add_test(tc_engine, test_ev_engine_temp_cap); // Test when new_temp > 90.0
    tcase_add_test(tc_engine, test_ev_engine_stop_cooldown);
    suite_add_tcase(s, tc_engine);

    // Signal handling tests (by direct function call)
    tc_signals = tcase_create("SignalHandlers");
    // No fixture needed as we directly call the handler and check globals
    tcase_add_test(tc_signals, test_ev_handle_signal_pause);
    tcase_add_test(tc_signals, test_ev_handle_signal_shutdown);
    suite_add_tcase(s, tc_signals);

    return s;
}

int main(void) {
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = ev_suite();
    sr = srunner_create(s);

    // Set nofork to debug tests within the same process
    // srunner_set_fork_status(sr, CK_NOFORK);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    // Unlink resources just in case previous runs failed cleanup
    // This is a safeguard, teardown *should* handle this
    cleanup_vmu_resources_ev(); // Call again in main as safeguard

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}