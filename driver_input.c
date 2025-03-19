#include "driver_input.h"

// Função que calcula a diferença de tempo em microsegundos
unsigned long diff_usec(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) * 1000000UL + (end.tv_usec - start.tv_usec);
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