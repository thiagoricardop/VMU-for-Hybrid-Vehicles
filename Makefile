CC = gcc
CFLAGS = -pthread -lrt -lm -fprofile-arcs -ftest-coverage -fcondition-coverage
CFLAGS += -I. -I./VMU -I./EV -I./IEC
LDLIBS = -lcheck -pthread -lm -lsubunit

SRC_DIR = ./src
BINDIR = bin
COVERAGE_DIR = coverage

TEST_DIRS = EV VMU IEC
EXECS = $(BINDIR)/vmu $(BINDIR)/ev $(BINDIR)/iec
TESTS = $(BINDIR)/test_ev $(BINDIR)/test_vmu $(BINDIR)/test_iec

.PHONY: all run coverage report clean

# Alvo principal
all: $(EXECS)

# Criação do diretório bin
$(BINDIR):
	mkdir -p $(BINDIR)

# Compilação dos executáveis principais (ordem corrigida)
$(BINDIR)/vmu: | $(BINDIR)
	$(CC) $(CFLAGS) src/VMU/main.c -o $(BINDIR)/vmu $(LDLIBS)

$(BINDIR)/ev: | $(BINDIR)
	$(CC) $(CFLAGS) src/EV/main.c -o $(BINDIR)/ev $(LDLIBS)

$(BINDIR)/iec: | $(BINDIR)
	$(CC) $(CFLAGS) src/IEC/main.c -o $(BINDIR)/iec $(LDLIBS)

# Compilação e execução dos testes
test: $(TESTS)
	@for test_exec in $(TESTS); do \
		echo "Executando $$test_exec..."; \
		$$test_exec; \
	done

# Compilação dos testes com ordem corrigida
$(BINDIR)/test_ev: test/EV/test_ev.c $(SRC_DIR)/EV/ev.c | $(BINDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BINDIR)/test_vmu: test/VMU/test_vmu.c $(SRC_DIR)/VMU/vmu.c | $(BINDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BINDIR)/test_iec: test/IEC/test_iec.c $(SRC_DIR)/IEC/iec.c | $(BINDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

# Geração de relatório de cobertura com LCOV + genhtml
coverage: run
	lcov --capture --directory . --output-file coverage.info --branch-coverage --mcdc-coverage
	genhtml coverage.info --output-directory $(COVERAGE_DIR) --branch-coverage --mcdc-coverage
	# xdg-open $(COVERAGE_DIR)/index.html

clean:
	rm -rf $(BINDIR) $(COVERAGE_DIR) coverage.info
