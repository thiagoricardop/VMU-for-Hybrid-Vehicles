#ifndef TERMINAL_CONFIG_H
#define TERMINAL_CONFIG_H

#include <termios.h>
#include <unistd.h>
#include <stdlib.h>


void restore_terminal(void);
void set_terminal_noncanonical(void);


#endif 