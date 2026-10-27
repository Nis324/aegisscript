// ============================================================================
//  AegisScript - Synthesis Strategy Selector  (Phase 2b)
// ----------------------------------------------------------------------------
//  This is the core technical contribution of the project.
//
//  Conventional FSM generators pick ONE fixed dispatch shape (usually a nested
//  switch) and emit it for every machine.  AegisScript instead treats the
//  verified transition graph as an input to code synthesis: the same static
//  analysis that proves the machine correct also measures its shape, and the
//  measured shape selects the dispatch topology and the memory layout of the
//  emitted C++.
//
//  Effects that are measurable at the machine level:
//    * worst-case dispatch cost bounded and, for dense graphs, made O(1)
//      and branch-free;
//    * transition storage sized at compile time, never heap allocated;
//    * states proven unreachable are excluded from the emitted state space,
//      so both the enum width and the table are physically smaller.
// ============================================================================
#pragma once

#include "ast.hpp"
#include "validator.hpp"
#include <string>
#include <vector>

namespace aegis {

enum class Strategy {
    LinearScan,     // tiny graphs: one ordered array, smallest footprint
    DenseTable,     // dense graphs: [state][event] matrix, O(1) branch-free
    SparseSwitch    // large/sparse graphs: nested switch, no matrix cost
};

const char* strategyName(Strategy s);

struct LayoutPlan {
    Strategy strategy = Strategy::SparseSwitch;

    int  stateWidthBits   = 8;    // width of the emitted state enum
    int  eventWidthBits   = 8;
    long tableBytes       = 0;    // static storage for the dispatch structure
    int  worstCaseProbes  = 0;    // upper bound on comparisons per dispatch
    int  prunedStates     = 0;    // unreachable states excluded from synthesis

    double density        = 0.0;
    long   cells          = 0;

    std::vector<std::string> rationale;   // human-readable decision trace
};

// Builds the plan from the verified machine + the graph facts.
// `forced` overrides the selector; it exists so the calibration harness can
// generate the same machine under every strategy and measure them.
LayoutPlan planLayout(const Machine& m, const GraphFacts& f, const Strategy* forced = nullptr);

// Parses a strategy name ("dense", "sparse", "linear"). Returns false if unknown.
bool parseStrategy(const std::string& name, Strategy& out);

// Cost of the naive fixed-shape baseline (linear scan of every transition),
// used to quantify the improvement in the --metrics report.
struct BaselineCost {
    int  worstCaseProbes = 0;
    long tableBytes      = 0;
};
BaselineCost baselineCost(const Machine& m, const GraphFacts& f);

} // namespace aegis
