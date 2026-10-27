#!/usr/bin/env python3
"""
Generates a large, sparse .aegis machine for stress testing.

Two purposes:

  1. It exercises the SPARSE_SWITCH path. After calibration the dense matrix
     is chosen for anything that fits the cache budget, so a machine has to
     be genuinely large before the fallback is selected. The small hand-written
     examples no longer reach that size.

  2. It is the scale test the milestone plan called for: hundreds of states
     and events, far past anything a person would write by hand, to confirm
     the frontend, the validator and the backend all hold up.

Usage:  python3 tools/gen_stress.py [-o out.aegis] [--states N] [--events N]
"""

import argparse
import random


def generate(n_states, n_events, occupancy, seed, name):
    rng = random.Random(seed)
    states = [f"Zone{i:03d}" for i in range(n_states)]
    events = [f"Sig{i:03d}" for i in range(n_events)]

    # Hamiltonian backbone so every state is reachable and none is a sinkhole.
    edges = {(i, 0): (i + 1) % n_states for i in range(n_states)}

    target = int(round(n_states * n_events * occupancy))
    free = [(s, e) for s in range(n_states) for e in range(n_events) if (s, e) not in edges]
    rng.shuffle(free)
    for cell in free[: max(0, target - len(edges))]:
        edges[cell] = rng.randrange(n_states)

    out = [
        "// ---------------------------------------------------------------------------",
        f"//  {name} - generated stress machine ({n_states} states, {n_events} events).",
        "//  Produced by tools/gen_stress.py; do not edit by hand.",
        "//",
        "//  Large enough that the dense dispatch matrix would exceed the measured cache",
        "//  budget, so the planner falls back to SPARSE_SWITCH. Also the scale test for",
        "//  the frontend and validator.",
        "// ---------------------------------------------------------------------------",
        "",
        f"machine {name} {{",
        "",
        "    context {",
        "        int  alert   = 0;",
        "        int  ticks   = 0;",
        "        bool armed   = true;",
        "    }",
        "",
        "    events {",
    ]
    for i in range(0, n_events, 8):
        chunk = ", ".join(events[i:i + 8])
        out.append(f"        {chunk}" + ("," if i + 8 < n_events else ""))
    out += ["    }", ""]

    for i, s in enumerate(states):
        out.append(f"    {'initial ' if i == 0 else ''}state {s} {{")
        if i % 7 == 0:
            out.append("        on enter  { alert = 1; }")
        out.append("        on update { ticks = ticks + 1; }")
        for (ss, ee), tgt in sorted(edges.items()):
            if ss != i:
                continue
            # sprinkle guards so the guard machinery is exercised at scale too
            if (ss + ee) % 11 == 0:
                out.append(f"        on {events[ee]} -> {states[tgt]} when armed;")
            else:
                out.append(f"        on {events[ee]} -> {states[tgt]};")
        out.append("    }")
        out.append("")
    out.append("}")
    return "\n".join(out), len(edges)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default="examples/crowd.aegis")
    ap.add_argument("--states", type=int, default=120)
    ap.add_argument("--events", type=int, default=112)
    ap.add_argument("--occupancy", type=float, default=0.035)
    ap.add_argument("--seed", type=int, default=20260916)
    ap.add_argument("--name", default="CrowdDirector")
    args = ap.parse_args()

    text, edges = generate(args.states, args.events, args.occupancy, args.seed, args.name)
    with open(args.out, "w") as f:
        f.write(text + "\n")

    cells = args.states * args.events
    print(f"wrote {args.out}")
    print(f"  {args.states} states x {args.events} events = {cells} cells")
    print(f"  {edges} transitions, occupancy {edges / cells * 100:.1f}%")
    print(f"  a dense matrix would need {cells * 2} bytes")


if __name__ == "__main__":
    main()
