# Top-level reproduction targets. Every target runs from a fresh clone
# (git clone --recurse-submodules ...; pip install -r requirements.txt) and fails on a mismatch.
#
#   make replayer       compile the Java replayer (referee classes from the submodule) into replayer/build/
#   make validate       sim/poker_sim.py and the Java replayer each reproduce all 381 replays (~1 min)
#   make reconstruct    data/cache/decisions.jsonl from all replays, and check that the committed
#                       data/decisions.jsonl.gz (120 games) regenerates with identical content
#   make cache          data/cache/replayed.pkl (analysis/analyze.py, ~1 min)
#   make preeq-check    analysis/preeq.tsv regenerates byte-identically from PreEq.java (~6 s)
#   make solvers-check  eq.c smoke run + every solver script against its documented numbers (~25 s)
#   make eq169-check    rebuild solvers/eq169.bin at 20k trials/pair and compare bytes (~50 s)
#   make cpp-test       exhaustive 7/6/5-card evaluator test, CodinGame and native flags (~1 min incl. build)
#   make engine-check   C++ engine vs poker_sim.py: 3,000 random-action games + all 381 replays (~1 min)
#   make bot            bundle bot/main.cpp -> build/cg/poker_bundled.cpp + poker_min.cpp (the submission), CodinGame flags
#   make arena          build/arena/arena (dev + frozen prev + scripted opponents), then a 200-game smoke run
#   make pf-tables      regenerate bot/pf_tables.hpp from solvers/pf.py (~20 s)
#   make freeze         bot/bot_prev.hpp := bot/bot.hpp (arena/freeze.py)
#   make arena-refs     frozen M1, M2.1, M2.3, OM1 from their commits -> build/arena/frozen/ (arena --variants)
#   make clones         clones of the live bots: replays -> data/cache/clone_decisions.tsv -> data/clones/clones.txt
#   make all            all of the above except eq169-check
#   make full           all + eq169-check
#   make clean          remove build outputs and data/cache/ (never touches committed data)
SHELL := /bin/bash
PYTHON ?= python3
JOBS ?= 8
CACHE := data/cache

.PHONY: all full submodule pfn-tables hu-tables bot-test replayer validate validate-sim validate-java reconstruct cache preeq-check \
        solvers-check eq169-check cpp-test engine-check bot arena arena-refs clones pf-tables freeze clean

all: replayer validate reconstruct cache preeq-check solvers-check cpp-test engine-check bot-test bot arena
full: all eq169-check

submodule:
	@test -f third_party/CodingamePoker/CodingamePoker/src/main/java/com/codingame/model/object/board/Board.java \
	  || git submodule update --init third_party/CodingamePoker

replayer: submodule
	replayer/build.sh

validate: validate-sim validate-java
validate-sim:
	$(PYTHON) sim/validate_replays.py
validate-java: replayer
	$(PYTHON) replayer/replay_game.py --check -j $(JOBS)

reconstruct:
	$(PYTHON) sim/reconstruct.py
	$(PYTHON) sim/reconstruct.py --ids tools/sim_games.txt -o $(CACHE)/decisions120.jsonl
	cmp $(CACHE)/decisions120.jsonl <(gzip -dc data/decisions.jsonl.gz) \
	  && echo "data/decisions.jsonl.gz: content reproduced exactly (22,916 records)"

cache: replayer
	$(PYTHON) analysis/analyze.py

preeq-check: replayer
	@mkdir -p $(CACHE)
	java -cp replayer/build com.codingame.game.PreEq 40000 > $(CACHE)/preeq.tsv
	cmp $(CACHE)/preeq.tsv analysis/preeq.tsv && echo "analysis/preeq.tsv reproduced byte-identically"

solvers-check:
	$(PYTHON) solvers/check.py
eq169-check:
	$(PYTHON) solvers/check.py --full

engine-check:
	$(PYTHON) engine/check.py all --games 3000 --seed 7

bot:
	$(PYTHON) tools/bundle.py --minify

arena:
	@mkdir -p build/arena
	g++ -std=gnu++17 -O3 -march=native -pthread -o build/arena/arena arena/arena.cpp
	build/arena/arena --games 200 --threads 4 --trials 5000 --opp station,jammer,random,folder

arena-refs:
	$(PYTHON) arena/freeze.py --commit 40d4864 --ns m1
	$(PYTHON) arena/freeze.py --commit b9b662f --ns m21
	$(PYTHON) arena/freeze.py --commit 65fca41 --ns m23
	$(PYTHON) arena/freeze.py --commit 9187617 --ns om1

clones:
	$(PYTHON) analysis/clone_fit.py data
	$(PYTHON) analysis/clone_fit.py fit
	for r in m1 m21 om1; do $(PYTHON) analysis/clone_fit.py schedule tools/$${r}_games.txt > data/clones/sched_$$r.txt; done

pf-tables:
	$(PYTHON) solvers/export_pf.py

# 3-4 player ICM push/fold charts: equity/card-removal tables (1 min), the solver's HU check, the stack grids
# (about 1.5 h on 4 cores, see solvers/pfn/run_grids.sh) and the distilled header bot/pfn_tables.hpp
pfn-tables:
	@mkdir -p build/pfn data/cache/pfn
	g++ -std=gnu++20 -O3 -march=native -pthread -o build/pfn/tables solvers/pfn/tables.cpp
	g++ -std=gnu++20 -O3 -march=native -pthread -o build/pfn/pfn solvers/pfn/pfn.cpp
	test -s data/cache/pfn/eq3.bin || build/pfn/tables 2000 data/cache/pfn
	build/pfn/pfn test
	sh solvers/pfn/run_grids.sh
	$(PYTHON) solvers/pfn/distil.py

# heads-up 8-120 BB strategy: the 2M-deal pool (30 s), nine stack points by MCCFR (about 20 min on 4 cores),
# the exported header bot/hu_tables.hpp
hu-tables:
	@mkdir -p build/hu data/cache/hu data/hu
	g++ -std=gnu++20 -O3 -march=native -pthread -o build/hu/hu solvers/hu/hu.cpp
	test -s data/cache/hu/pool.bin || build/hu/hu pool 2000000 1 data/cache/hu/pool.bin
	build/hu/hu grid 8,10,12,15,20,30,40,60,120 400000000 data/hu/grid9.bin data/cache/hu/pool.bin
	$(PYTHON) solvers/hu/export_hu.py --grid data/hu/grid9.bin --max-stack 20

bot-test:
	@mkdir -p build/bot
	g++ -std=gnu++20 -O2 -o build/bot/test_bot bot/test_bot.cpp
	build/bot/test_bot

freeze:
	$(PYTHON) arena/freeze.py

cpp-test:
	$(MAKE) -C cpp test

clean:
	rm -rf build replayer/build $(CACHE)
	$(MAKE) -C cpp clean

