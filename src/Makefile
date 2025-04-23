CC     := gcc
CFLAGS := -pthread -lrt -lm \
          -IVMU/VARIABLES \
          -IIEC/VARIABLES \
          -IEV/VARIABLES

BIN_DIR := bin


VMU_SRCS := \
  vmu/vmu.c \
  vmu/INITIALIZER/vmu_initialization.c \
  vmu/VARIABLES/vmu_variables.c \
  vmu/SPEED/calculate_speed.c \
  vmu/INTERFACE/interface.c \
  vmu/QUEUE/check_queue.c \
  vmu/CLEANER/vmu_cleanUp.c \
  vmu/CONTROLLER/vmu_control_engines.c

VMU_OBJS := $(VMU_SRCS:.c=.o)


EV_SRCS := \
  ev/ev.c \
  ev/INITIALIZER/ev_initializer.c \
  ev/VARIABLES/ev_variables.c \
  ev/EVQUEUE/ev_receive.c \
  ev/CLEANER/ev_cleanUp.c \
  ev/CONTROL_CALCULATE/ev_control_calculate.c

EV_OBJS := $(EV_SRCS:.c=.o)


IEC_SRCS := \
  iec/iec.c \
  iec/INITIALIZER/iec_initializer.c \
  iec/IECQUEUE/iec_receive.c \
  iec/CONTROL_CALCULATE/iec_control_calculate.c \
  iec/CLEANER/iec_cleanUp.c \
  iec/VARIABLES/iec_variables.c

IEC_OBJS := $(IEC_SRCS:.c=.o)


.PHONY: all clean

all: $(BIN_DIR)/vmu $(BIN_DIR)/ev $(BIN_DIR)/iec

$(BIN_DIR):
	mkdir -p $@


$(BIN_DIR)/vmu: $(VMU_OBJS) | $(BIN_DIR)
	$(CC) $^ -o $@ $(CFLAGS)
	rm -f $(VMU_OBJS)

$(BIN_DIR)/ev: $(EV_OBJS) | $(BIN_DIR)
	$(CC) $^ -o $@ $(CFLAGS)
	rm -f $(EV_OBJS)

$(BIN_DIR)/iec: $(IEC_OBJS) | $(BIN_DIR)
	$(CC) $^ -o $@ $(CFLAGS)
	rm -f $(IEC_OBJS)


%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)


clean:
	rm -rf $(BIN_DIR) *.o

