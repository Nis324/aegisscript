#include "layout.hpp"

#include <algorithm>
#include <sstream>

namespace aegis {

const char* strategyName(Strategy s) {
    switch (s) {
        case Strategy::LinearScan:   return "LINEAR_SCAN";
        case Strategy::DenseTable:   return "DENSE_TABLE";
        case Strategy::SparseSwitch: return "SPARSE_SWITCH";
    }
    return "?";
}

namespace {

// CALIBRATED THRESHOLDS -- see docs/CALIBRATION.md.
//
// These were originally reasoned about (a matrix should be "mostly full"; it
// should "fit in L1"). Measurement over a corpus of 2x2 to 128x128 machines
// disagreed with the reasoning on two of the three constants:
//
//   * The 25% occupancy threshold was wrong. The dense matrix won at every
//     occupancy measured, down to 2%. An indexed load beats a comparison
//     chain even when the table is almost entirely empty, because the empty
//     cells are never touched and cost only address space. Occupancy was
//     dropped from the decision entirely.
//
//   * The "cells <= 16 => linear scan" rule was wrong. LINEAR_SCAN was never
//     the fastest strategy at any size measured; below ~9 cells it tied with
//     the switch, and above that it lost badly (1425 ns/op at 128x128). The
//     selector no longer chooses it; it survives only behind
//     --force-strategy so the calibration harness can keep measuring it.
//
//   * The 32 KB storage budget was approximately right. Dense won up to a
//     25 kB table (112x112) and lost at 32 kB (128x128), so the budget is
//     tightened to a measured 24 kB rather than an assumed 32 kB.
//
// What is left is a single decision: does the matrix still fit in cache?
constexpr long kMaxTableBytes = 24576;   // measured, not assumed

int widthBitsFor(int count) {
    if (count <= 255)   return 8;
    if (count <= 65535) return 16;
    return 32;
}

} // namespace

LayoutPlan planLayout(const Machine& m, const GraphFacts& f, const Strategy* forced) {
    LayoutPlan p;

    const int S = f.reachableStates;
    const int E = f.eventCount;
    p.prunedStates   = f.stateCount - f.reachableStates;
    p.cells          = static_cast<long>(S) * E;
    p.density        = f.density;
    p.stateWidthBits = widthBitsFor(std::max(S, 1));
    p.eventWidthBits = widthBitsFor(std::max(E, 1));

    const int stateBytes = p.stateWidthBits / 8;

    std::ostringstream os;
    os << "graph: " << S << " reachable states x " << E << " events = "
       << p.cells << " cells, " << f.transitionCount << " transitions, occupancy "
       << static_cast<int>(f.density * 100.0 + 0.5) << "%";
    p.rationale.push_back(os.str());

    if (p.prunedStates > 0) {
        std::ostringstream ps;
        ps << "pruned " << p.prunedStates
           << " unreachable state(s) from the synthesised state space";
        p.rationale.push_back(ps.str());
    }

    // Candidate cost of the dense matrix: one slot per (state, event) cell,
    // each slot holding a target state id plus a guard index.
    const long denseBytes = p.cells * (stateBytes + 1);

    if (forced) {
        p.strategy = *forced;
        switch (p.strategy) {
            case Strategy::DenseTable:
                p.tableBytes = denseBytes; p.worstCaseProbes = 1; break;
            default:
                p.tableBytes = static_cast<long>(f.transitionCount) * (stateBytes + 2);
                p.worstCaseProbes = std::max(f.maxFanOut, 1); break;
        }
        p.rationale.push_back(std::string("strategy forced to ") + strategyName(p.strategy)
                              + " (--force-strategy; calibration mode)");
        return p;
    }

    if (denseBytes <= kMaxTableBytes) {
        p.strategy        = Strategy::DenseTable;
        p.tableBytes      = denseBytes;
        p.worstCaseProbes = 1;     // single indexed load, no comparison chain
        p.rationale.push_back(
            "matrix needs " + std::to_string(denseBytes) + " B, within the measured " +
            std::to_string(kMaxTableBytes) + " B cache budget: emitting a contiguous "
            "[state][event] matrix, dispatch is one indexed load with no comparison chain");
        p.rationale.push_back(
            "occupancy (" + std::to_string(static_cast<int>(f.density * 100.0 + 0.5)) +
            "%) is not part of this decision: calibration found the matrix wins at every "
            "occupancy measured, down to 2%");
    }
    else {
        p.strategy        = Strategy::SparseSwitch;
        p.tableBytes      = static_cast<long>(f.transitionCount) * (stateBytes + 2);
        p.worstCaseProbes = std::max(f.maxFanOut, 1);
        p.rationale.push_back(
            "matrix would need " + std::to_string(denseBytes) + " B, above the measured " +
            std::to_string(kMaxTableBytes) + " B cache budget: a table this size thrashes, "
            "emitting a switch over the populated cells instead");
    }

    p.rationale.push_back(
        "storage is a single static constant array: no heap allocation, no virtual "
        "dispatch, footprint fixed at compile time");

    return p;
}

bool parseStrategy(const std::string& name, Strategy& out) {
    if (name == "dense"  || name == "DENSE_TABLE")   { out = Strategy::DenseTable;   return true; }
    if (name == "sparse" || name == "SPARSE_SWITCH") { out = Strategy::SparseSwitch; return true; }
    if (name == "linear" || name == "LINEAR_SCAN")   { out = Strategy::LinearScan;   return true; }
    return false;
}

BaselineCost baselineCost(const Machine& m, const GraphFacts& f) {
    BaselineCost b;
    // Naive generator: one flat list of every transition in the machine,
    // scanned linearly on each event.
    b.worstCaseProbes = std::max(f.transitionCount, 1);
    b.tableBytes      = static_cast<long>(f.stateCount) * 8      // per-state record
                      + static_cast<long>(f.transitionCount) * 12;
    return b;
}

} // namespace aegis
