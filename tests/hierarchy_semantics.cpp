// ============================================================================
//  Hierarchy semantics test
//
//  The interesting part of hierarchical states is not that they parse, it is
//  that entry and exit actions fire the right number of times. These are the
//  four cases that distinguish a correct implementation from one that just
//  flattens names:
//
//    1. Entering a composite from outside runs the composite's entry action
//       AND the initial child's, in that order.
//    2. Moving between two children of the same composite runs only the
//       children's actions. The composite is NOT re-entered.
//    3. A transition inherited from the composite, targeting a sibling child,
//       stays inside the composite -- so the composite's exit must not run.
//    4. A child's own transition SHADOWS the composite's on the same event.
// ============================================================================

#include "KnightAI.h"

#include <cstdio>
#include <string>

using namespace aegis_gen;

namespace {

int failures = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++failures;
}

void checkEq(int got, int want, const std::string& what) {
    bool ok = (got == want);
    std::printf("  [%s] %-58s (got %d, want %d)\n",
                ok ? "PASS" : "FAIL", what.c_str(), got, want);
    if (!ok) ++failures;
}

} // namespace

int main() {
    std::printf("\nHierarchy semantics (examples/knight.aegis)\n\n");

    // ---- 1. entering a composite runs outer entry, then inner ------------
    {
        KnightAI k;
        check(k.state() == KnightAIState::Idle, "starts in Idle");
        k.dispatch(KnightAIEvent::SpotEnemy);
        check(k.state() == KnightAIState::Combat_Windup,
              "SpotEnemy -> Combat lands in the initial child, Combat.Windup");
        checkEq(k.ctx.rage, 1,  "Combat entry action ran (rage = 1)");
        checkEq(k.ctx.phase, 1, "Windup entry action ran after it (phase = 1)");
    }

    // ---- 2. sibling move does not re-enter the composite -----------------
    {
        KnightAI k;
        k.dispatch(KnightAIEvent::SpotEnemy);
        k.ctx.rage = 99;                     // poison the composite's entry marker
        k.dispatch(KnightAIEvent::Swing);    // Windup -> Strike, same parent
        check(k.state() == KnightAIState::Combat_Strike, "Swing -> Combat.Strike");
        checkEq(k.ctx.rage, 99,  "Combat was NOT re-entered on a sibling move");
        checkEq(k.ctx.phase, 2,  "Strike entry action ran (phase = 2)");
    }

    // ---- 3. inherited transition staying inside the composite ------------
    {
        KnightAI k;
        k.dispatch(KnightAIEvent::SpotEnemy);
        k.ctx.rage = 77;
        // Staggered is declared on Combat, not on Windup: Windup inherits it.
        // Its target, Recovering, is also inside Combat -- so Combat must not exit.
        k.dispatch(KnightAIEvent::Staggered);
        check(k.state() == KnightAIState::Combat_Recovering,
              "inherited Staggered fires from Combat.Windup");
        checkEq(k.ctx.rage, 77, "Combat exit action did NOT run (stayed inside)");
        checkEq(k.ctx.phase, 3, "Recovering entry action ran (phase = 3)");
    }

    // ---- 4. a child's own transition shadows the inherited one -----------
    {
        KnightAI k;
        k.dispatch(KnightAIEvent::SpotEnemy);
        k.dispatch(KnightAIEvent::Swing);    // now in Strike
        check(k.state() == KnightAIState::Combat_Strike, "in Combat.Strike");
        // Combat says Staggered -> Recovering. Strike overrides it: -> Idle.
        k.dispatch(KnightAIEvent::Staggered);
        check(k.state() == KnightAIState::Idle,
              "Strike's own Staggered shadows the one inherited from Combat");
        checkEq(k.ctx.rage, 0, "leaving the composite ran Combat's exit action");
    }

    // ---- 5. inherited transition out of the composite --------------------
    {
        KnightAI k;
        k.dispatch(KnightAIEvent::SpotEnemy);
        k.dispatch(KnightAIEvent::Killed);   // Killed declared on Combat
        check(k.state() == KnightAIState::Dead, "inherited Killed reaches Dead");
        checkEq(k.ctx.rage, 0, "Combat exit then Dead entry both ran");
        check(k.ctx.shielded == false, "Dead entry action ran (shielded = false)");
    }

    // ---- 6. terminal state ignores everything ----------------------------
    {
        KnightAI k;
        k.dispatch(KnightAIEvent::Killed);
        check(k.state() == KnightAIState::Dead, "Idle -> Dead");
        bool any = false;
        for (int e = 0; e < static_cast<int>(KnightAIEvent::COUNT); ++e)
            any |= k.dispatch(static_cast<KnightAIEvent>(e));
        check(!any, "final state accepts no further transitions");
    }

    std::printf("\n  %s\n\n", failures == 0
        ? "all hierarchy semantics correct"
        : "SEMANTICS BROKEN");
    return failures == 0 ? 0 : 1;
}
