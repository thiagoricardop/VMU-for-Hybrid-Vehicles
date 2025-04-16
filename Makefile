CC = gcc
CFLAGS = -pthread -lrt -lm -fprofile-arcs -ftest-coverage -fcondition-coverage
CFLAGS += -I. -I./vmu -I./ev -I./iec
LDLIBS = -lcheck -pthread -lm -lsubunit

SRC_DIR = ./src
TEST_DIR = ./test
BINDIR = bin
COVERAGE_DIR = coverage

MODULES = vmu ev iec
EXECS = $(addprefix $(BINDIR)/, $(MODULES))
TESTS = $(addprefix $(BINDIR)/test_, $(MODULES))

TMUX_SESSION = meu_sistema

.PHONY: all test coverage run clean kill

# Main target (compilation of executables)
all: $(EXECS)

# Create bin directory
$(BINDIR):
	mkdir -p $@

# Pattern rule for main executables
$(BINDIR)/%: $(SRC_DIR)/%/main.c | $(BINDIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

# Testes individuais
$(BINDIR)/test_ev: $(TEST_DIR)/ev/test_ev.c $(SRC_DIR)/ev/ev.c | $(BINDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BINDIR)/test_vmu: $(TEST_DIR)/vmu/test_vmu.c $(SRC_DIR)/vmu/vmu.c | $(BINDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BINDIR)/test_iec: $(TEST_DIR)/iec/test_iec.c $(SRC_DIR)/iec/iec.c | $(BINDIR)
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
	xdg-open $(COVERAGE_DIR)/index.html || echo "Failed to open coverage report"

# Running in tmux with split windows
run: all
	@tmux new-session -d -s $(TMUX_SESSION) -n main './$(BINDIR)/vmu' || { echo "Failed to start tmux session"; exit 1; }
	@tmux split-window -v -t $(TMUX_SESSION):0 './$(BINDIR)/ev' || { echo "Failed to split window for ev"; exit 1; }
	@tmux split-window -h -t $(TMUX_SESSION):0.1 './$(BINDIR)/iec' || { echo "Failed to split window for iec"; exit 1; }
	@tmux select-layout -t $(TMUX_SESSION):0 tiled
	@tmux select-pane -t $(TMUX_SESSION):0.0
	@tmux attach -t $(TMUX_SESSION) || echo "Failed to attach to tmux session"

# Clean up (remove binaries and reports)
clean:
	rm -rf $(BINDIR) $(COVERAGE_DIR) coverage.info

# Stop tmux
kill:
	@tmux kill-session -t $(TMUX_SESSION) || echo "No tmux session to kill"