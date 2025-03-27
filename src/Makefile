CC = gcc
CFLAGS = -pthread -lrt -lm
VMU_DIR = VMU
IEC_DIR = IEC
EV_DIR = EV
BIN_DIR = bin
OBJS = $(VMU_DIR)/vmu.o $(IEC_DIR)/iec.o $(EV_DIR)/ev.o
EXECS = $(BIN_DIR)/vmu $(BIN_DIR)/ev $(BIN_DIR)/iec

all: $(BIN_DIR) $(EXECS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/vmu: $(VMU_DIR)/vmu.c $(VMU_DIR)/vmu.h
	$(CC) $(VMU_DIR)/vmu.c -o $(BIN_DIR)/vmu $(CFLAGS)

$(BIN_DIR)/ev: $(EV_DIR)/ev.c $(VMU_DIR)/vmu.h
	$(CC) $(EV_DIR)/ev.c -o $(BIN_DIR)/ev $(CFLAGS)

$(BIN_DIR)/iec: $(IEC_DIR)/iec.c $(VMU_DIR)/vmu.h
	$(CC) $(IEC_DIR)/iec.c -o $(BIN_DIR)/iec $(CFLAGS)

clean:
	rm -rf $(BIN_DIR)
