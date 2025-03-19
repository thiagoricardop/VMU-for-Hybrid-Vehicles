#include "terminal_config.h"

static struct termios orig_termios;

void restore_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

void set_terminal_noncanonical(void) {
    struct termios new_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(restore_terminal);
    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
}
