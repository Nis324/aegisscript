// ============================================================================
//  Parallel region semantics test
//
//  Product construction is only correct if orthogonality actually holds: an
//  event handled by one region must move that region and leave the others
//  exactly where they were, and the enclosing parallel state must not be
//  re-entered while you stay inside it.
//
//  These are the five properties that distinguish a correct product
//  construction from one that merely produces the right number of states.
// ============================================================================

#include "MechPilot.h"

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
    std::printf("  [%s] %-56s (got %d, want %d)\n",
                ok ? "PASS" : "FAIL", what.c_str(), got, want);
    if (!ok) ++failures;
}

} // namespace

int main() {
    std::printf("\nParallel region semantics (examples/mech.aegis)\n\n");

    // ---- 1. entering a parallel enters every region -----------------------
    {
        MechPilot m;
        check(m.state() == MechPilotState::Offline, "starts Offline");
        m.dispatch(MechPilotEvent::Boot);
        check(m.state() == MechPilotState::Active_Walking_Holstered,
              "Boot lands in the product of both regions' initial states");
        checkEq(m.ctx.alert, 1, "the parallel state's own entry action ran");
        checkEq(m.ctx.gear,  1, "the Movement region entered Walking");
        check(m.ctx.armed == false, "the Weapon region entered Holstered");
    }

    // ---- 2. one region moves, the other does not --------------------------
    {
        MechPilot m;
        m.dispatch(MechPilotEvent::Boot);
        m.ctx.gear = 77;                          // poison the movement marker
        m.dispatch(MechPilotEvent::Draw);         // a Weapon-region event
        check(m.state() == MechPilotState::Active_Walking_Drawn,
              "Draw moves only the Weapon component");
        checkEq(m.ctx.gear, 77, "the Movement region was NOT re-entered");
        check(m.ctx.armed == true, "the Weapon region entered Drawn");
    }

    // ---- 3. and symmetrically, the other way round ------------------------
    {
        MechPilot m;
        m.dispatch(MechPilotEvent::Boot);
        m.dispatch(MechPilotEvent::Draw);         // now (Walking | Drawn)
        m.ctx.armed = false;                      // poison the weapon marker
        m.dispatch(MechPilotEvent::Sprint);       // a Movement-region event
        check(m.state() == MechPilotState::Active_Running_Drawn,
              "Sprint moves only the Movement component");
        check(m.ctx.armed == false, "the Weapon region was NOT re-entered");
        checkEq(m.ctx.gear, 2, "the Movement region entered Running");
    }

    // ---- 4. the parallel is not re-entered while you stay inside ----------
    {
        MechPilot m;
        m.dispatch(MechPilotEvent::Boot);
        m.ctx.alert = 99;                         // poison the parallel marker
        m.dispatch(MechPilotEvent::Sprint);
        m.dispatch(MechPilotEvent::Draw);
        m.dispatch(MechPilotEvent::Tire);
        check(m.state() == MechPilotState::Active_Walking_Drawn,
              "four moves inside the parallel land in the right product state");
        checkEq(m.ctx.alert, 99, "the parallel's entry action never re-ran");
    }

    // ---- 5. a transition on the parallel leaves it from any product -------
    {
        const MechPilotEvent path[][2] = {
            { MechPilotEvent::Boot,   MechPilotEvent::Destroy },
            { MechPilotEvent::Draw,   MechPilotEvent::Destroy },
            { MechPilotEvent::Sprint, MechPilotEvent::Destroy },
        };
        bool all = true;
        for (const auto& p : path) {
            MechPilot m;
            m.dispatch(MechPilotEvent::Boot);
            m.dispatch(p[0]);
            m.dispatch(p[1]);
            all &= (m.state() == MechPilotState::Wrecked) && (m.ctx.alert == 0);
        }
        check(all, "Destroy, declared once on the parallel, exits from every product state");
    }

    // ---- 6. every product state is reachable ------------------------------
    {
        MechPilot m;
        m.dispatch(MechPilotEvent::Boot);
        bool a = m.state() == MechPilotState::Active_Walking_Holstered;
        m.dispatch(MechPilotEvent::Sprint);
        bool b = m.state() == MechPilotState::Active_Running_Holstered;
        m.dispatch(MechPilotEvent::Draw);
        bool c = m.state() == MechPilotState::Active_Running_Drawn;
        m.dispatch(MechPilotEvent::Tire);
        bool d = m.state() == MechPilotState::Active_Walking_Drawn;
        check(a && b && c && d, "all four product states are reachable in one walk");
    }

    // ---- 7. both components tick ------------------------------------------
    {
        MechPilot m;
        m.dispatch(MechPilotEvent::Boot);
        m.dispatch(MechPilotEvent::Sprint);       // Running has an update block
        int before = m.ctx.hull;
        m.update();
        checkEq(m.ctx.hull, before - 1, "the Running component's update block ran");
    }

    std::printf("\n  %s\n\n", failures == 0
        ? "all parallel semantics correct"
        : "SEMANTICS BROKEN");
    return failures == 0 ? 0 : 1;
}
