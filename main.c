#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>
#include "variables.h"
#include "terminal_config.h"
#include "running_module.h"
#include "driver_input.h"
#include "display.h"


void handle_sigint(int sig) {
    continuar = 0;
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
