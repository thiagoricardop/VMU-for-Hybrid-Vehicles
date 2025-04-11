CC = gcc
CFLAGS = -pthread -lrt -lm -I. -I./VMU -I./EV -I./IEC
BINDIR = bin
EXECS = $(BINDIR)/vmu $(BINDIR)/ev $(BINDIR)/iec

all: $(EXECS)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/vmu: | $(BINDIR)
	$(CC) VMU/main.c VMU/vmu.h -o $(BINDIR)/vmu $(CFLAGS)

$(BINDIR)/ev: | $(BINDIR)
	$(CC) EV/main.c EV/ev.h -o $(BINDIR)/ev $(CFLAGS)

$(BINDIR)/iec: | $(BINDIR)
	$(CC) IEC/main.c IEC/iec.h -o $(BINDIR)/iec $(CFLAGS)

clean:
	rm -f $(EXECS)