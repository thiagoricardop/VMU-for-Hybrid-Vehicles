CC = gcc
CFLAGS = -pthread -lrt -lm -I. -I./vmu -I./ev -I./iec
BINDIR = bin
EXECS = $(BINDIR)/vmu $(BINDIR)/ev $(BINDIR)/iec

all: $(EXECS)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/vmu: | $(BINDIR)
	$(CC) vmu/main.c vmu/vmu.h -o $(BINDIR)/vmu $(CFLAGS)

$(BINDIR)/ev: | $(BINDIR)
	$(CC) ev/main.c ev/ev.h -o $(BINDIR)/ev $(CFLAGS)

$(BINDIR)/iec: | $(BINDIR)
	$(CC) iec/main.c iec/iec.h -o $(BINDIR)/iec $(CFLAGS)

clean:
	rm -f $(EXECS)