CC = gcc
CFLAGS = -pthread -lrt -lm
LDLIBS = -lcheck -pthread -lm -lsubunit

# Diretórios
SRC_DIR = ../src
BIN_DIR = bin
TEST_DIRS = EV VMU IEC

# Alvos padrão
.PHONY: all run clean

all: run

run: $(BIN_DIR)/test_ev $(BIN_DIR)/test_vmu $(BIN_DIR)/test_iec

$(BIN_DIR)/test_ev: EV/test_ev.c $(SRC_DIR)/EV/ev.c | $(BIN_DIR)
	$(CC) -o $@ $^ $(LDLIBS)

$(BIN_DIR)/test_vmu: VMU/test_vmu.c $(SRC_DIR)/VMU/vmu.c | $(BIN_DIR)
	$(CC) -o $@ $^ $(LDLIBS)

$(BIN_DIR)/test_iec: IEC/test_iec.c $(SRC_DIR)/IEC/iec.c | $(BIN_DIR)
	$(CC) -o $@ $^ $(LDLIBS)

# Garante que o diretório bin existe
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -f $(BIN_DIR)/test_ev $(BIN_DIR)/test_vmu $(BIN_DIR)/test_iec