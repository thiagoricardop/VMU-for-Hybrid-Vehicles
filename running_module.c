#include "running_module.h"

void runningAssign (const char *str, const char *event) {
    strcpy(running, str);
    strcpy(eventsDisplayed, event);
}

void running_func () {
    cont++;
            
    switch (cont % 11) {
        case 0:
            runningAssign("          ", events);
            break;
        case 1:
            runningAssign("R         ", events);
            break;
        case 2:
            runningAssign("Ru        ", events);
            break;
        case 3:
            runningAssign("Run       ", events);
            break;
        case 4:
            runningAssign("Runn      ", events);
            break;
        case 5:
            runningAssign("Runni     ", events);
            break;
        case 6:
            runningAssign("Runnin    ", events);
            break;
        case 7:
            runningAssign("Running   ", events);
            break;
        case 8:
            runningAssign("Running.  ", events);
            break;
        case 9:
            runningAssign("Running.. ", "                             ");
            break;
        case 10:
            runningAssign("Running...", "                             ");
            break;

    }
}