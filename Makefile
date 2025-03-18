
CC = gcc


CFLAGS = -Wall -pthread


TARGET = main


all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c


run: $(TARGET)
	./$(TARGET)


clean:
	rm -f $(TARGET)