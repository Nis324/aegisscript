# Threshold calibration

**What this is:** the synthesis planner chooses between three code shapes using
numeric thresholds. Those thresholds started as *reasoning* — a matrix should be
"mostly full" to be worth its bytes; it should "fit in L1". This document is the
measurement that replaced the reasoning, and it disagreed with it.

Reproduce with `make calibrate`, or the full sweep:

```bash
python3 tools/calibrate.py --shapes 24x24,64x64,128x128 --occs 0.02,0.05,0.15
```

---

## Method

For each point on a grid of (states × events × occupancy), the harness:

1. generates a synthetic `.aegis` machine of that shape — a Hamiltonian
   backbone guarantees every state is reachable and none is a sinkhole, so
   only the occupancy varies;
2. transpiles it **three times**, forcing each strategy with `--force-strategy`;
3. compiles each at `-O2` and times it over an identical pseudo-random event
   stream (4096 events, deterministic seed, median of 5 runs);
4. records `ns` per `update()` + `dispatch()` and the `.o` text+data size.

Measurements below are from the development machine (x86-64, GCC 13, `-O2`).
Absolute numbers will differ elsewhere; the *ordering* is what the thresholds
depend on.

---

## Result 1 — the occupancy threshold was wrong

The original rule required **≥ 25% occupancy** before using a dense matrix, on
the reasoning that a mostly-empty table wastes memory and cache.

| shape | occupancy | linear | dense | sparse | winner |
|---|---:|---:|---:|---:|---|
| 24×24 | 4.2% | 12.44 | **3.05** | 5.78 | dense |
| 24×24 | 5.0% | 14.00 | **3.17** | 6.29 | dense |
| 24×24 | 14.9% | 31.19 | **3.67** | 3.90 | dense |
| 64×64 | 2.0% | 84.67 | **3.69** | 7.78 | dense |
| 64×64 | 5.0% | 133.74 | **3.75** | 8.81 | dense |
| 64×64 | 15.0% | 358.37 | **4.62** | 4.85 | dense |
| 10×10 | 60.0% | 24.92 | **5.62** | 7.82 | dense |
| 16×16 | 60.2% | 83.10 | **7.75** | 9.60 | dense |

ns per op, lower is better.

**Dense won at every occupancy measured, down to 2%.** The reasoning was wrong
about why: the empty cells of the matrix are never *touched*, so they never
enter cache. They cost address space, not bandwidth. An indexed load beats a
comparison chain regardless of how much of the table is populated.

Occupancy was removed from the decision entirely.

---

## Result 2 — `LINEAR_SCAN` never wins

The original rule sent machines with ≤ 16 cells to a linear scan, on the
reasoning that an index costs more than it saves at that size.

| shape | cells | occupancy | linear | dense | sparse |
|---|---:|---:|---:|---:|---:|
| 2×2 | 4 | 50% | 7.09 | 7.53 | **7.05** |
| 3×3 | 9 | 44% | 6.44 | 6.99 | **5.93** |
| 4×4 | 16 | 50% | 11.69 | **6.88** | 7.46 |
| 5×5 | 25 | 48% | 10.69 | **6.56** | 6.67 |
| 128×128 | 16384 | 15% | 1424.66 | 5.10 | **4.82** |

Linear scan was never fastest at any size measured. Below ~9 cells it was
within noise of the alternatives; above that it lost, and at scale it lost
catastrophically (1425 ns/op at 128×128 — 280× the dense table).

**The selector no longer chooses it.** The code path is kept, reachable only
through `--force-strategy=linear`, so the calibration harness can keep
measuring it and so the regression can be caught if it ever becomes the right
answer on different hardware.

This is the result worth being honest about: a strategy that existed because it
sounded reasonable, measured, and removed.

---

## Result 3 — the storage budget was approximately right

The original 32 KB cap was the only constant the measurement supported.

| shape | cells | dense table | occupancy | dense | sparse | winner |
|---|---:|---:|---:|---:|---:|---|
| 80×80 | 6400 | 12.8 kB | 12% | **6.06** | 7.47 | dense |
| 96×96 | 9216 | 18.4 kB | 12% | **3.88** | 6.36 | dense |
| 112×112 | 12544 | 25.1 kB | 12% | **5.07** | 9.35 | dense |
| 112×112 | 12544 | 25.1 kB | 20% | **5.81** | 6.20 | dense |
| 128×128 | 16384 | 32.8 kB | 15% | 5.10 | **4.82** | sparse |

Dense held up to a 25 kB table and lost at 33 kB — consistent with a 32–48 kB
L1 data cache. The budget was tightened from an assumed 32768 B to a
**measured 24576 B**, the largest table the measurement actually supports.

---

## The rule that survived

Three constants became one:

```cpp
constexpr long kMaxTableBytes = 24576;   // measured, not assumed

if (denseBytes <= kMaxTableBytes)  DENSE_TABLE;
else                               SPARSE_SWITCH;
```

Everything that fits in cache gets the matrix. Everything else gets the switch.
Occupancy does not enter into it, and the linear scan is not offered.

---

## Effect on the shipped examples

| machine | cells | dense table | selected |
|---|---:|---:|---|
| `turret.aegis` | 4 | 8 B | DENSE_TABLE |
| `knight.aegis` | 35 | 70 B | DENSE_TABLE |
| `guard.aegis` | 36 | 72 B | DENSE_TABLE |
| `questline.aegis` | 70 | 140 B | DENSE_TABLE |
| `crowd.aegis` (generated) | 13440 | 26880 B | SPARSE_SWITCH |

Under the old thresholds `questline` selected `SPARSE_SWITCH` and `turret`
selected `LINEAR_SCAN`. Both were the wrong choice, and only measurement
showed it. A machine now has to be genuinely large — past what anyone writes by
hand — before the fallback is correct, which is why `tools/gen_stress.py`
exists.

---

## What this does not establish

- One machine, one compiler, one architecture. The *ordering* of the strategies
  is probably stable; the exact 24 kB boundary is not portable and should be
  re-measured on a target platform.
- The event stream is uniformly random. A real game has correlated events and
  agents clustered in a few states, which is friendlier to caches than this.
- Only single-agent dispatch is timed. With thousands of agents the working set
  is dominated by agent state, not by the transition table, which would push
  the boundary down.
