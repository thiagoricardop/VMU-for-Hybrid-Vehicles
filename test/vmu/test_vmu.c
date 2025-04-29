#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include "../../src/vmu/vmu.h"

// Mocking shared memory and semaphore functions
SystemState *mock_system_state;
sem_t *mock_sem;

// Mock semaphore functions
void mock_sem_wait(sem_t *sem) {
    // Simulate semaphore wait
}

void mock_sem_post(sem_t *sem) {
    // Simulate semaphore post
}

// Setup function to initialize mock objects before each test
void setup(void) {
    if (!mock_system_state) {
        mock_system_state = calloc(1, sizeof(SystemState));
        if (!mock_system_state) {
            perror("Failed to allocate mock_system_state");
            exit(EXIT_FAILURE);
        }
    }
    if (!mock_sem) {
        mock_sem = malloc(sizeof(sem_t));
        if (!mock_sem) {
            perror("Failed to allocate mock_sem");
            exit(EXIT_FAILURE);
        }
    }

    system_state = mock_system_state;
    sem = mock_sem;

    init_system_state(system_state);
}

// Teardown function to free mock objects after each test
void teardown(void) {
    if (mock_system_state) {
        free(mock_system_state);
        mock_system_state = NULL;
        system_state = NULL;
    }
    if (mock_sem) {
        free(mock_sem);
        mock_sem = NULL;
        sem = NULL;
    }
}

// Test case for init_system_state
START_TEST(test_init_system_state)
{
    SystemState state;
    init_system_state(&state);
    ck_assert_int_eq(state.accelerator, false);
    ck_assert_int_eq(state.brake, false);
    ck_assert_double_eq_tol(state.speed, MIN_SPEED, 1e-9);
    ck_assert_int_eq(state.rpm_ev, 0);
    ck_assert_int_eq(state.rpm_iec, 0);
    ck_assert_int_eq(state.ev_on, false);
    ck_assert_int_eq(state.iec_on, false);
    ck_assert_double_eq_tol(state.temp_ev, 25.0, 1e-9);
    ck_assert_double_eq_tol(state.temp_iec, 25.0, 1e-9);
    ck_assert_double_eq_tol(state.battery, MAX_BATTERY, 1e-9);
    ck_assert_double_eq_tol(state.fuel, MAX_FUEL, 1e-9);
    ck_assert_int_eq(state.power_mode, 5); // Check for initial parked mode
    ck_assert_double_eq_tol(state.transition_factor, 0.0, 1e-9);
}
END_TEST

//test hangle signals 
START_TEST(test_handle_signal_pause){
    ck_assert(handle_signal(SIGUSR1) == 0);
}
END_TEST

START_TEST(test_handle_signal_kill){
    ck_assert(handle_signal(SIGINT) == 1);
    ck_assert(handle_signal(SIGTERM) == 1);
}
END_TEST

// Test case for set_acceleration
START_TEST(test_set_acceleration)
{
    set_acceleration(true);
    ck_assert_int_eq(mock_system_state->accelerator, true);
    ck_assert_int_eq(mock_system_state->brake, false);
    set_acceleration(false);
    ck_assert_int_eq(mock_system_state->accelerator, false);
}
END_TEST

// Test case for set_braking
START_TEST(test_set_braking)
{
    set_braking(true);
    ck_assert_int_eq(mock_system_state->brake, true);
    ck_assert_int_eq(mock_system_state->accelerator, false);
    set_braking(false);
    ck_assert_int_eq(mock_system_state->brake, false);
}
END_TEST

// Test case for calculate_speed (accelerating)
START_TEST(test_calculate_speed_accelerating)
{
    mock_system_state->accelerator = true;
    mock_system_state->rpm_ev = 1000;
    mock_system_state->rpm_iec = 500;
    mock_system_state->transition_factor = 0.5;
    mock_system_state->speed = 0.0;
    calculate_speed(mock_system_state);
    ck_assert_double_gt(mock_system_state->speed, 0.0);
    ck_assert_double_le(mock_system_state->speed, MAX_SPEED);
}
END_TEST

// Test case for calculate_speed (coasting)
START_TEST(test_calculate_speed_coasting)
{
    mock_system_state->accelerator = false;
    mock_system_state->brake = false;
    mock_system_state->speed = 50.0;
    calculate_speed(mock_system_state);
    ck_assert_double_lt(mock_system_state->speed, 50.0);
    ck_assert_double_ge(mock_system_state->speed, MIN_SPEED);
}
END_TEST

// Test case for calculate_speed (braking)
START_TEST(test_calculate_speed_braking)
{
    mock_system_state->accelerator = false;
    mock_system_state->brake = true;
    mock_system_state->speed = 50.0;
    calculate_speed(mock_system_state);
    ck_assert_double_lt(mock_system_state->speed, 50.0);
    ck_assert_double_ge(mock_system_state->speed, MIN_SPEED);
}
END_TEST

// Test case for vmu_control_engines (below transition zone, accelerating, sufficient battery)
START_TEST(test_vmu_control_engines_below_transition_accelerating_battery)
{
    mock_system_state->speed = TRANSITION_SPEED_THRESHOLD - (TRANSITION_ZONE_WIDTH / 2.0) - 1;
    mock_system_state->battery = 50.0;
    mock_system_state->fuel = 50.0;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    ck_assert_double_eq_tol(mock_system_state->transition_factor, 0.0, 1e-9);
    ck_assert_int_eq(mock_system_state->ev_on, true);
    ck_assert_int_eq(mock_system_state->iec_on, false);
    ck_assert_int_eq(mock_system_state->power_mode, 0); // Electric Only
}
END_TEST

// Test case for vmu_control_engines (above transition zone, accelerating, sufficient fuel)
START_TEST(test_vmu_control_engines_above_transition_accelerating_fuel)
{
    mock_system_state->speed = TRANSITION_SPEED_THRESHOLD + (TRANSITION_ZONE_WIDTH / 2.0) + 1;
    mock_system_state->battery = 50.0;
    mock_system_state->fuel = 50.0;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    ck_assert_double_eq_tol(mock_system_state->transition_factor, 1.0, 1e-9);
    ck_assert_int_eq(mock_system_state->ev_on, false);
    ck_assert_int_eq(mock_system_state->iec_on, true);
    ck_assert_int_eq(mock_system_state->power_mode, 2); // Combustion Only
}
END_TEST

// Test case for vmu_control_engines (in transition zone, accelerating, sufficient battery and fuel)
START_TEST(test_vmu_control_engines_in_transition_accelerating_both)
{
    mock_system_state->speed = TRANSITION_SPEED_THRESHOLD;
    mock_system_state->battery = 50.0;
    mock_system_state->fuel = 50.0;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    ck_assert_double_ge(mock_system_state->transition_factor, 0.0);
    ck_assert_double_le(mock_system_state->transition_factor, 1.0);
    ck_assert_int_eq(mock_system_state->ev_on, true);
    ck_assert_int_eq(mock_system_state->iec_on, true);
    ck_assert_int_eq(mock_system_state->power_mode, 1); // Hybrid
}
END_TEST

// Test case for vmu_control_engines (emergency: low battery, below threshold, sufficient fuel)
START_TEST(test_vmu_control_engines_emergency_low_battery)
{
    mock_system_state->speed = TRANSITION_SPEED_THRESHOLD - 1;
    mock_system_state->battery = 5.0;
    mock_system_state->fuel = 50.0;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    ck_assert_double_eq_tol(mock_system_state->transition_factor, 1.0, 1e-9);
    ck_assert_int_eq(mock_system_state->ev_on, false);
    ck_assert_int_eq(mock_system_state->iec_on, true);
    ck_assert_int_eq(mock_system_state->power_mode, 2); // Combustion Only
}
END_TEST

// Test case for vmu_control_engines (emergency: low fuel, above threshold, sufficient battery)
START_TEST(test_vmu_control_engines_emergency_low_fuel)
{
    mock_system_state->speed = TRANSITION_SPEED_THRESHOLD + 1;
    mock_system_state->battery = 50.0;
    mock_system_state->fuel = 3.0;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    ck_assert_double_eq_tol(mock_system_state->transition_factor, 0.0, 1e-9);
    ck_assert_int_eq(mock_system_state->ev_on, true);
    ck_assert_int_eq(mock_system_state->iec_on, false);
    ck_assert_int_eq(mock_system_state->power_mode, 0); // Electric Only
}
END_TEST

// Test case for vmu_control_engines (both battery and fuel low)
START_TEST(test_vmu_control_engines_both_low)
{
    mock_system_state->speed = 30.0;
    mock_system_state->battery = 5.0;
    mock_system_state->fuel = 3.0;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    ck_assert_double_eq_tol(mock_system_state->transition_factor, 0.0, 1e-9);
    ck_assert_int_eq(mock_system_state->ev_on, false);
    ck_assert_int_eq(mock_system_state->iec_on, false);
    ck_assert_int_eq(mock_system_state->power_mode, 4); // Parked
}
END_TEST

// Test case for vmu_control_engines (not accelerating)
START_TEST(test_vmu_control_engines_not_accelerating)
{
    mock_system_state->accelerator = false;
    vmu_control_engines();
    ck_assert_int_eq(mock_system_state->ev_on, false);
    ck_assert_int_eq(mock_system_state->iec_on, false);
    ck_assert_int_eq(mock_system_state->power_mode, 4); // Parked
}
END_TEST

Suite *vmu_suite(void) {
    Suite *s;
    TCase *tc_core;

    s = suite_create("VMU");

    tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_init_system_state);
    tcase_add_test(tc_core, test_set_acceleration);
    tcase_add_test(tc_core, test_set_braking);
    tcase_add_test(tc_core, test_calculate_speed_accelerating);
    tcase_add_test(tc_core, test_calculate_speed_coasting);
    tcase_add_test(tc_core, test_calculate_speed_braking);
    tcase_add_test(tc_core, test_vmu_control_engines_below_transition_accelerating_battery);
    tcase_add_test(tc_core, test_vmu_control_engines_above_transition_accelerating_fuel);
    tcase_add_test(tc_core, test_vmu_control_engines_in_transition_accelerating_both);
    tcase_add_test(tc_core, test_vmu_control_engines_emergency_low_battery);
    tcase_add_test(tc_core, test_vmu_control_engines_emergency_low_fuel);
    tcase_add_test(tc_core, test_vmu_control_engines_both_low);
    tcase_add_test(tc_core, test_vmu_control_engines_not_accelerating);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void) {
    Suite *s = vmu_suite();
    SRunner *runner = srunner_create(s);

    srunner_run_all(runner, CK_NORMAL);
    int number_failed = srunner_ntests_failed(runner);
    srunner_free(runner);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}