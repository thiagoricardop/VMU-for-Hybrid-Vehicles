#include "display.h"

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
        printf("Current main event: %s\033[K", eventsDisplayed);

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