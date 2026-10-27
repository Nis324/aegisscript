# Demonstration runbook

Nishant Kumar · 24BCE2192 · AegisScript · project review

---

## Before you walk in

```bash
cd aegisscript
make            # builds ./aegisc
make test       # 54/54 should pass
./demo.sh --fast   # dry run, no pauses — confirm nothing errors
```

If `make` fails, it is almost always flex or bison missing:

```bash
brew install flex bison      # macOS
sudo apt install flex bison  # Linux
```

Then open a terminal at a readable font size, `cd` into the project, and clear the screen.

**Have these open in browser tabs before you start**, so you are not fumbling mid-demo:

- `demo_out/GuardAI.svg` — the verified graph
- `demo_out/BrokenGuard.svg` — the sinkhole, drawn in amber
- `demo_out/Orphan.svg` — the unreachable state, drawn in red

(Run `./demo.sh --fast` once beforehand and those files will exist.)

---

## Running it

```bash
./demo.sh
```

Thirteen steps, pauses for Enter between each. Budget **14–16 minutes**.
`./demo.sh --short` skips the benchmark and game loop if you only have five. `./demo.sh --auto` runs
straight through if you are short on time and want to narrate over it.

---

## What to say at each step

### 1 — The input (30s)

> "This is the entire guard AI. Sixty lines. Everything you see is behaviour —
> there's no dispatch machinery, no state classes, no transition table. That's
> the part the transpiler writes."

Don't read the code aloud. Let them scan it and move on.

### 2 — The sinkhole (90s) — **this is the most important step**

> "Look at `Stunned`. The NPC can get into it, and there is no transition out.
> It's not a designed end state — it's a mistake. Now: if I'd written this by
> hand in C++, it would compile. Clean build, no warnings. The bug shows up
> weeks later when a tester walks an NPC into that state and it freezes."

Run it. Then:

> "AegisScript refuses. Named the state, gave the line, told me what would
> happen at runtime, and generated nothing."

Switch to the `BrokenGuard.svg` tab:

> "And it drew me the graph with the problem marked."

**Don't rush this step.** Everything else in the project is in service of it.

### 3 — Unreachable state (45s)

> "Same analysis, different property. Breadth-first search from the initial
> state — anything the search doesn't reach gets reported by name. `Berserk`
> has no inbound edge, so it's dead code that still has to be maintained."

Show `Orphan.svg`.

### 4 — Valid machine + synthesis report (90s)

> "Now a machine that passes. Notice the verification didn't just say yes —
> it *measured* the graph. Six states, six events, 19 transitions, so 53% of
> the state-event space is occupied. That measurement then chose how the
> machine gets built: dense enough to be worth a contiguous matrix, and small
> enough to fit in cache. Worst-case dispatch drops from 19 probes to 1."

This is the part that distinguishes the project from existing FSM generators.
If you only get one sentence across, make it: **the verification result is an
input to code generation, not just a diagnostic.**

### 5 — Generated C++ (60s)

> "Here's the table it decided on, and here's the dispatch. One indexed load,
> no comparison chain. Every transition record is commented with its source
> state, event and target, so this is readable code, not a black box."

### 6 — It compiles and runs (60s)

> "`-Wall -Wextra -Werror`, clean. And here's the NPC actually walking the
> graph — Patrol, Chase, Search, Combat, Rest, back to Patrol, then killed.
> After `Dead` every event is ignored, which is what a final state should do."

### 7 — Hierarchical states (2 min)

> "Combat is a composite with three phases. `Killed` and `Staggered` are
> declared once, on Combat, and all three phases inherit them. Written flat
> you'd repeat those on every substate, and the day someone adds a phase and
> forgets one, the knight is immortal for an animation frame."

Then on the semantics test:

> "The part that's easy to get wrong is entry and exit ordering. Entering
> Combat runs Combat's entry action then Windup's. Moving Windup → Strike does
> *not* re-run Combat's. And Strike overrides the inherited `Staggered` with
> its own. Eighteen assertions pin that down."

### 8 — Parallel regions (2 min)

> "While the mech is Active it's doing two independent things: it has a
> movement mode *and* a weapon state. Drawing a weapon doesn't change how fast
> you're moving. Written as one flat machine that's four combined states and
> every transition written four times. Here each region is described once and
> the compiler multiplies them out."

**Say the cost yourself, before anyone asks:**

> "This multiplies the state space rather than adding to it — k regions of n
> states give n to the k product states. The compiler reports the
> multiplication and warns past 64 states. What it bought me is that three of
> the five stages needed no changes at all: the validator, the planner and the
> backend never learn parallelism exists."

Then on the semantics test:

> "Producing the right *number* of states is easy. Proving the regions are
> actually independent is the part that matters, so the test poisons one
> region's marker variable — sets it to 77 — fires the *other* region's event,
> and checks it's still 77. If the construction were re-entering that
> component, its entry action would have overwritten it. Sixteen assertions."

### 9 — Engine actions (90s)

> "A context struct isn't enough — an NPC has to be able to play an animation
> or make a noise. Those are declared in an `actions` block, so the compiler
> can check them, and supplied by the engine, so the language stays small."

Point at the generated header:

> "The header states exactly what the engine must supply. Plain free
> functions — no virtual dispatch, no function-pointer table, nothing
> allocated. The linker catches any you forget."

Then the rejection:

> "Three faults in one machine: a call to something never declared, a call
> with the wrong number of arguments, and a bool where an int was declared.
> All three named with a line number, before any C++ exists."

### 10 — Measured against hand-written C++ (90s)

> "Two baselines: the classic State pattern with virtual dispatch, and a naive
> flat table. It checks all three produce identical state traces before timing
> anything — a faster machine that behaves differently isn't a faster machine."

**Say this yourself, before anyone asks:**

> "Note the modelled 19-probes-to-1 is *not* a 19× speed-up. Real hardware
> absorbs a short comparison chain through caching and branch prediction; the
> measured gap is 4.8×. The static count overestimated by about four times, and
> the transpiler now says so in its own output."

### 11 — Game loop (60s)

> "512 agents, 4000 frames, two million agent-frames. 14.5 ns each including
> the per-frame update. At 60 fps that's about a million agents on one core."

### 12 — Calibration (90s) — **the punchline**

> "The thresholds started as reasoning: a matrix should be mostly full, it
> should fit in L1. I measured instead — 2×2 up to 128×128, every strategy
> forced, all timed. The reasoning was wrong twice. The dense matrix wins at
> every occupancy down to 2%, so occupancy came out of the decision entirely.
> And LINEAR_SCAN never won at any size, so I removed it. One threshold
> survived, tightened from an assumed 32 kB to a measured 24 kB."

That last point — **a strategy that got deleted because it didn't earn its
place** — is a stronger story than having three strategies. Lead with it.

### 13 — Test suite (45s)

> "Fifty-one checks across ten groups: positive, negative, strategy selection,
> scale, engine actions, hierarchy semantics, parallel semantics, behavioural
> equivalence with hand-written C++, the game loop, and the compiler itself
> under ASan and UBSan."

---

## Closing line

> "That's the whole project. All five stages work end to end — hierarchy,
> parallel regions and engine calls included — the thresholds are measured
> rather than assumed, and the generated code is benchmarked against
> hand-written C++ and proven behaviourally identical to it. What I'd do next
> is re-measure the cache boundary on a second architecture: the 24 kB number
> is from one machine and I don't want to overclaim it."

Saying this yourself is better than being asked.

---

## Questions you should expect

**"Isn't this just Yakindu / Stateflow / SCXML?"**
> Those validate and those generate, but as separate stages — the emitted
> architecture doesn't change with the shape of the graph. Here the
> verification output feeds the code generator. Unreachable states don't just
> get a warning, they're removed from the emitted enum and table.

**"Why not Behaviour Trees?"**
> Two reasons. FSM semantics map onto a simpler context-free grammar, which
> kept the Flex/Bison frontend achievable this semester. More importantly, the
> properties I check — reachability, sinkholes, determinism — are graph
> algorithms on a transition digraph, and that same digraph is what makes the
> synthesis planning possible. A tree has no state-event occupancy to measure.

**"How do parallel regions interact with the rest of the pipeline?"**
> They don't. The flattening pass multiplies the regions out into ordinary
> product states, so the validator, the planner and the backend are unchanged —
> they never learn that parallelism exists. One pass absorbs the complexity
> instead of it spreading across four.

**"Where did 19 probes → 1 come from? Did you measure it?"**
> Yes — `make bench`. And the honest answer is that the static count
> overestimates: measured, it's 4.8× against the naive table and 1.5× against
> the State pattern, not 19×. Caching and branch prediction absorb a short
> comparison chain. The transpiler prints that caveat itself now.

**"Isn't the dense matrix wasting memory at low occupancy?"**
> That's exactly what I assumed, and the measurement disagreed. Empty cells
> are never touched, so they cost address space rather than cache bandwidth.
> Dense won down to 2% occupancy. What does bind is total table size, which is
> why the one surviving threshold is a 24 kB cache budget.

**"Why did you remove LINEAR_SCAN?"**
> It never won. Below about nine cells it tied with the switch; above that it
> lost, and at 128×128 it was 280× slower than the matrix. It's still
> reachable through `--force-strategy` so the calibration harness can keep
> measuring it, but the selector doesn't choose it.

**"Does hierarchy actually change the generated code?"**
> Yes — each transition record carries an exit chain and an entry chain into a
> flat array of node ids, so exits run innermost-first up to the common
> ancestor and entries run outermost-first down to the target. For a flat
> machine both chains are length one, which is exactly what it was before.

**"What happens with a guard that's false everywhere?"**
> That's AEG-W002 — it warns that the event can be silently dropped when every
> transition on it is guarded. Show it in the guard.aegis output.

**"How big can a machine get?"**
> The state encoding widens automatically past 255 states. The dense matrix is
> capped at the measured 24,576 B budget, past which it falls back to the
> sparse strategy. The scale test is a generated 120-state, 112-event machine —
> 470 transitions, 80 kB of C++, 6 ms to compile.

**"What if one event should move two parallel regions at once?"**
> Rejected with AEG-E025 rather than silently picking one region. Supporting it
> properly means conjoining the guards across regions and multiplying the
> transition set by the number of guard combinations. It's stated as future
> work rather than half-implemented.

**"Do engine calls break the no-allocation guarantee?"**
> No. They lower to direct calls to free functions declared in the generated
> header — `hooks::PlayAnim(ctx, clip)`. No virtual dispatch, no function
> pointers, nothing allocated. The linker resolves them against whatever the
> engine defines.

---

## If something goes wrong

| Problem | Do this |
|---|---|
| `make` fails on flex/bison | `brew install flex bison`, then `make clean && make` |
| `demo.sh` says permission denied | `chmod +x demo.sh` |
| Colours look wrong on the projector | `./demo.sh --auto \| cat` strips them |
| Terminal too small for the tables | Shrink the font before you start, not during |
| Something errors live | Don't debug on stage. Say "let me show you the test suite instead," run `make test`, move on |
| The benchmark runs long | `./demo.sh --short` skips step 8 and 9 |

**Fallback if the laptop dies entirely:** the three SVG diagrams and the
synthesis report screenshot carry most of the story on their own. Have them on
a phone or a USB stick.

---

## The one-sentence version

If you get cut short and have thirty seconds:

> "It's a language for NPC behaviour that catches logic bugs a C++ compiler
> can't see — states the NPC can't escape, states it can never reach — and
> then uses what it proved about the graph to decide how to generate the C++.
> It's measured against hand-written C++, and it's faster."
