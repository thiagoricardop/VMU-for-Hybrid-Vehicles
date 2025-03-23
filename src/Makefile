CC = gcc
CFLAGS = -pthread -lrt -lm
OBJS = vmu.o ev.o iec.o
EXECS = vmu ev iec

all: $(EXECS)

vmu: vmu.c vmu.h
	$(CC) vmu.c -o vmu $(CFLAGS)

ev: ev.c vmu.h
	$(CC) ev.c -o ev $(CFLAGS)

iec: iec.c vmu.h
	$(CC) iec.c -o iec $(CFLAGS)

clean:
	rm -f $(OBJS) $(EXECS)