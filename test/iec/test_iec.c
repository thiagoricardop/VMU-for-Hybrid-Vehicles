#include <check.h>
#include <stdlib.h>
#include <math.h>
#include <semaphore.h>
#include <stdio.h>
#include <unistd.h>
#include "../../src/iec/iec.h"  // Adjust the path as needed
#include "../../src/vmu/vmu.h"

// Global pointers used by the engine() function.
static SystemState *test_state = NULL;
static sem_t *test_sem = NULL;

// Setup function: allocate and initialize the dummy shared state and semaphore.
void setup(void) {
    test_state = malloc(sizeof(SystemState));
    if (!test_state) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    // Initialize with default values.
    test_state->iec_on = false;
    test_state->rpm_iec = 0;
    test_state->temp_iec = 25.0;      // Ambient temperature.
    test_state->transition_factor = 0.0;
    system_state = test_state;        // Redirect global pointer.

    test_sem = malloc(sizeof(sem_t));
    if (sem_init(test_sem, 0, 1) != 0) {
        perror("sem_init");
        exit(EXIT_FAILURE);
    }
    sem = test_sem;                   // Redirect global semaphore pointer.
}

// Teardown function: free allocated resources.
void teardown(void) {
    sem_destroy(test_sem);
    free(test_sem);
    free(test_state);
}

//---------------------------------------------------------------
// Test case 1: IEC is ON - RPM and temperature update correctly.
START_TEST(test_engine_iec_on) {
    system_state->iec_on = true;
    system_state->transition_factor = 0.3; // Example factor.
    system_state->temp_iec = 20.0;           // Initial temperature.

    engine();

    // Expected values:
    // rpm_iec = (transition_factor * 5000) = 0.3 * 5000 = 1500.
    int expected_rpm = (int)(0.3 * 5000);
    // Temperature increases by transition_factor * 0.1 = 0.3 * 0.1 = 0.03.
    double expected_temp = 20.0 + (0.3 * 0.1);

    ck_assert_int_eq(system_state->rpm_iec, expected_rpm);
    ck_assert_msg(fabs(system_state->temp_iec - expected_temp) < 0.0001,
                  "Temperature did not update correctly when IEC is on.");
}
END_TEST

//---------------------------------------------------------------
// Test case 2: IEC is OFF with high temperature (>25.0) - RPM set to 0 and temperature decreases by 0.02.
START_TEST(test_engine_iec_off_high_temp) {
    system_state->iec_on = false;
    system_state->temp_iec = 30.0; // Above ambient.
    system_state->rpm_iec = 2500;  // Arbitrary nonzero value.

    engine();

    ck_assert_int_eq(system_state->rpm_iec, 0);
    double expected_temp = 30.0 - 0.02;
    ck_assert_msg(fabs(system_state->temp_iec - expected_temp) < 0.0001,
                  "Temperature did not cool correctly when IEC is off and above ambient.");
}
END_TEST

//---------------------------------------------------------------
// Test case 3: IEC is OFF with ambient temperature (<=25.0) - RPM set to 0 and temperature remains unchanged.
START_TEST(test_engine_iec_off_low_temp) {
    system_state->iec_on = false;
    system_state->temp_iec = 25.0; // At ambient.
    system_state->rpm_iec = 123;   // Arbitrary nonzero value.

    engine();

    ck_assert_int_eq(system_state->rpm_iec, 0);
    ck_assert_msg(fabs(system_state->temp_iec - 25.0) < 0.0001,
                  "Temperature should not change when IEC is off and at ambient level.");
}
END_TEST

//---------------------------------------------------------------
// Create the test suite.
Suite *engine_suite(void) {
    Suite *s;
    TCase *tc_core;

    s = suite_create("IEC Engine");
    tc_core = tcase_create("Core");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_engine_iec_on);
    tcase_add_test(tc_core, test_engine_iec_off_high_temp);
    tcase_add_test(tc_core, test_engine_iec_off_low_temp);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void) {
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = engine_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
