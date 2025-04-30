# — Diretórios —
SRC_DIR      := src
TEST_DIR     := test
BINSRC       := bin
BINTEST := $(BINSRC)/test
COVERAGE_DIR := coverage

# — Compilador e flags —
CC           := gcc

# Flags de cobertura: gera .gcno ao lado de .o, emite .gcda em BINSRC
CFLAGS := -pthread -lrt -lm --coverage \
          -I. -Isrc/vmu -Isrc/ev -Isrc/iec \
          -fprofile-arcs \
          -ftest-coverage \
          -fprofile-dir=$(BINSRC)

CFLAGS_TEST  := $(CFLAGS) -DTESTING 
LDLIBS       := -lcheck -pthread -lm -lsubunit

MODULES      := vmu ev iec
EXECS        := $(addprefix $(BINSRC)/, $(MODULES))

TMUX_SESSION := sistema

# — Arquivos fonte e objetos para cada módulo —
VMU_SRCS := $(filter-out src/vmu/main.c,$(wildcard src/vmu/*.c))
VMU_OBJS := $(patsubst src/vmu/%.c,$(BINSRC)/%.o,$(VMU_SRCS))
VMU_MAIN := $(BINSRC)/main_vmu.o
VMU_APP  := $(VMU_OBJS) $(VMU_MAIN)

EV_SRCS := $(filter-out src/ev/main.c,$(wildcard src/ev/*.c))
EV_OBJS := $(patsubst src/ev/%.c,$(BINSRC)/%.o,$(EV_SRCS))
EV_MAIN := $(BINSRC)/main_ev.o
EV_APP  := $(EV_OBJS) $(EV_MAIN)

IEC_SRCS := $(filter-out src/iec/main.c,$(wildcard src/iec/*.c))
IEC_OBJS := $(patsubst src/iec/%.c,$(BINSRC)/%.o,$(IEC_SRCS))
IEC_MAIN := $(BINSRC)/main_iec.o
IEC_APP  := $(IEC_OBJS) $(IEC_MAIN)

TEST_SRCS := $(wildcard $(TEST_DIR)/vmu/test_vmu.c \
                       $(TEST_DIR)/ev/test_ev.c \
                       $(TEST_DIR)/iec/test_iec.c)

# Objetos de teste: bin/test_vmu.o, bin/test_ev.o, bin/test_iec.o
TEST_OBJS := $(patsubst $(TEST_DIR)/%/test_%.c,$(BINSRC)/test_%.o,$(TEST_SRCS))

TESTS := $(BINTEST)/test_vmu $(BINTEST)/test_ev $(BINTEST)/test_iec

# — Alvos fictícios e secundários —
.PHONY: all test coverage run show clean kill
.SECONDARY: $(BINSRC)/%.o

# — Alvo padrão: compila aplicações —
all: $(EXECS)

# — Cria diretório bin se necessário —
$(BINSRC):
	mkdir -p $@

$(BINTEST): | $(BINSRC)
	mkdir -p $@

# — Regras de compilação para cada módulo —
$(BINSRC)/%.o: src/vmu/%.c | $(BINSRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINSRC)/%.o: src/ev/%.c | $(BINSRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINSRC)/%.o: src/iec/%.c | $(BINSRC)
	$(CC) $(CFLAGS) -c $< -o $@

# — Compila main de cada módulo —
$(BINSRC)/main_%.o: src/%/main.c | $(BINSRC)
	$(CC) $(CFLAGS) -c $< -o $@

# — Linka executáveis —
$(BINSRC)/vmu: $(VMU_APP)
	$(CC) $^ -o $@ $(CFLAGS) $(LDLIBS)

$(BINSRC)/ev: $(EV_APP)
	$(CC) $^ -o $@ $(CFLAGS) $(LDLIBS)

$(BINSRC)/iec: $(IEC_APP)
	$(CC) $^ -o $@ $(CFLAGS) $(LDLIBS)

# 1) Regra explícita para cada objeto de teste
$(BINSRC)/test_vmu.o: $(TEST_DIR)/vmu/test_vmu.c | $(BINSRC)
	$(CC) $(CFLAGS_TEST) -c $< -o $@

$(BINSRC)/test_ev.o: $(TEST_DIR)/ev/test_ev.c | $(BINSRC)
	$(CC) $(CFLAGS_TEST) -c $< -o $@

$(BINSRC)/test_iec.o: $(TEST_DIR)/iec/test_iec.c | $(BINSRC)
	$(CC) $(CFLAGS_TEST) -c $< -o $@

# 2) Link: gera bin/test/test_vmu, bin/test/test_ev, bin/test/test_iec
#    Usa cada objeto de teste + objetos já compilados da aplicação
$(BINTEST)/test_vmu: $(BINSRC)/test_vmu.o $(VMU_OBJS) | $(BINTEST)
	$(CC) $^ -o $@ $(CFLAGS_TEST) $(LDLIBS)

$(BINTEST)/test_ev:  $(BINSRC)/test_ev.o  $(EV_OBJS) | $(BINTEST)
	$(CC) $^ -o $@ $(CFLAGS_TEST) $(LDLIBS)

$(BINTEST)/test_iec: $(BINSRC)/test_iec.o $(IEC_OBJS) | $(BINTEST)
	$(CC) $^ -o $@ $(CFLAGS_TEST) $(LDLIBS)

# — Executa testes —
test: $(TESTS)
	@echo ">>> Entrou no target test <<<"
	@for t in $^; do \
		echo "Executando $$t…"; \
		./$$t; \
	done

# — Relatório de cobertura —
coverage: test
	lcov --capture --directory $(BINSRC) --output-file coverage.info --branch-coverage
	genhtml coverage.info --output-directory $(COVERAGE_DIR) --branch-coverage

# — Executa em tmux —
run: all
	@tmux new-session -d -s $(TMUX_SESSION) -n vmu './$(BINSRC)/vmu' \
		&& tmux split-window -v './$(BINSRC)/ev' \
		&& tmux split-window -h './$(BINSRC)/iec' \
		&& tmux select-layout tiled \
		&& tmux attach

# — Abre relatório de cobertura —
show:
	xdg-open $(COVERAGE_DIR)/index.html || echo "Falha ao abrir o relatório de cobertura"

# — Limpa artefatos —
clean:
	rm -rf $(BINSRC) $(BINTEST) $(COVERAGE_DIR) coverage.info
	find . -name "*.gcda" -delete
	find . -name "*.gcno" -delete
	find . -name "*.o" -delete

# — Encerra sessão tmux —
kill:
	@tmux kill-session -t $(TMUX_SESSION) || echo "Nenhuma sessão tmux para encerrar"
