// ============================================================================
//  Hand-written baseline A: the classic GoF State pattern.
//
//  This is what the AegisScript proposal is measured against: the same GuardAI
//  machine as examples/guard.aegis, written the way a competent C++ developer
//  would write it by hand, with one class per state and virtual dispatch.
//
//  Written to be *fair*, not to be slow: no allocation in the hot path, states
//  are singletons, and the code is idiomatic rather than deliberately naive.
// ============================================================================
#pragma once

#include <cstdint>

namespace handwritten {

enum class Event : uint8_t {
    PlayerSpotted = 0, PlayerLost, Attacked, Exhausted, Recovered, Killed, COUNT
};

struct Context {
    int32_t alertLevel = 0;
    float   stamina    = 100.0f;
    bool    hasWeapon  = true;
    int32_t searchTime = 0;
};

class GuardAI;

class State {
public:
    virtual ~State() = default;
    virtual const char* name() const = 0;
    virtual void onEnter(GuardAI&) const {}
    virtual void onUpdate(GuardAI&) const {}
    virtual void onExit(GuardAI&) const {}
    // returns the next state, or nullptr when the event is ignored
    virtual const State* handle(GuardAI&, Event) const = 0;
};

class GuardAI {
public:
    GuardAI();
    void reset();
    bool dispatch(Event e);
    void update();
    const char* stateName() const { return state_->name(); }
    const State* state() const { return state_; }

    Context ctx;

private:
    const State* state_ = nullptr;
    friend class PatrolState;
    friend class ChaseState;
    friend class CombatState;
    friend class SearchState;
    friend class RestState;
    friend class DeadState;
    void setState(const State* s) { state_ = s; }
};

} // namespace handwritten
