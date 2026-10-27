# ============================================================================
#  AegisScript - build
#  Requires: flex, bison, a C++17 compiler.
#  macOS:  brew install flex bison   (or use the system flex/bison)
# ============================================================================

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wno-unused-function
FLEX     ?= flex
BISON    ?= bison

# The grammar is written for Bison 2 as well as Bison 3, because macOS still
# ships 2.3 and `%empty` only exists from 3.0. Bison 3 then warns about the
# portable `/* empty */` spelling, so the warning is turned off where the flag
# exists and left alone where it does not.
BISON_MAJOR := $(shell $(BISON) --version 2>/dev/null | head -1 | \
                 sed 's/[^0-9]*\([0-9]*\).*/\1/')
BISONFLAGS  := $(if $(filter 3 4 5,$(BISON_MAJOR)),-Wno-empty-rule,)

SRC   := src
BUILD := build
BIN   := aegisc

GEN_C := $(BUILD)/parser.cpp $(BUILD)/lexer.cpp
OBJS  := $(BUILD)/parser.o $(BUILD)/lexer.o \
         $(BUILD)/flatten.o $(BUILD)/validator.o $(BUILD)/layout.o $(BUILD)/codegen.o \
         $(BUILD)/diagram.o $(BUILD)/main.o

.PHONY: all clean test demo demo-fast diagrams bench sim stress calibrate

all: $(BIN)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/parser.cpp $(BUILD)/parser.hpp: $(SRC)/parser.y | $(BUILD)
	$(BISON) $(BISONFLAGS) -d -o $(BUILD)/parser.cpp $(SRC)/parser.y

$(BUILD)/lexer.cpp: $(SRC)/lexer.l $(BUILD)/parser.hpp | $(BUILD)
	$(FLEX) -o $(BUILD)/lexer.cpp $(SRC)/lexer.l

$(BUILD)/%.o: $(SRC)/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(SRC) -I$(BUILD) -c $< -o $@

$(BUILD)/parser.o:
	$(CXX) $(CXXFLAGS) -I$(SRC) -I$(BUILD) -c $(BUILD)/parser.cpp -o $@

$(BUILD)/lexer.o:
	$(CXX) $(CXXFLAGS) -I$(SRC) -I$(BUILD) -c $(BUILD)/lexer.cpp -o $@

# Every object sees ast.hpp, so a change to the shared data structures rebuilds
# all of them. Listing it per-object was a real bug: parser.o and main.o were
# built against a stale layout while the rest saw the new one, which is an ODR
# violation the linker cannot catch.
HDRS := $(wildcard $(SRC)/*.hpp)

$(BUILD)/flatten.o:   $(SRC)/flatten.cpp   $(HDRS)
$(BUILD)/validator.o: $(SRC)/validator.cpp $(HDRS)
$(BUILD)/layout.o:    $(SRC)/layout.cpp    $(HDRS)
$(BUILD)/codegen.o:   $(SRC)/codegen.cpp   $(HDRS)
$(BUILD)/diagram.o:   $(SRC)/diagram.cpp   $(HDRS)
$(BUILD)/main.o:      $(SRC)/main.cpp      $(HDRS)
$(BUILD)/parser.o:    $(BUILD)/parser.cpp  $(HDRS)
$(BUILD)/lexer.o:     $(BUILD)/lexer.cpp   $(BUILD)/parser.hpp $(HDRS)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(BIN)

test: $(BIN)
	@bash tests/run_tests.sh

demo: $(BIN)
	@bash demo.sh

demo-fast: $(BIN)
	@bash demo.sh --fast

bench: $(BIN)
	@mkdir -p $(BUILD)/bench
	@./$(BIN) examples/guard.aegis -o $(BUILD)/bench --no-color > /dev/null
	@$(CXX) $(CXXFLAGS) -Ibench -I$(BUILD)/bench \
	    bench/bench.cpp bench/handwritten_guard.cpp bench/naive_guard.cpp \
	    $(BUILD)/bench/GuardAI.cpp -o $(BUILD)/bench/bench
	@./$(BUILD)/bench/bench
	@echo ""
	@echo "== static footprint (-O2, .o text+data, lower is better) =="
	@$(CXX) $(CXXFLAGS) -Ibench -c bench/handwritten_guard.cpp -o $(BUILD)/bench/hw.o
	@$(CXX) $(CXXFLAGS) -Ibench -c bench/naive_guard.cpp       -o $(BUILD)/bench/nv.o
	@$(CXX) $(CXXFLAGS) -I$(BUILD)/bench -c $(BUILD)/bench/GuardAI.cpp -o $(BUILD)/bench/ag.o
	@printf "  %-28s %s\n" "A. State pattern (virtual)" "$$(size $(BUILD)/bench/hw.o | awk 'NR==2{print $$1+$$2" bytes"}')"
	@printf "  %-28s %s\n" "B. Naive flat table"        "$$(size $(BUILD)/bench/nv.o | awk 'NR==2{print $$1+$$2" bytes"}')"
	@printf "  %-28s %s\n" "C. AegisScript generated"   "$$(size $(BUILD)/bench/ag.o | awk 'NR==2{print $$1+$$2" bytes"}')"
	@echo ""

sim: $(BIN)
	@mkdir -p $(BUILD)/sim
	@./$(BIN) examples/guard.aegis -o $(BUILD)/sim --no-color > /dev/null
	@$(CXX) $(CXXFLAGS) -I$(BUILD)/sim sim/game_loop.cpp $(BUILD)/sim/GuardAI.cpp -o $(BUILD)/sim/game_loop
	@./$(BUILD)/sim/game_loop

stress: $(BIN)
	@python3 tools/gen_stress.py
	@mkdir -p $(BUILD)/stress
	@./$(BIN) examples/crowd.aegis -o $(BUILD)/stress --metrics --emit-demo --no-color
	@echo "  compiling the generated 120-state machine..."
	@$(CXX) $(CXXFLAGS) -Wextra -Werror -I$(BUILD)/stress \
	    $(BUILD)/stress/CrowdDirector.cpp $(BUILD)/stress/CrowdDirector_demo.cpp \
	    -o $(BUILD)/stress/demo
	@echo "  compiles clean under -Wall -Wextra -Werror"
	@./$(BUILD)/stress/demo | head -3
	@echo "  ..."

calibrate: $(BIN)
	@python3 tools/calibrate.py --quick

diagrams: $(BIN)
	@for f in examples/*.aegis; do ./$(BIN) $$f -o out --emit-svg --emit-dot --no-color >/dev/null 2>&1 || true; done
	@echo "SVG diagrams written to out/ -- open them in a browser"

clean:
	rm -rf $(BUILD) $(BIN) out demo_out
