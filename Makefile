CC = gcc

CFLAGS = -Wall -pthread

TARGET = main

SRCS = variables.c main.c terminal_config.c running_module.c driver_input.c display.c

OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


run: $(TARGET)
	./$(TARGET)


clean:
	rm -f $(OBJS) $(TARGET)
