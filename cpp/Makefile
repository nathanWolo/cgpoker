# cpp/Makefile - evaluator test and micro-benchmarks, each built under two flag sets:
#   build/cg/<prog>      CodinGame's own command line (no -O flag; the sources carry
#                        `#pragma GCC optimize("O3")` + `#pragma GCC target(...)`, enabled by
#                        -DCGMODE where a source makes them conditional)
#   build/native/<prog>  local best case, -O3 -march=native
#
#   make test             exhaustive evaluator correctness test, both flag sets (exit 1 on failure)
#   make run              build and run every benchmark under both flag sets (README.md has numbers)
#   make run-cg | run-native
#   make cmp              pe7c vs OMPEval vs PHEvaluator (run ./fetch_third_party.sh first; optional)
#   make cgsize           minify cgsize.cpp with crossfish's tools/cg_minify.py (CROSSFISH=path)
#   make train-cost       numpy MLP training-step cost (train_cost.py)
#   make clean
CXX := g++
CC := gcc
PYTHON ?= python3
# CodinGame compiles C++ with g++ 11.2 and exactly these flags (crossfish
# documentation/minification.md section 13); it links -lm -lpthread -ldl -lcrypt.
CG_FLAGS := -std=gnu++17 -Werror=return-type -g -pthread
CG_LIBS := -lm -lpthread -ldl -lcrypt
NATIVE_FLAGS := -std=gnu++17 -O3 -march=native -pthread
# trials per class pair for pushfold's 169x169 Monte Carlo matrix (its default)
PUSHFOLD_TRIALS ?= 6000
CROSSFISH ?= ../../crossfish

BUILD := build
HDRS := pe7c.hpp pe7.hpp
# test_eval, bench (pe7c), rvr, mlp, pushfold, ex use pe7c.hpp; bench_ref/bench_direct/prof use
# pe7.hpp.  prof.cpp has no optimize pragma, so it is built natively only.
PROGS := test_eval bench bench_ref bench_direct ex rvr mlp pushfold
CG_BINS := $(addprefix $(BUILD)/cg/,$(PROGS))
NATIVE_BINS := $(addprefix $(BUILD)/native/,$(PROGS) prof)

.PHONY: all cg native test run run-cg run-native cmp cgsize train-cost clean
all: cg native
cg: $(CG_BINS)
native: $(NATIVE_BINS)

$(BUILD)/cg $(BUILD)/native:
	mkdir -p $@

$(BUILD)/cg/%: %.cpp $(HDRS) | $(BUILD)/cg
	$(CXX) $(CG_FLAGS) -DCGMODE -o $@ $< $(CG_LIBS)
$(BUILD)/native/%: %.cpp $(HDRS) | $(BUILD)/native
	$(CXX) $(NATIVE_FLAGS) -o $@ $<

# bench.cpp against the pe7.hpp reference: OMPEval's 2^12-wide hash rows, and the 67 MB direct table
$(BUILD)/cg/bench_ref: bench.cpp $(HDRS) | $(BUILD)/cg
	$(CXX) $(CG_FLAGS) -DCGMODE -DPE_REF -o $@ $< $(CG_LIBS)
$(BUILD)/native/bench_ref: bench.cpp $(HDRS) | $(BUILD)/native
	$(CXX) $(NATIVE_FLAGS) -DPE_REF -o $@ $<
$(BUILD)/cg/bench_direct: bench.cpp $(HDRS) | $(BUILD)/cg
	$(CXX) $(CG_FLAGS) -DCGMODE -DPE_REF -DPE_DIRECT -o $@ $< $(CG_LIBS)
$(BUILD)/native/bench_direct: bench.cpp $(HDRS) | $(BUILD)/native
	$(CXX) $(NATIVE_FLAGS) -DPE_REF -DPE_DIRECT -o $@ $<

test: $(BUILD)/cg/test_eval $(BUILD)/native/test_eval
	@echo "== test_eval, CodinGame flags"; $(BUILD)/cg/test_eval
	@echo "== test_eval, native flags"; $(BUILD)/native/test_eval

# one flag set at a time; each program runs alone so timings do not interfere
define RUN_ALL
	@echo "######## $(1) flags"
	@for p in bench bench_ref bench_direct ex rvr mlp; do echo "== $$p ($(1))"; $(BUILD)/$(1)/$$p || exit 1; done
	@echo "== pushfold $(PUSHFOLD_TRIALS) ($(1))"; $(BUILD)/$(1)/pushfold $(PUSHFOLD_TRIALS)
endef
run-cg: cg
	$(call RUN_ALL,cg)
run-native: native
	$(call RUN_ALL,native)
	@echo "== prof (native)"; $(BUILD)/native/prof
# sequential even under -j, so the two flag sets' timings do not overlap
run:
	$(MAKE) --no-print-directory run-cg
	$(MAKE) --no-print-directory run-native

# ---- optional: comparison with OMPEval and PHEvaluator (sources fetched, never committed) ----
TP := third_party
OMP_SRC := $(TP)/OMPEval/omp/HandEvaluator.cpp
PHE_DIR := $(TP)/PokerHandEvaluator/cpp/src
PHE_SRC := $(addprefix $(PHE_DIR)/,evaluator7.c hash.c hashtable.c hashtable7.c dptables.c tables_bitwise.c)
PHE_OBJ := $(patsubst $(PHE_DIR)/%.c,$(BUILD)/native/phe/%.o,$(PHE_SRC))
$(OMP_SRC) $(PHE_SRC):
	@echo "missing $@: run ./fetch_third_party.sh first"; exit 1
$(BUILD)/native/phe/%.o: $(PHE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) -O3 -march=native -I$(PHE_DIR) -c -o $@ $<
$(BUILD)/native/cmp: cmp.cpp pe7c.hpp $(OMP_SRC) $(PHE_OBJ) | $(BUILD)/native
	$(CXX) $(NATIVE_FLAGS) -I$(TP)/OMPEval -o $@ cmp.cpp $(OMP_SRC) $(PHE_OBJ)
cmp: $(BUILD)/native/cmp
	$(BUILD)/native/cmp

# ---- optional: source size after crossfish's minifier, and the minified file still works ----
$(BUILD)/cgsize_min.cpp: cgsize.cpp pe7c.hpp | $(BUILD)/cg
	@test -f $(CROSSFISH)/tools/cg_minify.py || { echo "set CROSSFISH=<path to crossfish checkout>"; exit 1; }
	$(PYTHON) $(CROSSFISH)/tools/cg_minify.py cgsize.cpp -o $@ --inline-local
cgsize: $(BUILD)/cgsize_min.cpp $(BUILD)/cg/cgsize
	$(CXX) $(CG_FLAGS) -o $(BUILD)/cg/cgsize_min $(BUILD)/cgsize_min.cpp $(CG_LIBS)
	@a=$$($(BUILD)/cg/cgsize); b=$$($(BUILD)/cg/cgsize_min); \
	 echo "minified $$(wc -m < $(BUILD)/cgsize_min.cpp) chars; output $$a (readable) vs $$b (minified)"; \
	 test "$$a" = "$$b"

train-cost:
	$(PYTHON) train_cost.py

clean:
	rm -rf $(BUILD)
