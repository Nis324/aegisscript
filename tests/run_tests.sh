#!/usr/bin/env bash
# ===========================================================================
#  AegisScript regression suite
#
#  Groups:
#    1. positive      - valid machines validate, generate, compile, run
#    2. hierarchy     - nested states: parsing, flattening, runtime semantics
#    2b. parallel    - orthogonal regions: product construction and semantics
#    2c. engine      - action calls lowered to free functions, checked at call sites
#    3. negative      - malformed machines are rejected with the right code
#    4. strategy      - the planner selects by measured rule, not by default
#    5. scale         - a 120-state machine survives the whole pipeline
#    6. equivalence   - generated code behaves identically to hand-written C++
#    7. integration   - the generated machine survives a real game loop
#    8. memory safety - the compiler itself is clean under ASan + UBSan
# ===========================================================================
set -u
cd "$(dirname "$0")/.."

AEGISC=./aegisc
CXX=${CXX:-g++}
OUT=build/testout
PASS=0
FAIL=0

green() { printf "\033[1;32m%s\033[0m" "$1"; }
red()   { printf "\033[1;31m%s\033[0m" "$1"; }
ok()    { PASS=$((PASS+1)); printf "  [%s] %s\n" "$(green PASS)" "$1"; }
bad()   { FAIL=$((FAIL+1)); printf "  [%s] %s\n" "$(red FAIL)" "$1"; }

rm -rf "$OUT"; mkdir -p "$OUT"

# ---------------------------------------------------------------- 1 ------
echo ""
echo "--- positive: validate, generate, compile, run -------------------------"
for f in examples/guard.aegis examples/turret.aegis examples/questline.aegis \
         examples/knight.aegis examples/mech.aegis; do
    name=$(basename "$f" .aegis)
    if ! $AEGISC "$f" -o "$OUT" --emit-demo --no-color > "$OUT/$name.log" 2>&1; then
        bad "$name: compiler rejected a valid machine"; continue
    fi
    ok "$name: validated and generated"

    machine=$(grep -o 'wrote .*\.h' "$OUT/$name.log" | head -1 | sed 's|.*/||; s|\.h$||')
    if $CXX -std=c++17 -O2 -Wall -Wextra -Werror \
            -I"$OUT" "$OUT/$machine.cpp" "$OUT/${machine}_demo.cpp" \
            -o "$OUT/${machine}_demo" 2> "$OUT/$name.cxx.log"; then
        ok "$name: generated C++ compiles clean under -Wall -Wextra -Werror"
    else
        bad "$name: generated C++ failed to compile"
        sed 's/^/        /' "$OUT/$name.cxx.log" | head -10
        continue
    fi

    if "$OUT/${machine}_demo" > "$OUT/$name.run.log" 2>&1; then
        ok "$name: smoke test drove the NPC through a full transition cycle"
    else
        bad "$name: smoke test crashed"
    fi
done

# ---------------------------------------------------------------- 2 ------
echo ""
echo "--- hierarchy: nesting, inheritance, entry/exit ordering ---------------"
if grep -q "hierarchy: 1 composite" "$OUT/knight.log"; then
    ok "knight: flattened 1 composite into leaf states"
else
    bad "knight: hierarchy summary not reported"
fi
if grep -q "8 inherited transition(s), 1 shadowed" "$OUT/knight.log"; then
    ok "knight: 8 transitions inherited from the composite, 1 shadowed by a child"
else
    bad "knight: inheritance counts wrong"
    grep hierarchy "$OUT/knight.log" | sed 's/^/        /'
fi
if $CXX -std=c++17 -O2 -Wall -Wextra -Werror -I"$OUT" \
        tests/hierarchy_semantics.cpp "$OUT/KnightAI.cpp" \
        -o "$OUT/hsem" 2> "$OUT/hsem.log"; then
    if "$OUT/hsem" > "$OUT/hsem.run.log" 2>&1; then
        n=$(grep -c '\[PASS\]' "$OUT/hsem.run.log")
        ok "knight: entry/exit ordering correct ($n semantic assertions)"
    else
        bad "knight: hierarchy semantics wrong"
        grep 'FAIL' "$OUT/hsem.run.log" | sed 's/^/        /'
    fi
else
    bad "knight: semantics test failed to compile"
    sed 's/^/        /' "$OUT/hsem.log" | head -6
fi

# --------------------------------------------------------------- 2b ------
echo ""
echo "--- parallel regions: product construction ----------------------------"
if grep -q "2 region(s) multiplied out into 4 product state(s)" "$OUT/mech.log"; then
    ok "mech: 2 regions of 2 states multiplied out into 4 product states"
else
    bad "mech: product construction counts wrong"
    grep parallel "$OUT/mech.log" | sed 's/^/        /'
fi
if $CXX -std=c++17 -O2 -Wall -Wextra -Werror -I"$OUT" \
        tests/parallel_semantics.cpp "$OUT/MechPilot.cpp" \
        -o "$OUT/psem" 2> "$OUT/psem.log"; then
    if "$OUT/psem" > "$OUT/psem.run.log" 2>&1; then
        n=$(grep -c '\[PASS\]' "$OUT/psem.run.log")
        ok "mech: regions are orthogonal ($n semantic assertions)"
    else
        bad "mech: parallel semantics wrong"
        grep 'FAIL' "$OUT/psem.run.log" | sed 's/^/        /'
    fi
else
    bad "mech: parallel semantics test failed to compile"
    sed 's/^/        /' "$OUT/psem.log" | head -6
fi

# --------------------------------------------------------------- 2c ------
echo ""
echo "--- engine actions: declared, checked, lowered to free calls ----------"
cat > "$OUT/hooks.aegis" <<'AEGIS'
machine Sentry {
    context { int alert = 0; }
    events  { Spot, Lose }
    actions { PlayAnim(int clip); Shout(int volume, float pitch); }
    initial state Idle {
        on enter { alert = 0; PlayAnim(1); }
        on Spot -> Chase;
    }
    state Chase {
        on enter { alert = 2; PlayAnim(2); Shout(80, 1.2); }
        on Lose -> Idle;
    }
}
AEGIS
if $AEGISC "$OUT/hooks.aegis" -o "$OUT/hooks" --no-color > "$OUT/hooks.log" 2>&1; then
    ok "engine actions: machine with call sites validates and generates"
    if grep -q "namespace hooks" "$OUT/hooks/Sentry.h" && \
       grep -q "hooks::PlayAnim(ctx, 1)" "$OUT/hooks/Sentry.cpp"; then
        ok "engine actions: hooks declared in the header, calls lowered in the body"
    else
        bad "engine actions: hook declarations or call sites missing"
    fi
    cat > "$OUT/hooks/main.cpp" <<'CPP'
#include "Sentry.h"
namespace aegis_gen { namespace hooks {
void PlayAnim(SentryContext&, int32_t) {}
void Shout(SentryContext&, int32_t, float) {}
}}
int main() {
    aegis_gen::Sentry s;
    s.dispatch(aegis_gen::SentryEvent::Spot);
    return s.state() == aegis_gen::SentryState::Chase ? 0 : 1;
}
CPP
    if $CXX -std=c++17 -O2 -Wall -Wextra -Werror -I"$OUT/hooks" \
            "$OUT/hooks/main.cpp" "$OUT/hooks/Sentry.cpp" -o "$OUT/hooks/run" \
            2> "$OUT/hooks.cxx.log" && "$OUT/hooks/run"; then
        ok "engine actions: links against user-supplied hooks and runs"
    else
        bad "engine actions: link or run failed"
        sed 's/^/        /' "$OUT/hooks.cxx.log" | head -6
    fi
    # strip comments first: the generated header explains that it uses no
    # virtual dispatch, and that sentence must not trip its own check
    if sed 's|//.*||' "$OUT/hooks/Sentry.h" "$OUT/hooks/Sentry.cpp" \
         | grep -qE '\b(virtual|new |malloc)\b'; then
        bad "engine actions: introduced virtual dispatch or allocation"
    else
        ok "engine actions: no virtual dispatch and no allocation in the output"
    fi
else
    bad "engine actions: compiler rejected a valid machine"
    sed 's/^/        /' "$OUT/hooks.log" | head -6
fi

# ---------------------------------------------------------------- 3 ------
echo ""
echo "--- negative: malformed machines must be rejected ----------------------"
check_neg() {
    local file=$1; shift
    local name; name=$(basename "$file" .aegis)
    local log="$OUT/$name.log"
    $AEGISC "$file" -o "$OUT" --no-color > "$log" 2>&1
    if [ $? -eq 0 ]; then bad "$name: broken machine was accepted"; return; fi
    for code in "$@"; do
        if grep -q "$code" "$log"; then ok "$name: reported $code"
        else bad "$name: expected $code, not reported"; sed 's/^/        /' "$log" | head -6; fi
    done
}

check_neg examples/bad_sinkhole.aegis     AEG-E012
check_neg examples/bad_unreachable.aegis  AEG-E013
check_neg examples/bad_undefined.aegis    AEG-E006 AEG-E007 AEG-E008 AEG-E011
check_neg examples/bad_hierarchy.aegis    AEG-E016
check_neg examples/bad_two_initial.aegis  AEG-E017
check_neg examples/bad_parallel.aegis     AEG-E025
check_neg examples/bad_nested_parallel.aegis AEG-E023

check_neg examples/bad_action.aegis       AEG-E019 AEG-E020 AEG-E021

printf 'machine Broken {\n  initial state A {\n    on X ->> B;\n  }\n}\n' > "$OUT/syntax.aegis"
if $AEGISC "$OUT/syntax.aegis" --check-only --no-color > "$OUT/syntax.log" 2>&1; then
    bad "syntax: malformed arrow was accepted"
else
    ok "syntax: malformed arrow rejected with a line number"
fi

# Panic-mode recovery: four independent faults must all be reported in one
# run. A parser that stops at the first would report only one.
$AEGISC examples/bad_syntax.aegis --check-only --no-color > "$OUT/recover.log" 2>&1
rc=$?
if [ $rc -eq 2 ]; then
    ok "recovery: multi-fault file rejected with exit code 2"
else
    bad "recovery: expected exit 2, got $rc"
fi
n=$(grep -c 'AEG-E000' "$OUT/recover.log")
if [ "$n" -eq 4 ]; then
    ok "recovery: all 4 syntax faults reported in a single pass"
else
    bad "recovery: expected 4 syntax errors, got $n"
    sed 's/^/        /' "$OUT/recover.log" | head -8
fi
if grep -q "near '" "$OUT/recover.log"; then
    ok "recovery: each message names the offending token"
else
    bad "recovery: offending token not reported"
fi

# ---------------------------------------------------------------- 4 ------
echo ""
echo "--- strategy selection (calibrated thresholds) -------------------------"
sel() { $AEGISC "$1" --check-only --metrics --no-color 2>/dev/null | awk '/strategy selected/{print $4}'; }

for f in guard turret questline knight mech; do
    got=$(sel "examples/$f.aegis")
    if [ "$got" = "DENSE_TABLE" ]; then
        ok "$f.aegis fits the cache budget -> DENSE_TABLE"
    else
        bad "$f.aegis should select DENSE_TABLE, got '$got'"
    fi
done

# ---------------------------------------------------------------- 5 ------
echo ""
echo "--- scale: a machine far past hand-written size ------------------------"
python3 tools/gen_stress.py -o "$OUT/crowd.aegis" > "$OUT/gen.log" 2>&1
if [ -f "$OUT/crowd.aegis" ]; then
    ok "generated a 120-state x 112-event stress machine"
    if $AEGISC "$OUT/crowd.aegis" -o "$OUT/stress" --metrics --emit-demo --no-color \
         > "$OUT/crowd.log" 2>&1; then
        ok "stress: 120 states validated and generated"
        got=$(awk '/strategy selected/{print $4}' "$OUT/crowd.log")
        if [ "$got" = "SPARSE_SWITCH" ]; then
            ok "stress: matrix exceeds the cache budget -> SPARSE_SWITCH"
        else
            bad "stress: expected SPARSE_SWITCH, got '$got'"
        fi
        if $CXX -std=c++17 -O2 -Wall -Wextra -Werror -I"$OUT/stress" \
                "$OUT/stress/CrowdDirector.cpp" "$OUT/stress/CrowdDirector_demo.cpp" \
                -o "$OUT/stress/demo" 2> "$OUT/crowd.cxx.log"; then
            ok "stress: 80 kB of generated C++ compiles clean"
        else
            bad "stress: generated C++ failed to compile"
            sed 's/^/        /' "$OUT/crowd.cxx.log" | head -6
        fi
    else
        bad "stress: compiler rejected the generated machine"
    fi
else
    bad "stress: generator failed"
fi

# ---------------------------------------------------------------- 6 ------
echo ""
echo "--- equivalence + throughput vs hand-written C++ -----------------------"
mkdir -p "$OUT/bench"
$AEGISC examples/guard.aegis -o "$OUT/bench" --no-color > /dev/null 2>&1
if $CXX -std=c++17 -O2 -Ibench -I"$OUT/bench" bench/bench.cpp \
        bench/handwritten_guard.cpp bench/naive_guard.cpp "$OUT/bench/GuardAI.cpp" \
        -o "$OUT/bench/bench" 2> "$OUT/bench.log"; then
    if "$OUT/bench/bench" > "$OUT/bench.run.log" 2>&1; then
        ok "generated machine is behaviourally identical to the hand-written one"
        ns=$(awk '/AegisScript generated/{print $4}' "$OUT/bench.run.log")
        hw=$(awk "/State pattern/{print \$6}" "$OUT/bench.run.log")
        ok "measured: ${ns} ns/op, State pattern is ${hw} slower"
    else
        bad "benchmark reported a behavioural divergence"
        grep -E 'DIVERGED|divergence' "$OUT/bench.run.log" | sed 's/^/        /'
    fi
else
    bad "benchmark failed to build"
    sed 's/^/        /' "$OUT/bench.log" | head -6
fi

# ---------------------------------------------------------------- 7 ------
echo ""
echo "--- integration: 512 agents through a game loop ------------------------"
if $CXX -std=c++17 -O2 -I"$OUT/bench" sim/game_loop.cpp "$OUT/bench/GuardAI.cpp" \
        -o "$OUT/sim" 2> "$OUT/sim.log"; then
    if "$OUT/sim" > "$OUT/sim.run.log" 2>&1; then
        pf=$(awk '/per agent-frame/{print $4}' "$OUT/sim.run.log")
        ok "2,048,000 agent-frames: agents moved, died and respawned (${pf} ns each)"
    else
        bad "game loop did not exercise the machine"
        tail -3 "$OUT/sim.run.log" | sed 's/^/        /'
    fi
else
    bad "game loop failed to build"
fi

# ---------------------------------------------------------------- 8 ------
echo ""
echo "--- memory safety (AddressSanitizer + UBSan) ---------------------------"
if $CXX -std=c++17 -g -fsanitize=address,undefined -Isrc -Ibuild \
        build/parser.cpp build/lexer.cpp src/flatten.cpp src/validator.cpp \
        src/layout.cpp src/codegen.cpp src/diagram.cpp src/main.cpp \
        -o "$OUT/aegisc_asan" 2> "$OUT/asan.build.log"; then
    clean=1
    for f in examples/guard.aegis examples/knight.aegis examples/mech.aegis "$OUT/crowd.aegis"; do
        [ -f "$f" ] || continue
        "$OUT/aegisc_asan" "$f" -o "$OUT/asan" --metrics --emit-demo --emit-svg --no-color \
            > "$OUT/asan.log" 2>&1 || clean=0
    done
    if [ $clean -eq 1 ]; then
        ok "compiler clean under ASan + UBSan on flat, nested, parallel and 120-state input"
    else
        bad "ASan/UBSan reported a problem"
        tail -15 "$OUT/asan.log" | sed 's/^/        /'
    fi
else
    printf "  [SKIP] sanitizers unavailable on this toolchain\n"
fi

echo ""
echo "========================================================================"
printf "  %s passed, %s failed\n" "$(green $PASS)" "$([ $FAIL -eq 0 ] && green 0 || red $FAIL)"
echo "========================================================================"
echo ""
[ $FAIL -eq 0 ]
