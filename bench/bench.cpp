// ============================================================================
//  AegisScript benchmark harness
//
//  Measures the generated machine against two hand-written baselines over an
//  identical, deterministic event stream:
//
//    A. State pattern  - one class per state, virtual dispatch (GoF)
//    B. Naive table    - one flat transition list, linear scan
//    C. AegisScript    - generated from examples/guard.aegis
//
//  It does two things, in this order:
//    1. EQUIVALENCE - drives all three over the same stream and asserts the
//       state traces are identical. A faster machine that behaves differently
//       is not a faster machine.
//    2. THROUGHPUT  - times each over many repetitions, reports the median of
//       several runs in nanoseconds per dispatch.
//
//  Build:  make bench
// ============================================================================

#include "handwritten_guard.hpp"
#include "naive_guard.hpp"
#include "GuardAI.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr int kEvents      = 6;
constexpr int kStreamLen   = 4096;      // event stream, fits comfortably in L1
constexpr int kReps        = 3000;      // repetitions per timed run
constexpr int kRuns        = 7;         // timed runs; the median is reported

// xorshift32: deterministic, identical on every platform, cheap enough that it
// does not dominate the measurement.
uint32_t rngState = 0x9E3779B9u;
uint32_t nextRand() {
    uint32_t x = rngState;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rngState = x;
}

std::vector<uint8_t> buildStream() {
    rngState = 0x9E3779B9u;
    std::vector<uint8_t> s(kStreamLen);
    for (int i = 0; i < kStreamLen; ++i)
        s[i] = static_cast<uint8_t>(nextRand() % kEvents);
    return s;
}

using Clock = std::chrono::steady_clock;

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// ---------------------------------------------------------------- traces ---
std::vector<uint8_t> traceHandwritten(const std::vector<uint8_t>& stream) {
    handwritten::GuardAI m;
    std::vector<uint8_t> t;
    t.reserve(stream.size());
    const char* names[] = { "Patrol", "Chase", "Combat", "Search", "Rest", "Dead" };
    for (uint8_t e : stream) {
        m.update();
        m.dispatch(static_cast<handwritten::Event>(e));
        uint8_t id = 0;
        for (uint8_t i = 0; i < 6; ++i)
            if (std::string(m.stateName()) == names[i]) { id = i; break; }
        t.push_back(id);
    }
    return t;
}

std::vector<uint8_t> traceNaive(const std::vector<uint8_t>& stream) {
    naive::GuardAI m;
    std::vector<uint8_t> t;
    t.reserve(stream.size());
    for (uint8_t e : stream) {
        m.update();
        m.dispatch(static_cast<naive::Event>(e));
        t.push_back(static_cast<uint8_t>(m.state()));
    }
    return t;
}

std::vector<uint8_t> traceAegis(const std::vector<uint8_t>& stream) {
    aegis_gen::GuardAI m;
    std::vector<uint8_t> t;
    t.reserve(stream.size());
    for (uint8_t e : stream) {
        m.update();
        m.dispatch(static_cast<aegis_gen::GuardAIEvent>(e));
        t.push_back(static_cast<uint8_t>(m.state()));
    }
    return t;
}

// ------------------------------------------------------------- throughput --
template <typename Machine, typename EventT>
double timeMachine(const std::vector<uint8_t>& stream, uint64_t& sink) {
    std::vector<double> runs;
    runs.reserve(kRuns);

    for (int run = 0; run < kRuns; ++run) {
        Machine m;
        uint64_t acc = 0;
        auto t0 = Clock::now();
        for (int rep = 0; rep < kReps; ++rep) {
            for (uint8_t e : stream) {
                m.update();
                acc += m.dispatch(static_cast<EventT>(e)) ? 1u : 0u;
            }
        }
        auto t1 = Clock::now();
        sink += acc;
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        runs.push_back(ns / (static_cast<double>(kReps) * stream.size()));
    }
    return medianOf(runs);
}

// the State-pattern machine needs its own loop: stateName() comparison is not
// part of the hot path, so it is excluded, exactly as for the other two.
double timeHandwritten(const std::vector<uint8_t>& stream, uint64_t& sink) {
    std::vector<double> runs;
    for (int run = 0; run < kRuns; ++run) {
        handwritten::GuardAI m;
        uint64_t acc = 0;
        auto t0 = Clock::now();
        for (int rep = 0; rep < kReps; ++rep) {
            for (uint8_t e : stream) {
                m.update();
                acc += m.dispatch(static_cast<handwritten::Event>(e)) ? 1u : 0u;
            }
        }
        auto t1 = Clock::now();
        sink += acc;
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        runs.push_back(ns / (static_cast<double>(kReps) * stream.size()));
    }
    return medianOf(runs);
}

} // namespace

int main() {
    const std::vector<uint8_t> stream = buildStream();

    std::printf("AegisScript benchmark\n");
    std::printf("  machine      : examples/guard.aegis (6 states, 6 events, 19 transitions)\n");
    std::printf("  event stream : %d pseudo-random events, deterministic seed\n", kStreamLen);
    std::printf("  timed        : %d repetitions x %d runs, median reported\n\n", kReps, kRuns);

    // ---- 1. equivalence ---------------------------------------------------
    std::printf("== behavioural equivalence ==\n");
    auto tA = traceHandwritten(stream);
    auto tB = traceNaive(stream);
    auto tC = traceAegis(stream);

    bool okAB = (tA == tB);
    bool okAC = (tA == tC);
    std::printf("  state pattern  vs naive table   : %s\n", okAB ? "identical" : "DIVERGED");
    std::printf("  state pattern  vs AegisScript   : %s\n", okAC ? "identical" : "DIVERGED");

    if (!okAC) {
        for (size_t i = 0; i < tA.size(); ++i)
            if (tA[i] != tC[i]) {
                std::printf("  first divergence at event %zu: handwritten=%u aegis=%u\n",
                            i, tA[i], tC[i]);
                break;
            }
        std::printf("\n  FAILED: generated code does not match the hand-written machine.\n");
        return 1;
    }
    std::printf("  %d events, %zu transitions taken, traces match exactly\n\n",
                kStreamLen, tA.size());

    // ---- 2. throughput ----------------------------------------------------
    uint64_t sink = 0;
    double hw    = timeHandwritten(stream, sink);
    double nv    = timeMachine<naive::GuardAI, naive::Event>(stream, sink);
    double ag    = timeMachine<aegis_gen::GuardAI, aegis_gen::GuardAIEvent>(stream, sink);

    std::printf("== throughput (ns per update+dispatch, lower is better) ==\n");
    std::printf("  %-28s %9s %10s\n", "implementation", "ns/op", "vs aegis");
    std::printf("  %-28s %9.2f %9.2fx\n", "A. State pattern (virtual)", hw, hw / ag);
    std::printf("  %-28s %9.2f %9.2fx\n", "B. Naive flat table",        nv, nv / ag);
    std::printf("  %-28s %9.2f %9s\n",    "C. AegisScript generated",   ag, "1.00x");

    std::printf("\n  (checksum %llu -- printed so the optimiser cannot elide the work)\n",
                static_cast<unsigned long long>(sink));
    return 0;
}
