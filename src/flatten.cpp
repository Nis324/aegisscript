#include "flatten.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace aegis {
namespace {

// A runtime state. For an ordinary machine this is one leaf. Inside a parallel
// composite it is one leaf PER REGION -- the configuration the agent is in.
struct RState {
    std::vector<int> members;      // component leaf node ids, one per region
    int              parallelNode = -1;   // the parallel composite, or -1
    std::string      ident;        // C++-safe name
    std::string      path;         // display name
};

struct Flattener {
    Machine& m;
    FlattenResult res;

    std::vector<RState>           rstates;
    std::map<std::vector<int>,int> configToId;

    explicit Flattener(Machine& mm) : m(mm) {}

    void error(const std::string& code, const std::string& msg, SourceLoc l) {
        res.diagnostics.push_back({Severity::Error, code, msg, l});
        res.ok = false;
    }
    void warn(const std::string& code, const std::string& msg, SourceLoc l) {
        res.diagnostics.push_back({Severity::Warning, code, msg, l});
    }

    // ---------------------------------------------------------- tree ------
    std::string pathOf(int n) const {
        const State& s = m.nodes[n];
        return s.parent < 0 ? s.name : pathOf(s.parent) + "." + s.name;
    }
    std::string identOf(int n) const {
        const State& s = m.nodes[n];
        return s.parent < 0 ? s.name : identOf(s.parent) + "_" + s.name;
    }
    std::vector<int> ancestorsOf(int n) const {      // outermost first, inclusive
        std::vector<int> c;
        for (int i = n; i >= 0; i = m.nodes[i].parent) c.push_back(i);
        std::reverse(c.begin(), c.end());
        return c;
    }
    int lca(int a, int b) const {
        auto ca = ancestorsOf(a), cb = ancestorsOf(b);
        int best = -1;
        for (size_t i = 0; i < ca.size() && i < cb.size() && ca[i] == cb[i]; ++i) best = ca[i];
        return best;
    }
    bool isAncestorOf(int anc, int n) const {
        for (int i = n; i >= 0; i = m.nodes[i].parent) if (i == anc) return true;
        return false;
    }
    // nearest enclosing parallel composite, or -1
    int enclosingParallel(int n) const {
        for (int i = m.nodes[n].parent; i >= 0; i = m.nodes[i].parent)
            if (m.nodes[i].isParallel) return i;
        return -1;
    }
    // nodes from just below `stop` down to `to`, outermost first
    std::vector<int> chainDownTo(int stop, int to) const {
        std::vector<int> d;
        for (int i = to; i >= 0 && i != stop; i = m.nodes[i].parent) d.push_back(i);
        std::reverse(d.begin(), d.end());
        return d;
    }
    // nodes from `from` upward, stopping below `stop`, innermost first
    std::vector<int> chainUpTo(int from, int stop) const {
        std::vector<int> u;
        for (int i = from; i >= 0 && i != stop; i = m.nodes[i].parent) u.push_back(i);
        return u;
    }

    // ------------------------------------------------- initial resolution --
    int initialChildOf(int node) {
        int chosen = -1, count = 0;
        for (int c : m.nodes[node].children)
            if (m.nodes[c].isInitial) { if (count++ == 0) chosen = c; }
        if (count == 0) {
            error("AEG-E016",
                  "composite state '" + pathOf(node) + "' has no 'initial' child; "
                  "entering it has no defined destination",
                  m.nodes[node].loc);
            return m.nodes[node].children.empty() ? -1 : m.nodes[node].children.front();
        }
        if (count > 1)
            error("AEG-E017",
                  "composite state '" + pathOf(node) + "' declares " + std::to_string(count) +
                  " initial children; exactly one is required",
                  m.nodes[node].loc);
        return chosen;
    }

    // descend to the leaf entering `node` lands on, ignoring parallelism
    int defaultLeafIn(int node) {
        int cur = node, guard = 0;
        while (!m.nodes[cur].children.empty()) {
            if (++guard > 64) return cur;
            int c = initialChildOf(cur);
            if (c < 0) return cur;
            cur = c;
        }
        return cur;
    }

    // every leaf descendant of `node` (not crossing into a nested parallel)
    void leafDescendants(int node, std::vector<int>& out) {
        if (m.nodes[node].children.empty()) { out.push_back(node); return; }
        for (int c : m.nodes[node].children) leafDescendants(c, out);
    }

    // ------------------------------------------------ runtime state space --
    void collect(int node) {
        const State& n = m.nodes[node];

        if (n.isParallel) {
            if (n.children.empty()) {
                error("AEG-E022",
                      "parallel state '" + n.path + "' has no regions", n.loc);
                return;
            }
            ++res.parallelCount;
            res.regionCount += static_cast<int>(n.children.size());

            // one leaf set per region
            std::vector<std::vector<int>> regions;
            for (int r : n.children) {
                if (m.nodes[r].isParallel) {
                    error("AEG-E023",
                          "region '" + m.nodes[r].path + "' is itself parallel; nested "
                          "parallel composites are not supported",
                          m.nodes[r].loc);
                    return;
                }
                std::vector<int> ls;
                leafDescendants(r, ls);
                regions.push_back(ls);
            }

            // cartesian product
            std::vector<std::vector<int>> combos{{}};
            for (const auto& region : regions) {
                std::vector<std::vector<int>> next;
                for (const auto& partial : combos)
                    for (int leaf : region) {
                        auto c = partial;
                        c.push_back(leaf);
                        next.push_back(c);
                    }
                combos.swap(next);
            }

            res.productFactor = std::max(res.productFactor, static_cast<int>(combos.size()));
            res.productStates += static_cast<int>(combos.size());

            for (const auto& c : combos) {
                RState rs;
                rs.members      = c;
                rs.parallelNode = node;
                rs.ident        = identOf(node);
                rs.path         = n.path + ".(";
                for (size_t i = 0; i < c.size(); ++i) {
                    rs.ident += "_" + m.nodes[c[i]].name;
                    rs.path  += (i ? "|" : "") + m.nodes[c[i]].name;
                }
                rs.path += ")";
                configToId[c] = static_cast<int>(rstates.size());
                rstates.push_back(rs);
            }
            return;
        }

        if (n.children.empty()) {
            RState rs;
            rs.members = { node };
            rs.ident   = identOf(node);
            rs.path    = n.path;
            configToId[rs.members] = static_cast<int>(rstates.size());
            rstates.push_back(rs);
            return;
        }
        for (int c : n.children) collect(c);
    }

    // the runtime state entering `node` lands in
    int defaultRStateFor(int node) {
        int P = m.nodes[node].isParallel ? node : enclosingParallel(node);
        if (P < 0) {
            std::vector<int> cfg{ defaultLeafIn(node) };
            auto it = configToId.find(cfg);
            return it == configToId.end() ? -1 : it->second;
        }
        // build the configuration: the region containing `node` takes it,
        // every other region takes its own default
        std::vector<int> cfg;
        for (int r : m.nodes[P].children) {
            if (node != P && isAncestorOf(r, node)) cfg.push_back(defaultLeafIn(node));
            else                                    cfg.push_back(defaultLeafIn(r));
        }
        auto it = configToId.find(cfg);
        return it == configToId.end() ? -1 : it->second;
    }

    // chain entered when arriving at runtime state `id` from outside `stop`
    std::vector<int> enterChainFor(int id, int stop) {
        const RState& rs = rstates[id];
        if (rs.parallelNode < 0) return chainDownTo(stop, rs.members[0]);

        std::vector<int> e = chainDownTo(stop, rs.parallelNode);   // ... down to P
        for (int mem : rs.members) {                               // then each region
            auto d = chainDownTo(rs.parallelNode, mem);
            e.insert(e.end(), d.begin(), d.end());
        }
        return e;
    }

    // chain exited when leaving runtime state `id` up to (excluding) `stop`
    std::vector<int> exitChainFor(int id, int stop) {
        const RState& rs = rstates[id];
        if (rs.parallelNode < 0) return chainUpTo(rs.members[0], stop);

        std::vector<int> x;
        for (auto it = rs.members.rbegin(); it != rs.members.rend(); ++it) {
            auto u = chainUpTo(*it, rs.parallelNode);
            x.insert(x.end(), u.begin(), u.end());
        }
        auto up = chainUpTo(rs.parallelNode, stop);
        x.insert(x.end(), up.begin(), up.end());
        return x;
    }

    // ------------------------------------------------------------- main ---
    FlattenResult run() {
        if (m.nodes.empty()) return res;

        for (size_t i = 0; i < m.nodes.size(); ++i) {
            m.nodes[i].path = pathOf(static_cast<int>(i));
            if (m.nodes[i].isComposite()) ++res.compositeCount;
            if (m.nodes[i].isInitial && m.nodes[i].isFinal)
                error("AEG-E024",
                      "state '" + m.nodes[i].path + "' is both initial and final",
                      m.nodes[i].loc);
        }

        // sibling names must be unique
        std::map<std::pair<int,std::string>,int> seen;
        for (size_t i = 0; i < m.nodes.size(); ++i) {
            auto key = std::make_pair(m.nodes[i].parent, m.nodes[i].name);
            if (seen.count(key))
                error("AEG-E003", "duplicate state '" + m.nodes[i].path + "'", m.nodes[i].loc);
            else seen[key] = static_cast<int>(i);
        }

        // ---- build the runtime state space --------------------------------
        for (size_t i = 0; i < m.nodes.size(); ++i)
            if (m.nodes[i].parent < 0) collect(static_cast<int>(i));
        res.leafCount = static_cast<int>(rstates.size());
        if (rstates.empty()) return res;

        // ---- name lookup, innermost scope outward -------------------------
        auto lookup = [&](int from, const std::string& nm) -> int {
            for (int scope = from; scope >= 0; scope = m.nodes[scope].parent)
                for (int c : m.nodes[scope].children)
                    if (m.nodes[c].name == nm) return c;
            for (size_t i = 0; i < m.nodes.size(); ++i)
                if (m.nodes[i].parent < 0 && m.nodes[i].name == nm) return static_cast<int>(i);
            return -1;
        };

        // ---- entry point ---------------------------------------------------
        int rootInitial = -1, rootCount = 0;
        for (size_t i = 0; i < m.nodes.size(); ++i)
            if (m.nodes[i].parent < 0 && m.nodes[i].isInitial)
                if (rootCount++ == 0) rootInitial = static_cast<int>(i);
        const int initialRState = (rootInitial >= 0) ? defaultRStateFor(rootInitial) : -1;

        // ---- effective transitions for one component ----------------------
        // Own transitions, then inherited from ancestors up to (excluding)
        // `stopAt`; a nearer declaration shadows a further one on the same event.
        auto effectiveFor = [&](int leaf, int stopAt,
                                std::vector<std::pair<const Transition*,int>>& out) {
            std::set<std::string> handled;
            for (int n = leaf; n >= 0 && n != stopAt; n = m.nodes[n].parent) {
                std::set<std::string> here;
                for (const auto& t : m.nodes[n].transitions) {
                    if (handled.count(t.event)) { ++res.shadowedEdges; continue; }
                    out.emplace_back(&t, n);
                    here.insert(t.event);
                    if (n != leaf) ++res.inheritedEdges;
                }
                handled.insert(here.begin(), here.end());
            }
        };

        // ---- build the flat machine ---------------------------------------
        m.states.clear();
        m.states.reserve(rstates.size());

        for (size_t id = 0; id < rstates.size(); ++id) {
            const RState& rs = rstates[id];
            const int anchor = rs.members[0];

            State st;
            st.name   = rs.ident;
            st.path   = rs.path;
            st.id     = static_cast<int>(id);
            st.nodeId = anchor;
            st.loc    = m.nodes[anchor].loc;
            st.depth  = m.nodes[anchor].depth;
            st.isInitial = (static_cast<int>(id) == initialRState);

            // actions: a product state ticks every component
            for (int mem : rs.members) {
                const State& n = m.nodes[mem];
                if (n.hasUpdate) {
                    st.onUpdate.insert(st.onUpdate.end(), n.onUpdate.begin(), n.onUpdate.end());
                    st.hasUpdate = true;
                }
            }
            // entry/exit live on the chains, not on the state record
            st.onEnter = m.nodes[anchor].onEnter; st.hasEnter = m.nodes[anchor].hasEnter;
            st.onExit  = m.nodes[anchor].onExit;  st.hasExit  = m.nodes[anchor].hasExit;

            for (int a : ancestorsOf(anchor)) if (m.nodes[a].isFinal) st.isFinal = true;

            // ---- transitions ----------------------------------------------
            std::vector<Transition> eff;

            if (rs.parallelNode < 0) {
                std::vector<std::pair<const Transition*,int>> src;
                effectiveFor(anchor, -1, src);
                for (auto& pr : src) {
                    Transition c = *pr.first;
                    c.declaredBy = pr.second;
                    eff.push_back(c);
                }
            } else {
                const int P = rs.parallelNode;
                // one region at a time, stopping below the parallel
                std::map<std::string,int> eventOwner;   // event -> region index
                for (size_t ri = 0; ri < rs.members.size(); ++ri) {
                    std::vector<std::pair<const Transition*,int>> src;
                    effectiveFor(rs.members[ri], P, src);
                    for (auto& pr : src) {
                        auto owner = eventOwner.find(pr.first->event);
                        if (owner != eventOwner.end() && owner->second != static_cast<int>(ri)) {
                            error("AEG-E025",
                                  "event '" + pr.first->event + "' is handled by more than one "
                                  "region of parallel state '" + m.nodes[P].path +
                                  "'; simultaneous cross-region transitions are not supported",
                                  pr.first->loc);
                            continue;
                        }
                        eventOwner[pr.first->event] = static_cast<int>(ri);
                        Transition c = *pr.first;
                        c.declaredBy = pr.second;
                        c.exitNodes.push_back(static_cast<int>(ri));   // region index, decoded below
                        eff.push_back(c);
                    }
                }
                // transitions on the parallel itself and above: they leave it
                std::vector<std::pair<const Transition*,int>> outer;
                {
                    std::set<std::string> handled;
                    for (const auto& kv : eventOwner) handled.insert(kv.first);
                    for (int n = P; n >= 0; n = m.nodes[n].parent) {
                        std::set<std::string> here;
                        for (const auto& t : m.nodes[n].transitions) {
                            if (handled.count(t.event)) { ++res.shadowedEdges; continue; }
                            outer.emplace_back(&t, n);
                            here.insert(t.event);
                            ++res.inheritedEdges;
                        }
                        handled.insert(here.begin(), here.end());
                    }
                }
                for (auto& pr : outer) {
                    Transition c = *pr.first;
                    c.declaredBy = pr.second;
                    eff.push_back(c);            // no region marker: exits the parallel
                }
            }

            st.transitions = eff;
            m.states.push_back(st);
        }

        // ---- resolve targets and build the chains --------------------------
        for (size_t id = 0; id < m.states.size(); ++id) {
            State& st = m.states[id];
            const RState& rs = rstates[id];

            for (auto& t : st.transitions) {
                const bool regional = !t.exitNodes.empty() && rs.parallelNode >= 0;
                const int  regionIdx = regional ? t.exitNodes[0] : -1;
                t.exitNodes.clear();
                t.enterNodes.clear();

                int tgtNode = lookup(t.declaredBy >= 0 ? t.declaredBy : rs.members[0], t.target);
                if (tgtNode < 0) { t.targetId = -1; continue; }

                int destId = -1;
                if (regional && isAncestorOf(rs.parallelNode, tgtNode)) {
                    // stays inside the parallel: swap one component
                    auto cfg = rs.members;
                    cfg[regionIdx] = defaultLeafIn(tgtNode);
                    auto it = configToId.find(cfg);
                    destId = (it == configToId.end()) ? -1 : it->second;

                    if (destId >= 0) {
                        int src = rs.members[regionIdx], dst = cfg[regionIdx];
                        if (src == dst) {
                            t.exitNodes  = { src };
                            t.enterNodes = { src };
                        } else {
                            int join = lca(src, dst);
                            t.exitNodes  = chainUpTo(src, join);
                            t.enterNodes = chainDownTo(join, dst);
                        }
                    }
                } else {
                    destId = defaultRStateFor(tgtNode);
                    if (destId >= 0) {
                        if (destId == static_cast<int>(id)) {
                            t.exitNodes  = exitChainFor(static_cast<int>(id), -1);
                            t.enterNodes = enterChainFor(destId, -1);
                        } else {
                            int a = rs.parallelNode >= 0 ? rs.parallelNode : rs.members[0];
                            int b = rstates[destId].parallelNode >= 0
                                        ? rstates[destId].parallelNode
                                        : rstates[destId].members[0];
                            int join = lca(a, b);
                            t.exitNodes  = exitChainFor(static_cast<int>(id), join);
                            t.enterNodes = enterChainFor(destId, join);
                        }
                    }
                }

                t.targetId = destId;
                if (destId >= 0) t.target = rstates[destId].ident;
            }
        }

        // ---- reset() entry chain -------------------------------------------
        m.initEnterChain.clear();
        if (initialRState >= 0) m.initEnterChain = enterChainFor(initialRState, -1);

        // ---- advice ---------------------------------------------------------
        for (size_t i = 0; i < m.nodes.size(); ++i) {
            const State& n = m.nodes[i];
            if (!n.isComposite()) continue;
            if (!n.isParallel && n.children.size() == 1)
                warn("AEG-W005",
                     "composite state '" + n.path + "' has a single child; the nesting "
                     "adds a level without adding a choice", n.loc);
            if (n.isParallel && n.children.size() == 1)
                warn("AEG-W008",
                     "parallel state '" + n.path + "' has only one region; it behaves as an "
                     "ordinary composite", n.loc);
            if (n.hasUpdate && !n.isParallel)
                warn("AEG-W006",
                     "composite state '" + n.path + "' has an 'on update' block; only leaf "
                     "states tick, so it will never run", n.loc);
        }
        if (res.productFactor >= 64)
            warn("AEG-W009",
                 "product construction produced " + std::to_string(res.productFactor) +
                 " states from one parallel composite; the state space multiplies rather "
                 "than adds", m.loc);

        return res;
    }
};

} // namespace

FlattenResult flatten(Machine& m) { return Flattener(m).run(); }

} // namespace aegis
