#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>

#define TAM_BUFFER 3
#define TIMEOUT_USEC 80000  // 80 ms

// Buffer para a entrada atual e para o valor armazenado
char input_buffer[TAM_BUFFER] = "";
char stored_value[TAM_BUFFER] = "";
char lastStoredValue[TAM_BUFFER] = "";
char brake[20] = "OFF";
char accelerator[20] = "OFF";
char running[11] = "Running";
char powertrain[20] = "None";
char events[30] = "Vehicle On";

unsigned char cont = 0;

float vehicle_speed = 0, iec_temperature = 0, iec_percentage = 0, ev_percentage = 0, Battery = 100, fuel_level = 45;  

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t continuar = 1;

struct termios orig_termios;

// Restaura as configurações originais do terminal
void restore_terminal() {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

// Configura o terminal para modo não canônico e sem eco
void set_terminal_noncanonical() {
    struct termios new_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(restore_terminal);
    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
}

// Handler para Ctrl+C
void handle_sigint(int sig) {
    continuar = 0;
}

// Função que calcula a diferença de tempo em microsegundos
unsigned long diff_usec(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) * 1000000UL + (end.tv_usec - start.tv_usec);
}

void runningAssign (const char *str) {
    strcpy(running, str);
}

void running_func () {
    cont++;
            
    switch (cont % 11) {
        case 0:
            runningAssign("          ");
            break;
        case 1:
            runningAssign("R         ");
            break;
        case 2:
            runningAssign("Ru        ");
            break;
        case 3:
            runningAssign("Run       ");
            break;
        case 4:
            runningAssign("Runn      ");
            break;
        case 5:
            runningAssign("Runni     ");
            break;
        case 6:
            runningAssign("Runnin    ");
            break;
        case 7:
            runningAssign("Running   ");
            break;
        case 8:
            runningAssign("Running.  ");
            break;
        case 9:
            runningAssign("Running.. ");
            break;
        case 10:
            runningAssign("Running...");
            break;

    }
}

void executeAction () {
    
    char *target = NULL;           
    const char *actionValue = NULL;

    switch (stored_value[0]){
        
        case '1':
            target = accelerator;
            actionValue = "Soft";
            break;
        
        case '2':
            target = accelerator;
            actionValue = "Medium";
            break;
            
        case '3':
            target = accelerator;
            actionValue = "Maximum";
            break;
        
        case '4':
            target = accelerator;
            actionValue = "Keep";
            strcpy (brake, "OFF");
            break;
        
        case '5':
            target = brake;
            actionValue = "Soft";
            break;

        case '6':    
            target = brake;
            actionValue = "Intense";
            break;
        
        default:
            return;

    }

    if (strcmp(stored_value, lastStoredValue) != 0) {
        if (!(target == brake && (strcmp(accelerator, "Keep") == 0))) {
            strcpy(target, actionValue);
        }
    } 
    
    else {
        if (strcmp(target, "OFF") == 0){

            if (!(target == brake && (strcmp(accelerator, "Keep") == 0))) {
                strcpy(target, actionValue);
            }
        }
        else {
            strcpy(target, "OFF");
        }
    }
    

}

// Thread que captura cada caractere a cada 80ms sem precisar de Enter
void* input_thread(void* arg) {
    struct timeval last_key_time, now;
    // Inicializa o tempo de última tecla
    gettimeofday(&last_key_time, NULL);

    while (continuar) {
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 80000; // 80 ms

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            char ch;
            int n = read(STDIN_FILENO, &ch, 1);
            if (n > 0) {
                pthread_mutex_lock(&mutex);
                // Trata backspace (ASCII 127 ou '\b')
                if (ch == 127 || ch == '\b') {
                    if (strlen(input_buffer) > 0) {
                        input_buffer[strlen(input_buffer)-1] = '\0';
                    }
                } else {
                    // Acumula caractere, se houver espaço
                    if (strlen(input_buffer) < TAM_BUFFER - 1) {
                        size_t len = strlen(input_buffer);
                        input_buffer[len] = ch;
                        input_buffer[len+1] = '\0';
                    }
                }
                pthread_mutex_unlock(&mutex);
                // Atualiza o tempo da última tecla lida
                gettimeofday(&last_key_time, NULL);
            }
        }

        // Verifica se o tempo desde a última tecla ultrapassou o limite
        gettimeofday(&now, NULL);
        if (diff_usec(last_key_time, now) >= TIMEOUT_USEC) {
            
            pthread_mutex_lock(&mutex);

            // Se houver algo no buffer de entrada, transfere para stored_value e limpa input_buffer
            if (strlen(input_buffer) > 0) {
                strncpy(stored_value, input_buffer, TAM_BUFFER - 1);
                stored_value[TAM_BUFFER - 1] = '\0';
                input_buffer[0] = '\0';
                executeAction();
                strcpy(lastStoredValue, stored_value);
            }

            pthread_mutex_unlock(&mutex);
        }
    }
    return NULL;
}

void* display_thread(void* arg) {
    while (continuar) {
        pthread_mutex_lock(&mutex);
        running_func();

        printf("\033[2;5H");
        printf("%s", running);

        printf("\033[3;5H");
        printf(" _________________________________________________");
        
        printf("\033[4;5H");
        printf("|                                                 |");

        printf("\033[5;5H");
        printf("|    Speed (KM/h):               %-17.6f|\033[K", vehicle_speed);
        printf("\033[6;5H");
        printf("|    Battery Charge (EV):        %-17.6f|\033[K", Battery);
        printf("\033[7;5H");
        printf("|    IEC Fuel Level (liters):    %-17.6f|\033[K", fuel_level);
        printf("\033[8;5H");
        printf("|    IEC Temperature (°C):       %-17.6f|\033[K", iec_temperature);
        printf("\033[9;5H");
        printf("|    Usage Percentage (IEC):     %-17.6f|\033[K", iec_percentage);
        printf("\033[10;5H");
        printf("|    Usage Percentage (EV):      %-17.6f|\033[K", ev_percentage);
        printf("\033[11;5H");
        printf("|    Powertrain Mode:            %-17s|\033[K", powertrain);
        printf("\033[12;5H");
        printf("|    Accelerator:                %-17s|\033[K", accelerator);
        printf("\033[13;5H");
        printf("|    Brake:                      %-17s|\033[K", brake);
        
        printf("\033[14;5H");
        printf("|_________________________________________________|");

        printf("\033[16;5H");
        printf("Driver inputs (press the same input twice cancels input):");

        printf("\033[17;5H");
        printf("1- Accelerate (soft)");
        printf("\033[18;5H");
        printf("2- Accelerate (medium)");
        printf("\033[19;5H");
        printf("3- Accelerate (maximum)");

        printf("\033[20;5H");
        printf("4- Keep Speed;");

        printf("\033[21;5H");
        printf("5- Brake (soft)");
        printf("\033[22;5H");
        printf("6- Brake (intense)");

        printf("\033[24;5H");
        printf("Current main event: %s\033[K", events);

        printf("\033[26;5H");
        printf("Last input stored: %s\033[K", stored_value);
        
        printf("\033[27;5H");
        printf("Option: %s\033[K", input_buffer);
        pthread_mutex_unlock(&mutex);
        fflush(stdout);
        usleep(80000);  
    }
    return NULL;
}

int main() {
    signal(SIGINT, handle_sigint);
    set_terminal_noncanonical();

    // Limpa a tela e posiciona o cursor no topo
    printf("\033[2J");
    printf("\033[H");
    fflush(stdout);

    pthread_t tid_input, tid_display;
    if (pthread_create(&tid_input, NULL, input_thread, NULL) != 0) {
        perror("Erro ao criar thread de input");
        return 1;
    }
    if (pthread_create(&tid_display, NULL, display_thread, NULL) != 0) {
        perror("Erro ao criar thread de display");
        return 1;
    }
    printf("\n");
    pthread_join(tid_input, NULL);
    pthread_join(tid_display, NULL);
    pthread_mutex_destroy(&mutex);
    return 0;
}
