# AegisScript

**A verification-driven DSL and synthesis toolchain for game-AI finite state machines.**

Nishant Kumar (24BCE2192) · Vellore Institute of Technology
Complete — frontend, hierarchy and parallel flattening, semantic validator,
calibrated synthesis planner, C++ backend with engine actions, benchmarks,
integration tests and the project report.

---

## What it does

You write the NPC's behaviour as a state machine:

```
machine GuardAI {
    context { float stamina = 100.0; bool hasWeapon = true; }
    events  { PlayerSpotted, PlayerLost, Attacked, Killed }
    actions { PlayAnim(int clip); Shout(int volume, float pitch); }

    initial state Patrol {
        on enter  { alertLevel = 0; PlayAnim(2); }
        on update { stamina = stamina + 0.5; }
        on PlayerSpotted -> Chase when stamina > 20.0;
        on Attacked      -> Combat;
    }
    ...
    final state Dead { }
}
```

Three composition features, all resolved by the compiler rather than the
runtime:

- **Nesting.** A transition declared once on a composite state is inherited by
  every substate, and a substate's own transition on the same event shadows it
  (`examples/knight.aegis`).
- **Parallel regions.** A `parallel` state holds orthogonal regions and the
  agent is in one leaf of each at once; the compiler multiplies the regions out
  into ordinary product states (`examples/mech.aegis`).
- **Engine actions.** Declared in an `actions { }` block, checked for arity and
  argument types at every call site, and lowered to plain free functions the
  host engine supplies — no virtual dispatch, no allocation.

`aegisc` then:

1. **tokenises and parses** it (Flex + Bison) into a state tree;
2. **flattens** the tree into leaf states, resolving inherited and shadowed
   transitions, multiplying out parallel regions, and computing the entry/exit
   chains;
3. **proves it correct** — type-checks guards, actions and engine call sites,
   and runs graph
   analysis over the transition digraph to catch unreachable states,
   sinkholes, non-determinism, dangling targets and dead events **before a
   single line of C++ exists**;
4. **plans the synthesis** — measures the shape of the verified graph and
   picks the dispatch topology and memory layout that suit it;
5. **emits** standalone, engine-agnostic `.h` / `.cpp` with no heap
   allocation, no virtual dispatch and a compile-time-fixed footprint.

Step 4 is the part that is not in the existing tools: the output of
verification is *reused as an input to code synthesis*, rather than only being
reported to the developer. Its thresholds are measured, not assumed — see
[docs/CALIBRATION.md](docs/CALIBRATION.md).

---

## Build

Needs `flex`, `bison` and a C++17 compiler.

```bash
brew install flex bison     # macOS
make                        # builds ./aegisc
make test                   # 51-check regression suite
make demo                   # paced live demonstration (see DEMO.md)
make bench                  # measured against hand-written C++
make sim                    # 512 agents through a game loop
make stress                 # generate and compile a 120-state machine
make calibrate              # re-run the threshold sweep
make diagrams               # render every example's transition graph to SVG
```

## Use

```bash
./aegisc examples/guard.aegis -o out --metrics --emit-demo
```

| flag | effect |
|---|---|
| `-o <dir>` | output directory (default `./out`) |
| `--check-only` | validate, emit no code |
| `--metrics` | print the synthesis report |
| `--emit-demo` | also write `<Machine>_demo.cpp` smoke test |
| `--emit-svg` | write `<Machine>.svg` — the transition graph, opens in any browser |
| `--emit-dot` | write `<Machine>.dot` — Graphviz source |
| `--force-strategy=<s>` | override the selector (`dense`/`sparse`/`linear`) — calibration only |
| `--no-color` | plain output |

Exit codes: `0` ok · `2` syntax error · `3` semantic error · `4` I/O error.

---

## Measured against hand-written C++

`make bench` checks behavioural equivalence first, then times all three over an
identical event stream:

```
== behavioural equivalence ==
  state pattern  vs AegisScript   : identical
  4096 events, traces match exactly

== throughput (ns per update+dispatch, lower is better) ==
  implementation                   ns/op   vs aegis
  A. State pattern (virtual)        5.02      1.50x
  B. Naive flat table              16.01      4.77x
  C. AegisScript generated          3.36      1.00x

== static footprint (-O2, .o text+data) ==
  A. State pattern (virtual)   3432 bytes
  B. Naive flat table          1228 bytes
  C. AegisScript generated     1965 bytes
```

Three honest notes on these numbers. The modelled "19 probes → 1" is **not** a
19× speed-up: on real hardware, caching and branch prediction absorb most of a
short comparison chain, and the measured gap against the naive table is 4.8×.
The naive table has a *smaller* static footprint than the dense matrix — the
matrix trades ~740 bytes for that 4.8×. And the third significant figure moves
between runs: across repeated invocations the generated code stays within
3.35–3.38 ns, the State pattern within 4.88–5.12 ns and the naive table within
15.6–16.5 ns. The ordering is stable; the exact decimals are not.

## Strategy selection

```
  strategy selected   : DENSE_TABLE
    - matrix needs 72 B, within the measured 24576 B cache budget
    - occupancy (53%) is not part of this decision: calibration found the
      matrix wins at every occupancy measured, down to 2%
```

| strategy | selected when | dispatch cost |
|---|---|---|
| `DENSE_TABLE` | dense matrix ≤ 24 kB | **1 indexed load** |
| `SPARSE_SWITCH` | otherwise | ≤ out-degree |
| `LINEAR_SCAN` | never — measured and rejected | ≤ out-degree |

The thresholds came out of `tools/calibrate.py`, which sweeps 2×2 to 128×128
machines, forces each strategy, and times the result. It overturned two of the
three original constants and removed a strategy. Details and tables in
[docs/CALIBRATION.md](docs/CALIBRATION.md).

## Diagnostics implemented

| code | meaning |
|---|---|
| AEG-E000 | syntax error, with the offending token named (panic-mode recovery) |
| AEG-E001/E002 | no initial state / more than one initial state |
| AEG-E003/E004/E005 | duplicate state / event / context variable |
| AEG-E006 | transition targets an undefined state |
| AEG-E007 | state reacts to an undeclared event |
| AEG-E008 | undefined identifier in a guard |
| AEG-E009 | type error in a guard, action or initialiser |
| AEG-E010 | guard expression is not boolean |
| AEG-E011 | non-determinism: two unconditional transitions on one event |
| AEG-E012 | **sinkhole** — reachable, no way out, not final |
| AEG-E013 | **unreachable state** |
| AEG-E014 | assignment to an undefined context variable |
| AEG-E015 | machine declares no states |
| AEG-W001 | event declared but never handled |
| AEG-W002 | every transition on an event is guarded — the event can be dropped |
| AEG-W003 | context variable written but never read |
| AEG-W004 | no path from this state to any final state |
| AEG-E016 | composite state has no `initial` child |
| AEG-E017 | composite state declares more than one `initial` child |
| AEG-E018 | duplicate engine action, or one colliding with a context variable |
| AEG-E019 | call to an engine action not declared in the `actions` block |
| AEG-E020 | engine action called with the wrong number of arguments |
| AEG-E021 | engine action argument has the wrong type |
| AEG-E022 | `parallel` state declares no regions |
| AEG-E023 | nested `parallel` composites (not supported) |
| AEG-E024 | state is both `initial` and `final` |
| AEG-E025 | one event handled by two regions of the same parallel state |
| AEG-W005 | composite state has a single child — nesting without a choice |
| AEG-W006 | composite state has `on update`, which only leaf states run |
| AEG-W007 | engine action declared but never called |
| AEG-W008 | `parallel` state has one region — it behaves as an ordinary composite |
| AEG-W009 | product construction multiplied the state space past 64 states |

35 diagnostics in total (AEG-E000, AEG-E001…E025, AEG-W001…W009), each carrying a
`file:line: severity [CODE]` source location.

---

## Layout

```
src/
  lexer.l        Flex lexical specification             (stage 1a)
  parser.y       Bison grammar, nested and parallel     (stage 1b)
  ast.hpp        state tree, flat machine, arena
  flatten.*      hierarchy and parallel -> leaf machine (stage 2)
  validator.*    type checking + graph analysis         (stage 3)
  layout.*       synthesis strategy selector            (stage 4)  <- core contribution
  codegen.*      C++ backend                            (stage 5)
  diagram.*      SVG / DOT transition graphs
  main.cpp       aegisc driver
bench/           hand-written baselines + benchmark harness
sim/             512-agent game-loop integration test
tools/           calibrate.py (threshold sweep), gen_stress.py
examples/        5 valid machines (nested, parallel, actions), 8 deliberately broken
tests/           regression suite + hierarchy and parallel assertions
docs/            CALIBRATION.md, LANGUAGE.md, CLAIMS.md, VIVA_PREP.md
```

2,966 lines of compiler (C++, Flex, Bison), plus 1,926 lines of tests,
benchmarks and tooling.

## Demonstration

`./demo.sh` runs a paced walkthrough — the DSL, rejected machines, the
synthesis report, the generated C++, the NPC running, and the test suite.
`DEMO.md` is the runbook: what to say at each step, expected questions, and
what to do if something breaks live.

The `--emit-svg` flag draws the transition graph the validator reasoned about,
with unreachable states in red and sinkholes in amber — useful in the demo and
in the report.

## Test status

54/54 checks passing:

| group | what it establishes |
|---|---|
| positive | 5 machines validate, generate, compile under `-Werror`, run |
| hierarchy | inheritance counts, shadowing, and 18 entry/exit ordering assertions |
| parallel | 16 orthogonality assertions — one region moves, the others do not |
| engine actions | declared signatures reach the generated header and link |
| negative | 7 broken machines rejected with the expected diagnostic code |
| strategy | the planner selects by the calibrated rule, not by default |
| scale | a 120-state × 112-event machine survives the whole pipeline |
| equivalence | generated code is behaviourally identical to hand-written C++ |
| integration | 2,048,000 agent-frames, 14.5 ns each |
| memory safety | clean under ASan + UBSan on flat, nested, parallel and 120-state input |
