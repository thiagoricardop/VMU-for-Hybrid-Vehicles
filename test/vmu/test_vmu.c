#define _DEFAULT_SOURCE

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <mqueue.h>
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

#include "../../src/vmu/vmu.h"


// Mocking shared memory and semaphore functions
SystemState *mock_system_state;
sem_t *mock_sem;

mqd_t ev_mqd, iec_mqd;

/* Stub acceleration/brake setters */
bool accel_flag;
bool brake_flag;

/* Local test infrastructure */
static SystemState state_buf;
static sem_t      sem_buf;
static bool       send_called;

/* Stub queues */
static int                 stub_mode; /* 1=EV, 2=IEC */
static size_t              ev_qsize, ev_qidx;
static EngineCommandEV     ev_queue[4];
static size_t              iec_qsize, iec_qidx;
static EngineCommandIEC    iec_queue[4];

static int    saved_stdin;
static int    pipe_fds[2];

// cada wrap retornará erro
int __wrap_shm_open(const char *n, int f, mode_t m)   { errno = EACCES; return -1; }
int __wrap_ftruncate(int fd, off_t len)               { errno = EINVAL; return -1; }
void * __wrap_mmap(void *a, size_t l, int p, int f, int fd, off_t o) {
    return MAP_FAILED;
}
sem_t * __wrap_sem_open(const char *n, int f, mode_t m, unsigned v) {
    return SEM_FAILED;
}
mqd_t __wrap_mq_open(const char *n, int o, ...)         { return (mqd_t)-1; }
int __wrap_pthread_create(pthread_t *t, const void *a,
                          void *(*r)(void *), void *p) { return EAGAIN; }

// helper que executa vmu_initialization em fork e checa exit failure
static void assert_init_fails(void) {
    pid_t pid = fork();
    ck_assert_int_ge(pid, 0);
    if (pid == 0) {
        vmu_initialization();
        _exit(0); // não deveria chegar aqui
    } else {
        int status;
        waitpid(pid, &status, 0);
        ck_assert(WIFEXITED(status));
        ck_assert_int_eq(WEXITSTATUS(status), EXIT_FAILURE);
    }
}


/* Fixtures */
/* Fixture de setup: cria as MQs e o semáforo, e inicializa o state */
void vmu_queue_setup(void) {
    /* Remove restos de runs anteriores */
    mq_unlink("/test_iec_queue");
    mq_unlink("/test_ev_queue");
    sem_unlink("/test_sem");

    /* Atributos das filas */
    struct mq_attr attr_iec = {
        .mq_flags   = 0,
        .mq_maxmsg  = 10,
        .mq_msgsize = sizeof(EngineCommandIEC),
        .mq_curmsgs = 0
    };
    struct mq_attr attr_ev = {
        .mq_flags   = 0,
        .mq_maxmsg  = 10,
        .mq_msgsize = sizeof(EngineCommandEV),
        .mq_curmsgs = 0
    };

    /* Cria/open das filas */
    iec_mqd = mq_open("/test_iec_queue",
                      O_CREAT | O_RDWR | O_NONBLOCK,
                      0666, &attr_iec);
    ck_assert_msg(iec_mqd != (mqd_t)-1, "Falha ao criar /test_iec_queue");

    ev_mqd = mq_open("/test_ev_queue",
                     O_CREAT | O_RDWR | O_NONBLOCK,
                     0666, &attr_ev);
    ck_assert_msg(ev_mqd != (mqd_t)-1, "Falha ao criar /test_ev_queue");

    /* Cria/open do semáforo */
    sem = sem_open("/test_sem",
                   O_CREAT | O_EXCL,
                   0666, 1);
    ck_assert_msg(sem != SEM_FAILED, "Falha ao criar /test_sem");

    /* Aloca e inicializa o estado */
    system_state = malloc(sizeof *system_state);
    ck_assert_ptr_nonnull(system_state);
    init_system_state(system_state);
}

/* Fixture de teardown: fecha e unlink de tudo */
void vmu_queue_teardown(void) {
    /* Fecha e remove filas */
    mq_close(iec_mqd);
    mq_unlink("/test_iec_queue");

    mq_close(ev_mqd);
    mq_unlink("/test_ev_queue");

    /* Fecha e remove semáforo */
    sem_close(sem);
    sem_unlink("/test_sem");

    /* Libera o estado */
    free(system_state);
}

// Fixture: antes de cada teste, garante arquivo limpo e estados iniciais
static void redirect_setup(void) {
    // remove arquivo anterior
    unlink("out.txt");
    // reset globals
    paused = 0;
    running = 1;
    // redireciona stdout para “out.txt”
    freopen("out.txt", "w+", stdout);
}

static void redirect_teardown(void) {
    // restaura stdout se necessário (omitido para brevidade)

}

// Fixtures: iniciam VMU para criar recursos
void cleanSetup(void) {
    // Garante ambiente limpo antes
    shm_unlink(SHARED_MEM_NAME);                                    // :contentReference[oaicite:0]{index=0}
    sem_unlink(SEMAPHORE_NAME);
    mq_unlink(EV_COMMAND_QUEUE_NAME);
    mq_unlink(IEC_COMMAND_QUEUE_NAME);

    // Inicializa a VMU para criar filas, shm e semáforo
    vmu_initialization();                                           // :contentReference[oaicite:1]{index=1}
}

void cleanTeardown(void) {

    // caso cleanup não tenha sido chamado, limpamos aqui
    mq_close(ev_mq);
    mq_close(iec_mq);
    mq_unlink(EV_COMMAND_QUEUE_NAME);
    mq_unlink(IEC_COMMAND_QUEUE_NAME);
    munmap(system_state, sizeof(SystemState));
    shm_unlink(SHARED_MEM_NAME);
    sem_close(sem);
    sem_unlink(SEMAPHORE_NAME);
}


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

void input_setup(void) {
    /* Create a pipe and redirect stdin */
    ck_assert(pipe(pipe_fds) == 0);
    saved_stdin = dup(STDIN_FILENO);
    ck_assert(saved_stdin >= 0);
    fflush(stdin);
    dup2(pipe_fds[0], STDIN_FILENO);
    close(pipe_fds[0]);

    /* Initialize shared state and semaphore */
    static SystemState st;
    system_state = &st;
    memset(system_state, 0, sizeof(st));
    static sem_t s;
    sem_init(&s, 0, 1);
    sem = &s;

    /* Initialize control flags */
    running = 1;
    finish  = 0;

    /* Spawn reader thread */
    int rc = pthread_create(&input_thread, NULL, read_input, NULL);
    ck_assert(rc == 0);
}

void input_teardown(void) {
    /* Signal thread to stop and close writer for EOF */

    running = 0;
    close(pipe_fds[1]);
    /* Join thread */
    pthread_join(input_thread, NULL);
    /* Restore stdin */
    fflush(stdin);
    dup2(saved_stdin, STDIN_FILENO);
    close(saved_stdin);
    /* Destroy semaphore */
    sem_destroy(sem);
}

/*
START_TEST(test_shm_open_fail)   { assert_init_fails(); } END_TEST
START_TEST(test_ftruncate_fail)  { assert_init_fails(); } END_TEST
START_TEST(test_mmap_fail)       { assert_init_fails(); } END_TEST
START_TEST(test_sem_open_fail)   { assert_init_fails(); } END_TEST
START_TEST(test_ev_mq_fail)      { assert_init_fails(); } END_TEST
START_TEST(test_iec_mq_fail)     { assert_init_fails(); } END_TEST
START_TEST(test_thread_create_fail) { assert_init_fails(); } END_TEST
*/

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

START_TEST(test_iecTransitionsActivated_end)
{
    transitionIEC = true;
    mock_system_state->iecPercentage = 1.1;
    mock_system_state->evPercentage = 0.57;
    mock_system_state->speed = 98.0;
    mock_system_state->battery = 8.0;
    mock_system_state->fuel = 44.0;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    ck_assert_int_eq(transitionIEC, true);
    ck_assert_int_eq(transitionEV, false);
    ck_assert_double_eq_tol(mock_system_state->evPercentage, 0.0, 1e-9);
    ck_assert_double_eq_tol(mock_system_state->iecPercentage, 1.0, 1e-9);
}
END_TEST

START_TEST(test_evcTransitionsActivated_end)
{
    transitionEV = true;
    mock_system_state->iecPercentage = 0.002;
    mock_system_state->evPercentage = 0.998;
    mock_system_state->speed = 38.0;
    mock_system_state->battery = 100.0;
    mock_system_state->fuel = 44.0;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    ck_assert_double_eq_tol(mock_system_state->evPercentage, 1.0, 1e-9);
    ck_assert_double_eq_tol(mock_system_state->iecPercentage, 0.0, 1e-9);
}
END_TEST

START_TEST(test_carStopped_true)
{
    carStop = true;
    mock_system_state->speed = 38.0;
    mock_system_state->battery = 60.0;
    mock_system_state->fuel = 0.0;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    ck_assert(carStop);
}
END_TEST

START_TEST(test_carStopped_true_batteryFull)
{
    carStop = true;
    mock_system_state->speed = 38.0;
    mock_system_state->battery = 100.0;
    mock_system_state->fuel = 0.0;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    ck_assert(!carStop);
}
END_TEST

START_TEST(test_carStopped_activated)
{
    carStop = true;
    mock_system_state->speed = 38.0;
    mock_system_state->battery = 0.0;
    mock_system_state->fuel = 0.0;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    ck_assert(carStop);
}
END_TEST

START_TEST(test_standard_hybrid)
{
    mock_system_state->speed = 60.0;
    mock_system_state->battery = 20.0;
    mock_system_state->fuel = 20.0;
    transitionEV = false;
    mock_system_state->accelerator = true;
    vmu_control_engines();
    
}
END_TEST


START_TEST(test_read_input_zero) {
    /* Send "1\n" to the thread via pipe */
    const char *cmd = "0\n";
    write(pipe_fds[1], cmd, strlen(cmd));
    /* Close writer to signal EOF */
    close(pipe_fds[1]);
    /* Allow thread to process */
    usleep(1000);

    ck_assert_msg(!system_state->brake,    "Expected brake OFF");
}
END_TEST

START_TEST(test_read_input_one) {
    /* Send "1\n" to the thread via pipe */
    const char *cmd = "1\n";
    write(pipe_fds[1], cmd, strlen(cmd));
    /* Close writer to signal EOF */
    close(pipe_fds[1]);
    /* Allow thread to process */
    usleep(1000);

    ck_assert_msg(!system_state->brake,    "Expected brake OFF");
}
END_TEST

START_TEST(test_read_input_two) {
    /* Send "1\n" to the thread via pipe */
    const char *cmd = "2\n";
    write(pipe_fds[1], cmd, strlen(cmd));
    /* Close writer to signal EOF */
    close(pipe_fds[1]);
    /* Allow thread to process */
    usleep(1000);

}
END_TEST

/* Tests for display_status */
START_TEST(test_display_status_output) {
    /* prepare a state */
    SystemState st = {
        .speed        = 12.3,
        .rpm_ev       = 45.6,
        .rpm_iec      = 78.9,
        .evPercentage = 0.25f,
        .iecPercentage= 0.75f,
        .ev_on        = true,
        .iec_on       = false,
        .temp_ev      = 30.0f,
        .temp_iec     = 40.0f,
        .battery      = 55.5f,
        .fuel         = 6.789f,
        .power_mode   = 2,
        .accelerator  = true,
        .brake        = false,
        .debg         = "DEBUG_MSG"
    };

    /* initialize globals */
    cont = 0;
    transitionEV = true;

    /* --- setup pipe to capture stdout --- */
    int pipefd[2];
    ck_assert_msg(pipe(pipefd) == 0, "pipe() failed");

    int saved_stdout = dup(STDOUT_FILENO);
    ck_assert_msg(saved_stdout >= 0, "dup() failed");

    /* redirect stdout to pipe write end */
    fflush(stdout);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    /* call function under test */
    display_status(&st);

    /* restore stdout */
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    /* read everything from pipe read end */
    char buf[4096];
    ssize_t n = read(pipefd[0], buf, sizeof(buf)-1);
    close(pipefd[0]);
    ck_assert_msg(n >= 0, "read() failed");
    buf[n] = '\0';

    /* now assert on contents */
    ck_assert_msg(strstr(buf, "Speed: 012 km/h")    != NULL, "Missing speed");
    ck_assert_msg(strstr(buf, "RPM EV: 046")      != NULL, "Missing RPM EV");
    ck_assert_msg(strstr(buf, "EV: ON")            != NULL, "EV on");
    ck_assert_msg(strstr(buf, "IEC: OFF")           != NULL, "IEC off");
    ck_assert_msg(strstr(buf, "Battery: 55.50%")    != NULL, "Battery");
    ck_assert_msg(strstr(buf, "Fuel (liters): 6.789") != NULL, "Fuel");
    ck_assert_msg(strstr(buf, "Power mode: Combustion Only") != NULL, "Power mode");
    ck_assert_msg(strstr(buf, "Last mensage: DEBUG_MSG") != NULL, "Debug msg");

}
END_TEST

START_TEST(test_display_mock) {
    /* prepare a state */
    SystemState st = {
        .speed        = 12.3,
        .rpm_ev       = 45.6,
        .rpm_iec      = 78.9,
        .evPercentage = 0.25f,
        .iecPercentage= 0.75f,
        .ev_on        = true,
        .iec_on       = false,
        .temp_ev      = 30.0f,
        .temp_iec     = 40.0f,
        .battery      = 55.5f,
        .fuel         = 6.789f,
        .power_mode   = 0,
        .accelerator  = true,
        .brake        = false,
        .debg         = "DEBUG_MSG"
    };

    /* initialize globals */
    cont = 0;
    transitionEV = true;

    /* --- setup pipe to capture stdout --- */
    int pipefd[2];
    ck_assert_msg(pipe(pipefd) == 0, "pipe() failed");

    int saved_stdout = dup(STDOUT_FILENO);
    ck_assert_msg(saved_stdout >= 0, "dup() failed");

    /* redirect stdout to pipe write end */
    fflush(stdout);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    /* call function under test */
    display_status(&st);

    st.ev_on = false;
    st.iec_on = true;
    st.power_mode = 2;

    display_status(&st);

    st.accelerator = false;
    st.brake = true;

    display_status(&st);

    /* restore stdout */
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    /* read everything from pipe read end */
    char buf[4096];
    ssize_t n = read(pipefd[0], buf, sizeof(buf)-1);
    close(pipefd[0]);
    ck_assert_msg(n >= 0, "read() failed");
    buf[n] = '\0';


}
END_TEST

START_TEST(test_iec_toVMU_true) {
    /* 2) Prepara e envia o comando */
    EngineCommandIEC cmd = {
        .toVMU     = true,
        .rpm_iec   = 55.5f,
        .fuelIEC   = 3.14f,
        .iecActive = true
    };
    strcpy(cmd.check, "ok");
    mq_send(iec_mqd, (const char *)&cmd, sizeof(cmd), 0);
    

    /* 3) Chama a função que faz sem_wait/receive/process/post */
    vmu_check_queue(0, iec_mqd, false);

    /* 4) Verifica que o estado foi atualizado */
    ck_assert_str_eq(lastmsg, "ok");
    ck_assert_float_eq(system_state->rpm_iec, 55.5f);
    ck_assert_float_eq(system_state->fuel,    3.14f);
    ck_assert(system_state->iec_on);
    ck_assert_int_eq(safetyCount, 0);
    ck_assert_int_eq(localcount, 1);
}
END_TEST

START_TEST(test_iec_toVMU_false) {
    /* 2) Prepara e envia o comando */
    EngineCommandIEC cmd = {
        .toVMU     = false,
        .rpm_iec   = 55.5f,
        .fuelIEC   = 3.14f,
        .iecActive = true
    };
    strcpy(cmd.check, "no");
    int rc = mq_send(iec_mqd, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_eq(rc, 0);

    /* 3) Chama a função que faz sem_wait/receive/process/post */
    vmu_check_queue(0, iec_mqd, false);

    /* 4) Verifica que o estado foi atualizado */

    ck_assert_int_eq(localcount, 0);
}
END_TEST

START_TEST(test_ev_toVMU_true) {
    /* 2) Prepara e envia o comando */
    EngineCommandEV cmd = {
        .toVMU     = true,
        .rpm_ev    = 258.8f,
        .batteryEV = 13.4f,
        .evActive = true
    };
    strcpy(cmd.check, "ok");
    mq_send(ev_mqd, (const char *)&cmd, sizeof(cmd), 0);
    

    /* 3) Chama a função que faz sem_wait/receive/process/post */
    vmu_check_queue(0, ev_mqd, true);

    /* 4) Verifica que o estado foi atualizado */
    ck_assert_str_eq(lastmsg, "ok");
    ck_assert_float_eq(system_state->rpm_ev, 258.8f);
    ck_assert_float_eq(system_state->battery,    13.4f);
    ck_assert(system_state->ev_on);
    ck_assert_int_eq(safetyCount, 0);
    ck_assert_int_eq(localcount, 1);
}
END_TEST

START_TEST(test_ev_toVMU_false) {
    /* 2) Prepara e envia o comando */
    EngineCommandEV cmd = {
        .toVMU     = false,
        .rpm_ev    = 258.8f,
        .batteryEV = 13.4f,
        .evActive = true
    };
    strcpy(cmd.check, "no");
    int rc = mq_send(ev_mqd, (const char *)&cmd, sizeof(cmd), 0);
    ck_assert_int_eq(rc, 0);

    /* 3) Chama a função que faz sem_wait/receive/process/post */
    vmu_check_queue(0, ev_mqd, true);

    /* 4) Verifica que o estado foi atualizado */

    ck_assert_int_eq(localcount, 0);
}
END_TEST

START_TEST(test_vmu_init_success) {

    testing = true;
    vmu_initialization();

    // 1) system_state mapeado
    ck_assert_ptr_nonnull(system_state);

    // 2) semáforo
    ck_assert_ptr_nonnull(sem);

    // 3) filas de mensagem
    ck_assert_int_ge(ev_mq, 0);
    ck_assert_int_ge(iec_mq, 0);

    // 4) thread criada?
    // pthread_kill(thread, 0) só verifica existência
    ck_assert_int_eq(pthread_kill(input_thread, 0), 0);

    // 5) cancela a thread para liberar o teste
    ck_assert_int_eq(pthread_cancel(input_thread), 0);


}
END_TEST


START_TEST(test_sigusr1_toggle_on) {
    // paused inicia false
    ck_assert_int_eq(paused, 0);
    handle_signal(SIGUSR1);
    // variável global mudou
    ck_assert_int_eq(paused, 1);
    // lê saída
    fflush(stdout);
    struct stat st; 
    stat("out.txt", &st);
    ck_assert_int_gt(st.st_size, 0);
    // compara conteúdo
    char buf[64];
    FILE *f = fopen("out.txt","r");
    fgets(buf, sizeof(buf), f);
    fclose(f);
    ck_assert_str_eq(buf, "[VMU] Paused: true\n");
}
END_TEST

START_TEST(test_sigusr1_toggle_off) {
    paused = 1;
    handle_signal(SIGUSR1);
    ck_assert_int_eq(paused, 0);
    fflush(stdout);
    char buf[64];
    FILE *f = fopen("out.txt","r");
    fgets(buf, sizeof(buf), f);
    fclose(f);
    ck_assert_str_eq(buf, "[VMU] Paused: false\n");
}
END_TEST

START_TEST(test_sigint_shut) {
    ck_assert_int_eq(running, 1);
    handle_signal(SIGINT);
    ck_assert_int_eq(running, 0);
    fflush(stdout);
    char buf[64];
    FILE *f = fopen("out.txt","r");
    fgets(buf, sizeof(buf), f);
    fclose(f);
    ck_assert_str_eq(buf, "[VMU] Shutting down...\n");
}
END_TEST

START_TEST(test_sigterm_shut) {
    running = 1;
    handle_signal(SIGTERM);
    ck_assert_int_eq(running, 0);
    fflush(stdout);
    char buf[64];
    FILE *f = fopen("out.txt","r");
    fgets(buf, sizeof(buf), f);
    fclose(f);
    ck_assert_str_eq(buf, "[VMU] Shutting down...\n");
}
END_TEST

// Helper para receber um comando de término de uma fila já aberta
static void assert_received_cmd_end(mqd_t mq) {
    EngineCommandEV ev;
    EngineCommandIEC iec;
    ssize_t r = mq_receive(mq, (char *)&ev, sizeof(ev), NULL);      // :contentReference[oaicite:2]{index=2}
    ck_assert_int_eq(ev.type, CMD_END);                             // :contentReference[oaicite:3]{index=3}
}

// Teste principal
START_TEST(test_cleanup_resources) {
    vmuProduction = false;
    testing = true;
    vmu_initialization();

    // Chama o cleanup
    cleanUp();

}
END_TEST

Suite *cleanup_suite(void) {
    Suite *s = suite_create("CleanUp");
    TCase *tc = tcase_create("Core");
    tcase_add_checked_fixture(tc, setup, teardown);                 
    tcase_add_test(tc, test_cleanup_resources);
    suite_add_tcase(s, tc);
    return s;
}


Suite *vmu_suite(void) {
    Suite *s;
    TCase *tc_core, *tc_read, *tc_disp, *tc_queue, *tc_signal, *tc_cleanUp, *tc_initFail;

    s = suite_create("VMU");

    /*
    tc_initFail = tcase_create("Failures");
    // executamos in‑process para cobertura
    tcase_add_checked_fixture(tc_initFail, NULL, NULL);
    tcase_add_test(tc_initFail, test_shm_open_fail);
    tcase_add_test(tc_initFail, test_ftruncate_fail);
    tcase_add_test(tc_initFail, test_mmap_fail);
    tcase_add_test(tc_initFail, test_sem_open_fail);
    tcase_add_test(tc_initFail, test_ev_mq_fail);
    tcase_add_test(tc_initFail, test_iec_mq_fail);
    tcase_add_test(tc_initFail, test_thread_create_fail);
    suite_add_tcase(s, tc_initFail);
    */
    
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
    tcase_add_test(tc_core, test_vmu_init_success);
    tcase_add_test(tc_core, test_iecTransitionsActivated_end);
    tcase_add_test(tc_core, test_evcTransitionsActivated_end);
    tcase_add_test(tc_core, test_carStopped_true);
    tcase_add_test(tc_core, test_carStopped_true_batteryFull);
    tcase_add_test(tc_core, test_carStopped_activated);
    tcase_add_test(tc_core, test_standard_hybrid);
    suite_add_tcase(s, tc_core);

    tc_read = tcase_create("ReadInput");
    tcase_add_checked_fixture(tc_read, input_setup, input_teardown);
    tcase_add_test(tc_read, test_read_input_zero);
    tcase_add_test(tc_read, test_read_input_one);
    tcase_add_test(tc_read, test_read_input_two);
    suite_add_tcase(s, tc_read);

    tc_disp = tcase_create("DisplayStatus");
    tcase_add_test(tc_disp, test_display_status_output);
    suite_add_tcase(s, tc_disp);

    tc_queue = tcase_create("receiveQueue");
    tcase_add_checked_fixture(tc_queue, vmu_queue_setup, vmu_queue_teardown);
    tcase_add_test(tc_queue, test_iec_toVMU_true);
    tcase_add_test(tc_queue, test_iec_toVMU_false);
    tcase_add_test(tc_queue, test_ev_toVMU_true);
    tcase_add_test(tc_queue, test_ev_toVMU_false);
    suite_add_tcase(s, tc_queue);

    tc_signal = tcase_create("Signal");
    tcase_add_checked_fixture(tc_signal, redirect_setup, redirect_teardown);
    tcase_add_test(tc_signal, test_sigusr1_toggle_on);
    tcase_add_test(tc_signal, test_sigusr1_toggle_off);
    tcase_add_test(tc_signal, test_sigint_shut);
    tcase_add_test(tc_signal, test_sigterm_shut);
    suite_add_tcase(s, tc_signal);

    tc_cleanUp = tcase_create("CleanUp");
    tcase_add_checked_fixture(tc_cleanUp, cleanSetup, cleanTeardown);                 
    tcase_add_test(tc_cleanUp, test_cleanup_resources);
    suite_add_tcase(s, tc_cleanUp);

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