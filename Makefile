CC = gcc
CFLAGS = -pthread -lrt -lm -fprofile-arcs -ftest-coverage -fcondition-coverage
CFLAGS += -I. -I.src/vmu -I.src/ev -I.src/iec \
          -I.src/vmu/VARIABLES \
          -I.src/iec/VARIABLES \
          -I.src/ev/VARIABLES
LDLIBS = -lcheck -pthread -lm -lsubunit

VMU_SRCS := \
  src/vmu/vmu.c \
  src/vmu/INITIALIZER/vmu_initialization.c \
  src/vmu/VARIABLES/vmu_variables.c \
  src/vmu/SPEED/calculate_speed.c \
  src/vmu/INTERFACE/interface.c \
  src/vmu/QUEUE/check_queue.c \
  src/vmu/CLEANER/vmu_cleanUp.c \
  src/vmu/CONTROLLER/vmu_control_engines.c

VMU_OBJS := $(VMU_SRCS:.c=.o)


EV_SRCS := \
  src/ev/ev.c \
  src/ev/INITIALIZER/ev_initializer.c \
  src/ev/VARIABLES/ev_variables.c \
  src/ev/EVQUEUE/ev_receive.c \
  src/ev/CLEANER/ev_cleanUp.c \
  src/ev/CONTROL_CALCULATE/ev_control_calculate.c

EV_OBJS := $(EV_SRCS:.c=.o)


IEC_SRCS := \
  src/iec/iec.c \
  src/iec/INITIALIZER/iec_initializer.c \
  src/iec/IECQUEUE/iec_receive.c \
  src/iec/CONTROL_CALCULATE/iec_control_calculate.c \
  src/iec/CLEANER/iec_cleanUp.c \
  src/iec/VARIABLES/iec_variables.c

IEC_OBJS := $(IEC_SRCS:.c=.o)

SRC_DIR = ./src
TEST_DIR = ./test
BINSRC = ./src/bin
BINTEST = ./test/bin
COVERAGE_DIR = coverage

MODULES = vmu ev iec
EXECS = $(addprefix $(BINSRC)/, $(MODULES))
TESTS = $(addprefix $(BINTEST)/test_, $(MODULES))

TMUX_SESSION = sistema

.PHONY: all test coverage run show clean kill

# Main target (compilation of executables)
all: $(EXECS)

# Create bin directory for executables
$(BINSRC):
	mkdir -p $@

# Create bin directory for tests
$(BINTEST):
	mkdir -p $@

$(BINSRC)/vmu: $(VMU_OBJS) | $(BINSRC)
	$(CC) $^ -o $@ $(CFLAGS) $(LDLIBS)
	rm -f $(VMU_OBJS)

$(BINSRC)/ev: $(EV_OBJS) | $(BINSRC)
	$(CC) $^ -o $@ $(CFLAGS) $(LDLIBS)
	rm -f $(EV_OBJS)

$(BINSRC)/iec: $(IEC_OBJS) | $(BINSRC)
	$(CC) $^ -o $@ $(CFLAGS) $(LDLIBS)
	rm -f $(IEC_OBJS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS) $(LDLIBS)


# Testes individuais
$(BINTEST)/test_ev: $(TEST_DIR)/ev/test_ev.c $(SRC_DIR)/ev/ev.c | $(BINTEST)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BINTEST)/test_vmu: $(TEST_DIR)/vmu/test_vmu.c $(SRC_DIR)/vmu/vmu.c | $(BINTEST)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BINTEST)/test_iec: $(TEST_DIR)/iec/test_iec.c $(SRC_DIR)/iec/iec.c | $(BINTEST)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

# Compilação e execução dos testes
test: $(TESTS)
	@for test_exec in $(TESTS); do \
		echo "Executando $$test_exec..."; \
		$$test_exec; \
	done

# Coverage generation (report with LCOV + genhtml)
coverage: test
	lcov --capture --directory . --output-file coverage.info --branch-coverage --mcdc-coverage
	genhtml coverage.info --output-directory $(COVERAGE_DIR) --branch-coverage --mcdc-coverage
	

# Running in tmux with split windows
run: all
	
	@tmux new-session -d -s $(TMUX_SESSION) -n vmu './$(BINSRC)/vmu' || { echo "Failed to start tmux session"; exit 1; }
	@tmux split-window -v -t $(TMUX_SESSION):0 './$(BINSRC)/ev' || { echo "Failed to split window for ev"; exit 1; }
	@tmux split-window -h -t $(TMUX_SESSION):0.1 './$(BINSRC)/iec' || { echo "Failed to split window for iec"; exit 1; }
	@tmux select-layout -t $(TMUX_SESSION):0 tiled
	@tmux select-pane -t $(TMUX_SESSION):0.0
	@tmux attach -t $(TMUX_SESSION) || echo "Failed to attach to tmux session"

show:
	xdg-open $(COVERAGE_DIR)/index.html || echo "Failed to open coverage report"

# Clean up (remove binaries and reports)
clean:
	rm -rf $(BINSRC) $(BINTEST) $(COVERAGE_DIR) coverage.info
	find . -name "*.gcda" -type f -delete
	find . -name "*.gcno" -type f -delete

# Stop tmux
kill:
	@tmux kill-session -t $(TMUX_SESSION) || echo "No tmux session to kill"