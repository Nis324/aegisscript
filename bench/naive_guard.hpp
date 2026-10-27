// ============================================================================
//  Hand-written baseline B: the naive fixed-shape generator.
//
//  This is the shape the --metrics "naive" column models: one flat list of
//  every transition in the machine, scanned linearly on each event. It is what
//  a straightforward FSM generator emits when it does not look at the shape of
//  the graph at all.
//
//  Same GuardAI machine, same semantics, same actions.
// ============================================================================
#pragma once

#include <cstdint>

namespace naive {

enum class Event : uint8_t {
    PlayerSpotted = 0, PlayerLost, Attacked, Exhausted, Recovered, Killed, COUNT
};

enum class State : uint8_t {
    Patrol = 0, Chase, Combat, Search, Rest, Dead, COUNT
};

struct Context {
    int32_t alertLevel = 0;
    float   stamina    = 100.0f;
    bool    hasWeapon  = true;
    int32_t searchTime = 0;
};

class GuardAI {
public:
    GuardAI();
    void reset();
    bool dispatch(Event e);
    void update();
    State state() const { return state_; }
    const char* stateName() const;

    Context ctx;

private:
    State state_ = State::Patrol;
    void onEnter(State s);
    void onExit(State s);
    bool guard(int id) const;
};

} // namespace naive
