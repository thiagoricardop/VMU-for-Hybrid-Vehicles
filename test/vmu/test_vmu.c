#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stdio.h>

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

    transitionEV = false;
    transitionIEC = false;

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

// Test case for vmu_control_engines (Speed below 45km/h , Battery above 10%, Eletric Only) 
START_TEST(test_evonly_atevrange)
{
    mock_system_state->speed = 18.0;
    mock_system_state->battery = 50.0;
    mock_system_state->fuel = 45.0;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    ck_assert_int_eq(mock_system_state->power_mode, 0); // Electric Only
    ck_assert_double_eq_tol(mock_system_state->evPercentage, 1.0, 1e-9);
    ck_assert_double_eq_tol(mock_system_state->iecPercentage, 0.0, 1e-9);
}
END_TEST

// Test case for vmu_control_engines (Speed equal 0.0 , Battery under 10%, Combustion Only)
START_TEST(test_ieconly_atevrange)
{
    mock_system_state->speed = 0.0;
    mock_system_state->battery = 9.0;
    mock_system_state->fuel = 45.0;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    ck_assert_int_eq(mock_system_state->power_mode, 2); // Combustion Only
    ck_assert_double_eq_tol(mock_system_state->evPercentage, 0.0, 1e-9);
    ck_assert_double_eq_tol(mock_system_state->iecPercentage, 1.0, 1e-9);
}
END_TEST

// Test case for vmu_control_engines (Fuel empty, verify if transition EV is activated)
START_TEST(test_evtransitionactivated_nofuel)
{
    mock_system_state->speed = 98.0;
    mock_system_state->battery = 50.0;
    mock_system_state->fuel = 0.0;
    mock_system_state->accelerator = true;
    transitionEV = false;
    vmu_control_engines();
    ck_assert_int_eq(mock_system_state->accelerator, false);
    ck_assert_int_eq(transitionEV, true);
}
END_TEST

// Test case for vmu_control_engines (emergency: low battery, below threshold, sufficient fuel)
START_TEST(test_evtransitionsActivated_batteryfull)
{
    mock_system_state->evPercentage = 0.57;
    mock_system_state->iecPercentage = 0.43;
    mock_system_state->speed = 98.0;
    mock_system_state->battery = 100.0;
    mock_system_state->fuel = 44.0;
    mock_system_state->accelerator = true;
    transitionEV = false;
    transitionIEC = true;
    vmu_control_engines();
    ck_assert_int_eq(transitionIEC, false);
    ck_assert_int_eq(transitionEV, true);
    ck_assert_int_eq(mock_system_state->power_mode, 1); // Hybrid
}
END_TEST

// Test case for vmu_control_engines (emergency: low fuel, above threshold, sufficient battery)
START_TEST(test_iectransitionsActivated_batteryunderten)
{
    mock_system_state->evPercentage = 0.57;
    mock_system_state->iecPercentage = 0.43;
    mock_system_state->speed = 98.0;
    mock_system_state->battery = 8.0;
    mock_system_state->fuel = 44.0;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    ck_assert_int_eq(transitionIEC, true);
    ck_assert_int_eq(transitionEV, false);
}
END_TEST

// Test case for vmu_control_engines (both battery and fuel low)
START_TEST(test_transitionFromIecToStandard)
{
    mock_system_state->evPercentage = 0.497;
    mock_system_state->iecPercentage = 0.503;
    transitionEV = true;
    mock_system_state->speed = 90.0;
    mock_system_state->battery = 98.0;
    mock_system_state->fuel = 44.0;
    vmu_control_engines();
    ck_assert_int_eq(transitionIEC, false);
    ck_assert_int_eq(transitionEV, false);
    ck_assert_int_eq(mock_system_state->power_mode, 1); // Hybrid
    ck_assert_double_eq_tol(mock_system_state->evPercentage, 0.5, 1e-9);
    ck_assert_double_eq_tol(mock_system_state->iecPercentage, 0.5, 1e-9);
}
END_TEST

// Test case for vmu_control_engines (not accelerating)
START_TEST(test_transitionFromHybridToEv)
{
    mock_system_state->evPercentage = 0.99;
    mock_system_state->iecPercentage = 0.01;
    transitionEV = true;
    mock_system_state->speed = 44.0;
    mock_system_state->battery = 88.0;
    mock_system_state->fuel = 0.0;
    mock_system_state->accelerator = true;    
    vmu_control_engines();
    ck_assert_int_eq(transitionIEC, false);
    ck_assert_int_eq(transitionEV, false);
    ck_assert_int_eq(mock_system_state->power_mode, 0); // Hybrid
    ck_assert_double_eq_tol(mock_system_state->evPercentage, 1.0, 1e-9);
    ck_assert_double_eq_tol(mock_system_state->iecPercentage, 0.0, 1e-9);
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
    tcase_add_test(tc_core, test_evonly_atevrange);
    tcase_add_test(tc_core, test_ieconly_atevrange);
    tcase_add_test(tc_core, test_evtransitionactivated_nofuel);
    tcase_add_test(tc_core, test_evtransitionsActivated_batteryfull);
    tcase_add_test(tc_core, test_iectransitionsActivated_batteryunderten);
    tcase_add_test(tc_core, test_transitionFromIecToStandard);
    tcase_add_test(tc_core, test_transitionFromHybridToEv);
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