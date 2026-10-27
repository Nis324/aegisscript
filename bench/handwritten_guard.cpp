#include "handwritten_guard.hpp"

namespace handwritten {
namespace {

class PatrolState;  class ChaseState;  class CombatState;
class SearchState;  class RestState;   class DeadState;

const State& patrol();
const State& chase();
const State& combat();
const State& search();
const State& rest();
const State& dead();

class PatrolState : public State {
public:
    const char* name() const override { return "Patrol"; }
    void onEnter(GuardAI& m) const override { m.ctx.alertLevel = 0; m.ctx.searchTime = 0; }
    void onUpdate(GuardAI& m) const override { m.ctx.stamina = m.ctx.stamina + 0.5f; }
    const State* handle(GuardAI& m, Event e) const override {
        switch (e) {
            case Event::PlayerSpotted:
                if (m.ctx.stamina > 20.0f)  return &chase();
                if (m.ctx.stamina <= 20.0f) return &rest();
                return nullptr;
            case Event::Attacked: return &combat();
            case Event::Killed:   return &dead();
            default:              return nullptr;
        }
    }
};

class ChaseState : public State {
public:
    const char* name() const override { return "Chase"; }
    void onEnter(GuardAI& m) const override { m.ctx.alertLevel = 2; }
    void onUpdate(GuardAI& m) const override { m.ctx.stamina = m.ctx.stamina - 1.5f; }
    void onExit(GuardAI& m) const override { m.ctx.searchTime = 0; }
    const State* handle(GuardAI& m, Event e) const override {
        (void)m;
        switch (e) {
            case Event::PlayerLost: return &search();
            case Event::Attacked:   return &combat();
            case Event::Exhausted:  return &rest();
            case Event::Killed:     return &dead();
            default:                return nullptr;
        }
    }
};

class CombatState : public State {
public:
    const char* name() const override { return "Combat"; }
    void onEnter(GuardAI& m) const override { m.ctx.alertLevel = 3; }
    void onUpdate(GuardAI& m) const override { m.ctx.stamina = m.ctx.stamina - 2.0f; }
    const State* handle(GuardAI& m, Event e) const override {
        (void)m;
        switch (e) {
            case Event::PlayerLost: return &search();
            case Event::Exhausted:  return &rest();
            case Event::Killed:     return &dead();
            default:                return nullptr;
        }
    }
};

class SearchState : public State {
public:
    const char* name() const override { return "Search"; }
    void onEnter(GuardAI& m) const override { m.ctx.alertLevel = 1; }
    void onUpdate(GuardAI& m) const override { m.ctx.searchTime = m.ctx.searchTime + 1; }
    const State* handle(GuardAI& m, Event e) const override {
        switch (e) {
            case Event::PlayerSpotted: return m.ctx.hasWeapon ? &chase() : nullptr;
            case Event::PlayerLost:    return m.ctx.searchTime > 3 ? &patrol() : nullptr;
            case Event::Attacked:      return &combat();
            case Event::Killed:        return &dead();
            default:                   return nullptr;
        }
    }
};

class RestState : public State {
public:
    const char* name() const override { return "Rest"; }
    void onEnter(GuardAI& m) const override { m.ctx.alertLevel = 0; }
    void onUpdate(GuardAI& m) const override { m.ctx.stamina = m.ctx.stamina + 3.0f; }
    const State* handle(GuardAI& m, Event e) const override {
        switch (e) {
            case Event::Recovered:     return &patrol();
            case Event::PlayerSpotted: return m.ctx.stamina > 40.0f ? &chase() : nullptr;
            case Event::Attacked:      return &combat();
            case Event::Killed:        return &dead();
            default:                   return nullptr;
        }
    }
};

class DeadState : public State {
public:
    const char* name() const override { return "Dead"; }
    void onEnter(GuardAI& m) const override { m.ctx.alertLevel = 0; m.ctx.hasWeapon = false; }
    const State* handle(GuardAI&, Event) const override { return nullptr; }
};

const PatrolState kPatrol;  const ChaseState  kChase;   const CombatState kCombat;
const SearchState kSearch;  const RestState   kRest;    const DeadState   kDead;

const State& patrol() { return kPatrol; }
const State& chase()  { return kChase;  }
const State& combat() { return kCombat; }
const State& search() { return kSearch; }
const State& rest()   { return kRest;   }
const State& dead()   { return kDead;   }

} // namespace

GuardAI::GuardAI() { reset(); }

void GuardAI::reset() {
    ctx = Context{};
    state_ = &patrol();
    state_->onEnter(*this);
}

void GuardAI::update() { state_->onUpdate(*this); }

bool GuardAI::dispatch(Event e) {
    const State* next = state_->handle(*this, e);
    if (!next) return false;
    state_->onExit(*this);
    state_ = next;
    state_->onEnter(*this);
    return true;
}

} // namespace handwritten
