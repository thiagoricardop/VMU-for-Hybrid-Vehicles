# — Compiler and flags —
CC           := gcc
CFLAGS       := -pthread -lrt -lm --coverage \
                 -I. -Isrc/vmu -Isrc/ev -Isrc/iec
CFLAGS_TEST  := $(CFLAGS) -DTESTING
LDLIBS       := -lcheck -pthread -lm -lsubunit

# — Directories —
SRC_DIR      := src
TEST_DIR     := test
BINSRC       := $(SRC_DIR)/bin
BINTEST      := $(TEST_DIR)/bin
COVERAGE_DIR := coverage

MODULES      := vmu ev iec
EXECS        := $(addprefix $(BINSRC)/, $(MODULES))
TESTS        := $(BINTEST)/test_ev $(BINTEST)/test_vmu $(BINTEST)/test_iec

TMUX_SESSION := sistema

# — Source files by module —
VMU_ALL_SRCS    := $(wildcard src/vmu/*.c)
VMU_SRCS        := $(filter-out src/vmu/main.c, $(VMU_ALL_SRCS))
VMU_OBJS        := $(VMU_SRCS:.c=.o)
VMU_MAIN_OBJ    := src/vmu/main.o
VMU_APP_OBJS    := $(VMU_OBJS) $(VMU_MAIN_OBJ)

EV_ALL_SRCS     := $(shell find src/ev -name '*.c')
EV_SRCS         := $(filter-out %/main.c, $(EV_ALL_SRCS))
EV_OBJS         := $(EV_SRCS:.c=.o)
EV_MAIN_OBJ     := $(filter %/main.o, $(EV_ALL_SRCS:.c=.o))
EV_APP_OBJS     := $(EV_OBJS) $(EV_MAIN_OBJ)

IEC_ALL_SRCS    := $(wildcard src/iec/*.c)
IEC_SRCS        := $(filter-out src/iec/main.c, $(IEC_ALL_SRCS))
IEC_OBJS        := $(IEC_SRCS:.c=.o)
IEC_MAIN_OBJ    := src/iec/main.o
IEC_APP_OBJS    := $(IEC_OBJS) $(IEC_MAIN_OBJ)

# — Test object lists —
VMU_TEST_OBJS := $(VMU_OBJS)
EV_TEST_OBJS  := $(EV_OBJS)
IEC_TEST_OBJS := $(IEC_OBJS)

# — Phony targets —
.PHONY: all test coverage run show clean kill
.SECONDARY: $(VMU_OBJS) $(EV_OBJS) $(IEC_OBJS) src/vmu/main.o src/ev/main.o src/iec/main.o

# — Default: build applications —
all: $(EXECS)

# Create bin directories
$(BINSRC):
	mkdir -p $@
$(BINTEST):
	mkdir -p $@

# — Link applications —
$(BINSRC)/vmu: $(VMU_APP_OBJS) | $(BINSRC)
	$(CC) $^ -o $@ $(CFLAGS) $(LDLIBS)

$(BINSRC)/ev: $(EV_APP_OBJS) | $(BINSRC)
	$(CC) $^ -o $@ $(CFLAGS) $(LDLIBS)

$(BINSRC)/iec: $(IEC_APP_OBJS) | $(BINSRC)
	$(CC) $^ -o $@ $(CFLAGS) $(LDLIBS)

# — Generic compile rule —
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# — Test object compilation —
$(TEST_DIR)/%/test_%.o: $(TEST_DIR)/%/test_%.c | $(BINTEST)
	$(CC) $(CFLAGS_TEST) -c $< -o $@

# — Test executables linking —
$(BINTEST)/test_ev:  $(TEST_DIR)/ev/test_ev.o  $(EV_TEST_OBJS)  | $(BINTEST)
	$(CC) $^ -o $@ $(CFLAGS_TEST) $(LDLIBS)

$(BINTEST)/test_vmu: $(TEST_DIR)/vmu/test_vmu.o $(VMU_TEST_OBJS) | $(BINTEST)
	$(CC) $^ -o $@ $(CFLAGS_TEST) $(LDLIBS)

$(BINTEST)/test_iec: $(TEST_DIR)/iec/test_iec.o $(IEC_TEST_OBJS) | $(BINTEST)
	$(CC) $^ -o $@ $(CFLAGS_TEST) $(LDLIBS)

# — Run tests —
test: $(TESTS)
	@for t in $^; do echo "Executando $$t..."; $$t; done

# — Coverage report —
coverage: test
	lcov --capture --directory . --output-file coverage.info --branch-coverage --mcdc-coverage
	genhtml coverage.info --output-directory $(COVERAGE_DIR) --branch-coverage --mcdc-coverage

# — Run in tmux —
run: all
	@tmux new-session -d -s $(TMUX_SESSION) -n vmu './$(BINSRC)/vmu' || { echo "Failed to start tmux session"; exit 1; }
	@tmux split-window -v -t $(TMUX_SESSION):0 './$(BINSRC)/ev' || { echo "Failed to split window for ev"; exit 1; }
	@tmux split-window -h -t $(TMUX_SESSION):0.1 './$(BINSRC)/iec' || { echo "Failed to split window for iec"; exit 1; }
	@tmux select-layout -t $(TMUX_SESSION):0 tiled
	@tmux select-pane -t $(TMUX_SESSION):0.0
	@tmux attach -t $(TMUX_SESSION) || echo "Failed to attach to tmux session"

# — Open coverage report —
show:
	xdg-open $(COVERAGE_DIR)/index.html || echo "Failed to open coverage report"

# — Clean artifacts —
clean:
	rm -rf $(BINSRC) $(BINTEST) $(COVERAGE_DIR) coverage.info
	find . -name "*.gcda" -type f -delete
	find . -name "*.gcno" -type f -delete
	find . -name "*.o" -type f -delete

# — Kill tmux session —
kill:
	@tmux kill-session -t $(TMUX_SESSION) || echo "No tmux session to kill"

