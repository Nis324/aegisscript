#!/usr/bin/env python3
"""
AegisScript threshold calibration
=================================

The synthesis planner picks between LINEAR_SCAN, DENSE_TABLE and SPARSE_SWITCH
using two constants: an occupancy threshold and a storage budget. Those
constants were originally *reasoned about* (a matrix should be mostly full; it
should fit in L1) rather than measured, which is a weakness.

This harness replaces the reasoning with measurement. It:

  1. generates a corpus of synthetic .aegis machines spanning a grid of
     (state count, event count, occupancy);
  2. transpiles each one three times, forcing each strategy in turn;
  3. compiles and times each result over an identical event stream;
  4. reports which strategy actually wins at each point, and where the
     crossover really is.

Usage:  python3 tools/calibrate.py [--quick]
"""

import argparse
import os
import random
import re
import shutil
import statistics
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AEGISC = os.path.join(ROOT, "aegisc")
CXX = os.environ.get("CXX", "g++")
STRATEGIES = ["linear", "dense", "sparse"]

# grid: (states, events) pairs, and the occupancies to sample at each
GRID_FULL = [(4, 4), (6, 6), (8, 8), (10, 10), (12, 12), (16, 16), (24, 24)]
GRID_QUICK = [(6, 6), (10, 10), (16, 16)]
OCCUPANCIES_FULL = [0.10, 0.15, 0.20, 0.25, 0.30, 0.40, 0.55, 0.75]
OCCUPANCIES_QUICK = [0.15, 0.30, 0.60]

REPS = 2000
STREAM = 4096


# --------------------------------------------------------------- corpus ----
def make_machine(name, n_states, n_events, occupancy, seed):
    """Emit a syntactically valid .aegis machine with the requested shape.

    Every state is reachable (a Hamiltonian backbone guarantees it) and no
    state is a sinkhole, so the machine always passes validation; only the
    occupancy of the state x event space varies.
    """
    rng = random.Random(seed)
    states = [f"S{i}" for i in range(n_states)]
    events = [f"E{i}" for i in range(n_events)]

    target_cells = max(n_states, int(round(n_states * n_events * occupancy)))
    target_cells = min(target_cells, n_states * n_events)

    # backbone: S0 -E0-> S1 -E0-> ... -> S0  (keeps everything reachable)
    edges = {}
    for i in range(n_states):
        edges[(i, 0)] = (i + 1) % n_states

    # fill the rest at random until the occupancy target is met
    free = [(s, e) for s in range(n_states) for e in range(n_events) if (s, e) not in edges]
    rng.shuffle(free)
    for cell in free[: max(0, target_cells - len(edges))]:
        edges[cell] = rng.randrange(n_states)

    lines = [f"machine {name} {{", "    context { int tick = 0; }",
             "    events { " + ", ".join(events) + " }", ""]
    for i, s in enumerate(states):
        lines.append(f"    {'initial ' if i == 0 else ''}state {s} {{")
        lines.append("        on update { tick = tick + 1; }")
        for (ss, ee), tgt in sorted(edges.items()):
            if ss == i:
                lines.append(f"        on {events[ee]} -> {states[tgt]};")
        lines.append("    }")
        lines.append("")
    lines.append("}")
    return "\n".join(lines), len(edges)


BENCH_TEMPLATE = r"""
#include "{cls}.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <algorithm>

int main() {{
    constexpr int kEvents = {n_events};
    constexpr int kStream = {stream};
    constexpr int kReps   = {reps};

    uint32_t r = 0x9E3779B9u;
    std::vector<uint8_t> stream(kStream);
    for (int i = 0; i < kStream; ++i) {{
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        stream[i] = static_cast<uint8_t>(r % kEvents);
    }}

    std::vector<double> runs;
    uint64_t sink = 0;
    for (int run = 0; run < 5; ++run) {{
        aegis_gen::{cls} m;
        uint64_t acc = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int rep = 0; rep < kReps; ++rep)
            for (uint8_t e : stream) {{
                m.update();
                acc += m.dispatch(static_cast<aegis_gen::{cls}Event>(e)) ? 1u : 0u;
            }}
        auto t1 = std::chrono::steady_clock::now();
        sink += acc;
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        runs.push_back(ns / (double(kReps) * kStream));
    }}
    std::sort(runs.begin(), runs.end());
    std::printf("%.4f %llu\n", runs[runs.size() / 2],
                static_cast<unsigned long long>(sink));
    return 0;
}}
"""


def measure(src_path, cls, n_events, strategy, workdir):
    out = os.path.join(workdir, strategy)
    os.makedirs(out, exist_ok=True)
    gen = subprocess.run(
        [AEGISC, src_path, "-o", out, f"--force-strategy={strategy}", "--no-color"],
        capture_output=True, text=True)
    if gen.returncode != 0:
        return None, None

    bench_cpp = os.path.join(out, "bench.cpp")
    with open(bench_cpp, "w") as f:
        f.write(BENCH_TEMPLATE.format(cls=cls, n_events=n_events,
                                      stream=STREAM, reps=REPS))

    exe = os.path.join(out, "bench")
    obj = os.path.join(out, "machine.o")
    cc = subprocess.run(
        [CXX, "-std=c++17", "-O2", f"-I{out}", bench_cpp,
         os.path.join(out, f"{cls}.cpp"), "-o", exe],
        capture_output=True, text=True)
    if cc.returncode != 0:
        print("   compile failed:", cc.stderr.splitlines()[:3])
        return None, None

    subprocess.run([CXX, "-std=c++17", "-O2", f"-I{out}", "-c",
                    os.path.join(out, f"{cls}.cpp"), "-o", obj],
                   capture_output=True, text=True)
    size = 0
    sz = subprocess.run(["size", "-d", obj], capture_output=True, text=True)
    if sz.returncode == 0 and len(sz.stdout.splitlines()) > 1:
        parts = sz.stdout.splitlines()[1].split()
        try:
            size = int(parts[0]) + int(parts[1]) + int(parts[2])
        except (ValueError, IndexError):
            size = 0

    run = subprocess.run([exe], capture_output=True, text=True)
    if run.returncode != 0:
        return None, None
    return float(run.stdout.split()[0]), size


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true", help="smaller grid, faster")
    ap.add_argument("--shapes", help="override shapes, e.g. 12x12,24x24,64x64")
    ap.add_argument("--occs", help="override occupancies, e.g. 0.03,0.10,0.30")
    ap.add_argument("--reps", type=int, help="timing repetitions")
    args = ap.parse_args()

    grid = GRID_QUICK if args.quick else GRID_FULL
    occs = OCCUPANCIES_QUICK if args.quick else OCCUPANCIES_FULL
    if args.shapes:
        grid = [tuple(int(x) for x in p.split("x")) for p in args.shapes.split(",")]
    if args.occs:
        occs = [float(x) for x in args.occs.split(",")]
    global REPS
    if args.reps:
        REPS = args.reps

    if not os.path.exists(AEGISC):
        print("aegisc not built -- run 'make' first")
        return 1

    print("AegisScript threshold calibration")
    print(f"  grid        : {len(grid)} shapes x {len(occs)} occupancies "
          f"x {len(STRATEGIES)} strategies = {len(grid)*len(occs)*len(STRATEGIES)} builds")
    print(f"  measurement : {REPS} reps x {STREAM} events, median of 5 runs\n")

    workdir = tempfile.mkdtemp(prefix="aegis-calib-")
    rows = []
    try:
        for (ns, ne) in grid:
            for occ in occs:
                cls = f"M{ns}x{ne}o{int(occ*100)}"
                src, edges = make_machine(cls, ns, ne, occ, seed=ns * 1000 + ne * 10 + int(occ * 100))
                src_path = os.path.join(workdir, f"{cls}.aegis")
                with open(src_path, "w") as f:
                    f.write(src)

                actual_occ = edges / float(ns * ne)
                times, sizes = {}, {}
                for st in STRATEGIES:
                    t, sz = measure(src_path, cls, ne, st, os.path.join(workdir, cls))
                    if t is None:
                        break
                    times[st], sizes[st] = t, sz
                if len(times) != len(STRATEGIES):
                    print(f"  skipped {cls}")
                    continue

                best = min(times, key=times.get)
                rows.append(dict(states=ns, events=ne, cells=ns * ne,
                                 occ=actual_occ, times=times, sizes=sizes, best=best))
                print(f"  {ns:3d}x{ne:<3d} cells={ns*ne:<5d} occ={actual_occ*100:5.1f}%   "
                      + "  ".join(f"{st}={times[st]:6.2f}" for st in STRATEGIES)
                      + f"   -> {best.upper()}")
    finally:
        pass

    # ------------------------------------------------------------ report ----
    print("\n" + "=" * 78)
    print("  RESULTS")
    print("=" * 78)

    print("\n  Winner by occupancy band (all shapes pooled):\n")
    bands = [(0.0, 0.15), (0.15, 0.25), (0.25, 0.35), (0.35, 0.50), (0.50, 1.01)]
    for lo, hi in bands:
        sel = [r for r in rows if lo <= r["occ"] < hi]
        if not sel:
            continue
        tally = {}
        for r in sel:
            tally[r["best"]] = tally.get(r["best"], 0) + 1
        winner = max(tally, key=tally.get)
        detail = ", ".join(f"{k}:{v}" for k, v in sorted(tally.items()))
        print(f"    {int(lo*100):3d}-{int(hi*100):3d}%  n={len(sel):2d}   "
              f"{winner.upper():14s} ({detail})")

    print("\n  Dense vs sparse, by occupancy (small machines excluded, cells > 16):\n")
    print(f"    {'occupancy':>10}  {'dense ns':>9}  {'sparse ns':>10}  {'dense/sparse':>13}  verdict")
    big = sorted([r for r in rows if r["cells"] > 16], key=lambda r: r["occ"])
    for r in big:
        d, sp = r["times"]["dense"], r["times"]["sparse"]
        ratio = d / sp
        verdict = "dense wins" if ratio < 0.98 else ("sparse wins" if ratio > 1.02 else "tie")
        print(f"    {r['occ']*100:9.1f}%  {d:9.2f}  {sp:10.2f}  {ratio:13.3f}  {verdict}")

    print("\n  Footprint cost of the dense matrix (bytes, .o text+data):\n")
    print(f"    {'shape':>9}  {'occupancy':>10}  {'dense':>7}  {'sparse':>7}  {'linear':>7}")
    for r in sorted(rows, key=lambda r: (r["cells"], r["occ"]))[:14]:
        print(f"    {r['states']:3d}x{r['events']:<4d} {r['occ']*100:9.1f}%  "
              f"{r['sizes']['dense']:7d}  {r['sizes']['sparse']:7d}  {r['sizes']['linear']:7d}")

    # crossover estimate
    cross = None
    for r in big:
        if r["times"]["dense"] < r["times"]["sparse"] * 0.98:
            cross = r["occ"]
            break
    print("\n  " + "-" * 74)
    if cross is not None:
        print(f"  Measured dense/sparse crossover: dense first wins at ~{cross*100:.0f}% occupancy.")
    else:
        print("  Dense never beat sparse on this grid.")
    tiny = [r for r in rows if r["cells"] <= 16]
    if tiny:
        wins = sum(1 for r in tiny if r["best"] == "linear")
        print(f"  Small machines (cells <= 16): linear won {wins}/{len(tiny)}.")
    print("  " + "-" * 74)

    shutil.rmtree(workdir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
