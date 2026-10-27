// ============================================================================
//  AegisScript integration test: a minimal game loop.
//
//  The smoke harness that ships with each generated machine proves the code
//  compiles and can take a transition. This proves something different: that
//  the generated machine survives being used the way a game actually uses it —
//  many agents, many frames, per-frame update() plus event dispatch, agents
//  reaching terminal states and being respawned, over a long run.
//
//  It also reports what a profiler would care about: time per agent-frame, and
//  where the population ended up.
//
//  Build:  make sim
// ============================================================================

#include "GuardAI.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr int kAgents = 512;
constexpr int kFrames = 4000;

using Guard = aegis_gen::GuardAI;
using Event = aegis_gen::GuardAIEvent;
constexpr int kStateCount = static_cast<int>(aegis_gen::GuardAIState::COUNT);
constexpr int kEventCount = static_cast<int>(aegis_gen::GuardAIEvent::COUNT);

// Deterministic per-agent noise, so the run is reproducible across machines.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int below(int n) { return static_cast<int>(next() % static_cast<uint32_t>(n)); }
};

// A crude stand-in for perception: most frames nothing happens, occasionally
// the world pokes the agent. Roughly what a real game loop feeds an FSM.
bool worldEvent(Rng& rng, Event& out) {
    if (rng.below(100) >= 18) return false;         // ~18% of frames carry an event
    out = static_cast<Event>(rng.below(kEventCount));
    return true;
}

} // namespace

int main() {
    std::printf("AegisScript game-loop integration test\n");
    std::printf("  machine : GuardAI, generated from examples/guard.aegis\n");
    std::printf("  scene   : %d agents x %d frames = %d agent-frames\n\n",
                kAgents, kFrames, kAgents * kFrames);

    std::vector<Guard> agents(kAgents);
    std::vector<Rng>   rngs;
    rngs.reserve(kAgents);
    for (int i = 0; i < kAgents; ++i) rngs.emplace_back(0x1234567u + i * 2654435761u);

    long long transitions = 0, ignored = 0, respawns = 0;
    std::vector<long long> occupancy(kStateCount, 0);

    auto t0 = std::chrono::steady_clock::now();

    for (int frame = 0; frame < kFrames; ++frame) {
        for (int i = 0; i < kAgents; ++i) {
            Guard& g = agents[i];
            Rng&   r = rngs[i];

            g.update();                              // per-frame tick

            Event ev;
            if (worldEvent(r, ev)) {
                if (g.dispatch(ev)) ++transitions;
                else                ++ignored;
            }

            occupancy[static_cast<int>(g.state())]++;

            // A dead guard is removed and a fresh one spawns in its place,
            // which is what exercises reset() under load.
            if (g.state() == aegis_gen::GuardAIState::Dead) {
                g.reset();
                ++respawns;
            }
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    double perAgentFrame = ns / (static_cast<double>(kAgents) * kFrames);

    std::printf("== behaviour ==\n");
    std::printf("  transitions taken   : %lld\n", transitions);
    std::printf("  events ignored      : %lld   (no transition for that state/event)\n", ignored);
    std::printf("  agents respawned    : %lld\n\n", respawns);

    std::printf("== where the population spent its time ==\n");
    long long total = 0;
    for (long long v : occupancy) total += v;
    for (int s = 0; s < kStateCount; ++s) {
        double pct = total ? 100.0 * static_cast<double>(occupancy[s]) / static_cast<double>(total) : 0.0;
        std::printf("  %-8s %6.2f%%  ", Guard::stateName(static_cast<aegis_gen::GuardAIState>(s)), pct);
        int bars = static_cast<int>(pct / 2.0 + 0.5);
        for (int b = 0; b < bars; ++b) std::printf("#");
        std::printf("\n");
    }

    std::printf("\n== cost ==\n");
    std::printf("  total               : %.2f ms for %d agent-frames\n",
                ns / 1e6, kAgents * kFrames);
    std::printf("  per agent-frame     : %.2f ns\n", perAgentFrame);
    std::printf("  budget check        : at 60 fps a 16.67 ms frame could drive\n"
                "                        %.0f agents on one core\n",
                16.67e6 / perAgentFrame);

    // A machine that never leaves its initial state, or one where every agent
    // died, means the scene is not actually exercising the graph.
    bool healthy = transitions > 0 && respawns > 0 &&
                   occupancy[0] < total && occupancy[0] > 0;
    std::printf("\n  %s\n", healthy
        ? "PASS: agents moved through the graph, reached the terminal state and respawned."
        : "FAIL: the population did not exercise the machine.");
    return healthy ? 0 : 1;
}
