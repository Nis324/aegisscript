#include "naive_guard.hpp"

namespace naive {
namespace {

// One flat record per transition in the whole machine. No indexing structure:
// dispatch walks this list from the top on every event.
struct Row {
    State from;
    Event ev;
    State to;
    int   guard;   // -1 == unconditional
};

const Row kRows[] = {
    { State::Patrol, Event::PlayerSpotted, State::Chase,  0 },
    { State::Patrol, Event::PlayerSpotted, State::Rest,   1 },
    { State::Patrol, Event::Attacked,      State::Combat, -1 },
    { State::Patrol, Event::Killed,        State::Dead,   -1 },

    { State::Chase,  Event::PlayerLost,    State::Search, -1 },
    { State::Chase,  Event::Attacked,      State::Combat, -1 },
    { State::Chase,  Event::Exhausted,     State::Rest,   -1 },
    { State::Chase,  Event::Killed,        State::Dead,   -1 },

    { State::Combat, Event::PlayerLost,    State::Search, -1 },
    { State::Combat, Event::Exhausted,     State::Rest,   -1 },
    { State::Combat, Event::Killed,        State::Dead,   -1 },

    { State::Search, Event::PlayerSpotted, State::Chase,  2 },
    { State::Search, Event::PlayerLost,    State::Patrol, 3 },
    { State::Search, Event::Attacked,      State::Combat, -1 },
    { State::Search, Event::Killed,        State::Dead,   -1 },

    { State::Rest,   Event::Recovered,     State::Patrol, -1 },
    { State::Rest,   Event::PlayerSpotted, State::Chase,  4 },
    { State::Rest,   Event::Attacked,      State::Combat, -1 },
    { State::Rest,   Event::Killed,        State::Dead,   -1 },
};
constexpr int kRowCount = sizeof(kRows) / sizeof(kRows[0]);

const char* const kNames[] = { "Patrol", "Chase", "Combat", "Search", "Rest", "Dead" };

} // namespace

GuardAI::GuardAI() { reset(); }

void GuardAI::reset() {
    ctx = Context{};
    state_ = State::Patrol;
    onEnter(state_);
}

const char* GuardAI::stateName() const { return kNames[static_cast<int>(state_)]; }

bool GuardAI::guard(int id) const {
    switch (id) {
        case 0: return ctx.stamina > 20.0f;
        case 1: return ctx.stamina <= 20.0f;
        case 2: return ctx.hasWeapon;
        case 3: return ctx.searchTime > 3;
        case 4: return ctx.stamina > 40.0f;
        default: return true;
    }
}

void GuardAI::onEnter(State s) {
    switch (s) {
        case State::Patrol: ctx.alertLevel = 0; ctx.searchTime = 0; break;
        case State::Chase:  ctx.alertLevel = 2; break;
        case State::Combat: ctx.alertLevel = 3; break;
        case State::Search: ctx.alertLevel = 1; break;
        case State::Rest:   ctx.alertLevel = 0; break;
        case State::Dead:   ctx.alertLevel = 0; ctx.hasWeapon = false; break;
        default: break;
    }
}

void GuardAI::onExit(State s) {
    if (s == State::Chase) ctx.searchTime = 0;
}

void GuardAI::update() {
    switch (state_) {
        case State::Patrol: ctx.stamina = ctx.stamina + 0.5f; break;
        case State::Chase:  ctx.stamina = ctx.stamina - 1.5f; break;
        case State::Combat: ctx.stamina = ctx.stamina - 2.0f; break;
        case State::Search: ctx.searchTime = ctx.searchTime + 1; break;
        case State::Rest:   ctx.stamina = ctx.stamina + 3.0f; break;
        default: break;
    }
}

bool GuardAI::dispatch(Event e) {
    for (int i = 0; i < kRowCount; ++i) {
        const Row& r = kRows[i];
        if (r.from != state_ || r.ev != e) continue;
        if (r.guard >= 0 && !guard(r.guard)) continue;
        onExit(state_);
        state_ = r.to;
        onEnter(state_);
        return true;
    }
    return false;
}

} // namespace naive
