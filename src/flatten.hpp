// ============================================================================
//  AegisScript - hierarchy flattening (Phase 1c)
// ----------------------------------------------------------------------------
//  Turns the state tree the parser built into the flat machine that every
//  later stage already knows how to handle.
//
//  The semantics implemented here are the conventional statechart ones,
//  restricted to what this project needs:
//
//    * Only LEAF states exist at runtime. A composite state is a grouping
//      device; the agent is never "in" it alone, it is in one of its leaves.
//
//    * A composite must nominate exactly one `initial` child. Entering the
//      composite means entering that child -- recursively, until a leaf.
//
//    * A leaf responds to its own transitions plus every transition declared
//      by its ancestors. A leaf's own transition on an event SHADOWS an
//      ancestor's transition on the same event, so the specific case wins
//      over the general one.
//
//    * When a transition fires, exit actions run from the source leaf upward
//      to (but not including) the lowest common ancestor of source and
//      target, then entry actions run from just below that ancestor down to
//      the target leaf. Staying inside a composite therefore does not re-run
//      that composite's entry action.
//
//  PARALLEL REGIONS are handled here too, by product construction. A state
//  marked `parallel` holds orthogonal regions, and the agent is in one leaf of
//  EACH region at once. Rather than carry a set of active states into the
//  runtime, this pass multiplies the regions out: two regions of 2 and 3 leaves
//  become 6 product states, each one an ordinary leaf with its own transitions.
//  An event handled by one region moves only that component and leaves the
//  others alone, which is exactly what orthogonality means.
//
//  The cost is explicit and reported: the state space multiplies rather than
//  adds. The benefit is that everything after this pass is unchanged -- the
//  validator, the planner and the backend never learn that parallelism exists.
//
//  The payoff for the rest of the compiler is that nothing downstream has to
//  know any of this: the validator still runs reachability over a plain
//  digraph, and the planner still measures a plain state x event space.
// ============================================================================
#pragma once

#include "ast.hpp"
#include "validator.hpp"
#include <string>
#include <vector>

namespace aegis {

struct FlattenResult {
    std::vector<Diagnostic> diagnostics;
    bool ok = true;
    int  compositeCount = 0;
    int  leafCount      = 0;
    int  inheritedEdges = 0;   // transitions a leaf got from an ancestor
    int  shadowedEdges  = 0;   // ancestor transitions a leaf overrode

    // parallel regions
    int  parallelCount  = 0;   // parallel composites found
    int  regionCount    = 0;   // regions across all of them
    int  productStates  = 0;   // states the product construction created
    int  productFactor  = 0;   // largest single product (regions multiplied out)
};

// Fills m.states (leaves) and m.initEnterChain from m.nodes (the tree).
FlattenResult flatten(Machine& m);

} // namespace aegis
