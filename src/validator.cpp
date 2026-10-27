#include "validator.hpp"

#include <algorithm>
#include <queue>
#include <set>
#include <sstream>

namespace aegis {

const char* typeName(VarType t) {
    switch (t) {
        case VarType::Int:   return "int";
        case VarType::Float: return "float";
        case VarType::Bool:  return "bool";
        default:             return "<unknown>";
    }
}

void Validator::error(const std::string& code, const std::string& msg, SourceLoc l) {
    diags_.push_back({Severity::Error, code, msg, l});
}
void Validator::warn(const std::string& code, const std::string& msg, SourceLoc l) {
    diags_.push_back({Severity::Warning, code, msg, l});
}

int Validator::errorCount() const {
    int n = 0;
    for (const auto& d : diags_) if (d.severity == Severity::Error) ++n;
    return n;
}
int Validator::warningCount() const {
    int n = 0;
    for (const auto& d : diags_) if (d.severity == Severity::Warning) ++n;
    return n;
}

// ---------------------------------------------------------------------------
bool Validator::run() {
    buildSymbolTables();
    resolveReferences();
    typeCheck();
    checkDeterminism();
    analyseGraph();
    collectFacts();
    return errorCount() == 0;
}

// -------------------------------------------------------- symbol tables ----
void Validator::buildSymbolTables() {
    if (m_.states.empty())
        error("AEG-E015", "machine '" + m_.name + "' declares no states", m_.loc);

    for (size_t i = 0; i < m_.context.size(); ++i) {
        auto& v = m_.context[i];
        if (m_.varIndex.count(v.name))
            error("AEG-E005", "duplicate context variable '" + v.name + "'", v.loc);
        else
            m_.varIndex[v.name] = static_cast<int>(i);
    }

    for (size_t i = 0; i < m_.actions.size(); ++i) {
        const auto& a = m_.actions[i];
        if (m_.actionIndex.count(a.name))
            error("AEG-E018", "duplicate engine action '" + a.name + "'", a.loc);
        else
            m_.actionIndex[a.name] = static_cast<int>(i);
        if (m_.varIndex.count(a.name))
            error("AEG-E018",
                  "engine action '" + a.name + "' collides with a context variable", a.loc);
    }

    for (size_t i = 0; i < m_.events.size(); ++i) {
        const auto& e = m_.events[i];
        if (m_.eventIndex.count(e))
            error("AEG-E004", "duplicate event '" + e + "'", m_.loc);
        else
            m_.eventIndex[e] = static_cast<int>(i);
    }

    int initialCount = 0;
    for (size_t i = 0; i < m_.states.size(); ++i) {
        auto& s = m_.states[i];
        s.id = static_cast<int>(i);
        if (m_.stateIndex.count(s.name))
            error("AEG-E003", "duplicate state '" + s.name + "'", s.loc);
        else
            m_.stateIndex[s.name] = static_cast<int>(i);
        if (s.isInitial) ++initialCount;
        if (s.isFinal)   facts_.hasFinalStates = true;
    }

    if (!m_.states.empty()) {
        if (initialCount == 0)
            error("AEG-E001",
                  "machine '" + m_.name + "' has no 'initial state'; the FSM has no entry point",
                  m_.loc);
        else if (initialCount > 1)
            error("AEG-E002",
                  "machine '" + m_.name + "' declares " + std::to_string(initialCount) +
                  " initial states; exactly one entry point is required",
                  m_.loc);
    }
}

// -------------------------------------------------------- reference res ----
void Validator::resolveReferences() {
    for (auto& s : m_.states) {
        for (auto& t : s.transitions) {
            auto ei = m_.eventIndex.find(t.event);
            if (ei == m_.eventIndex.end())
                error("AEG-E007",
                      "state '" + s.name + "' reacts to undeclared event '" + t.event +
                      "'; add it to the events block",
                      t.loc);
            else
                t.eventId = ei->second;

            auto si = m_.stateIndex.find(t.target);
            if (si == m_.stateIndex.end())
                error("AEG-E006",
                      "transition from '" + s.name + "' targets undefined state '" + t.target + "'",
                      t.loc);
            else
                t.targetId = si->second;
        }
    }

    // engine actions declared but never called
    {
        std::set<std::string> called;
        for (const auto& n : m_.nodes)
            for (const Block* bl : {&n.onEnter, &n.onUpdate, &n.onExit})
                for (const auto& st : *bl)
                    if (st.kind == StmtKind::Call) called.insert(st.target);
        for (const auto& a : m_.actions)
            if (!called.count(a.name))
                warn("AEG-W007",
                     "engine action '" + a.name + "' is declared but never called",
                     a.loc);
    }

    // dead events: declared but never consumed anywhere
    std::set<std::string> used;
    for (const auto& s : m_.states)
        for (const auto& t : s.transitions)
            used.insert(t.event);
    for (const auto& e : m_.events)
        if (!used.count(e))
            warn("AEG-W001",
                 "event '" + e + "' is declared but never handled by any state",
                 m_.loc);
}

// ------------------------------------------------------------ type pass ----
VarType Validator::inferExpr(Expr* e) {
    if (!e) return VarType::Unknown;

    switch (e->kind) {
        case ExprKind::IntLit:   return e->type = VarType::Int;
        case ExprKind::FloatLit: return e->type = VarType::Float;
        case ExprKind::BoolLit:  return e->type = VarType::Bool;

        case ExprKind::VarRef: {
            auto it = m_.varIndex.find(e->name);
            if (it == m_.varIndex.end()) {
                error("AEG-E008", "undefined identifier '" + e->name + "'", e->loc);
                return e->type = VarType::Unknown;
            }
            return e->type = m_.context[it->second].type;
        }

        case ExprKind::Unary: {
            VarType t = inferExpr(e->lhs);
            if (e->uop == UnOp::Not) {
                if (t != VarType::Bool && t != VarType::Unknown)
                    error("AEG-E009", std::string("operator '!' expects bool, got ") + typeName(t), e->loc);
                return e->type = VarType::Bool;
            }
            if (t == VarType::Bool)
                error("AEG-E009", "unary '-' cannot be applied to bool", e->loc);
            return e->type = t;
        }

        case ExprKind::Binary: {
            VarType a = inferExpr(e->lhs);
            VarType b = inferExpr(e->rhs);
            switch (e->bop) {
                case BinOp::And: case BinOp::Or:
                    if ((a != VarType::Bool && a != VarType::Unknown) ||
                        (b != VarType::Bool && b != VarType::Unknown))
                        error("AEG-E009", "logical operator requires bool operands", e->loc);
                    return e->type = VarType::Bool;

                case BinOp::Lt: case BinOp::Le: case BinOp::Gt: case BinOp::Ge:
                    if (a == VarType::Bool || b == VarType::Bool)
                        error("AEG-E009", "relational operator cannot compare bool", e->loc);
                    return e->type = VarType::Bool;

                case BinOp::Eq: case BinOp::Ne:
                    if (a != VarType::Unknown && b != VarType::Unknown &&
                        ((a == VarType::Bool) != (b == VarType::Bool)))
                        error("AEG-E009",
                              std::string("cannot compare ") + typeName(a) + " with " + typeName(b),
                              e->loc);
                    return e->type = VarType::Bool;

                default: // arithmetic
                    if (a == VarType::Bool || b == VarType::Bool)
                        error("AEG-E009", "arithmetic operator cannot be applied to bool", e->loc);
                    return e->type = (a == VarType::Float || b == VarType::Float)
                                     ? VarType::Float : VarType::Int;
            }
        }
    }
    return VarType::Unknown;
}

void Validator::checkBlock(Block& b, const std::string& where) {
    for (auto& st : b) {
        // ---- engine action call -----------------------------------------
        if (st.kind == StmtKind::Call) {
            auto ai = m_.actionIndex.find(st.target);
            if (ai == m_.actionIndex.end()) {
                error("AEG-E019",
                      "call to undeclared engine action '" + st.target + "' in " + where +
                      "; add it to the actions block",
                      st.loc);
                for (auto* a : st.args) inferExpr(a);
                continue;
            }
            st.actionId = ai->second;
            const ActionDecl& decl = m_.actions[ai->second];

            if (st.args.size() != decl.params.size()) {
                error("AEG-E020",
                      "engine action '" + decl.name + "' takes " +
                      std::to_string(decl.params.size()) + " argument(s), " +
                      std::to_string(st.args.size()) + " given",
                      st.loc);
            }
            const size_t n = std::min(st.args.size(), decl.params.size());
            for (size_t i = 0; i < n; ++i) {
                VarType got = inferExpr(st.args[i]);
                VarType want = decl.params[i];
                bool ok = (got == want) ||
                          (want == VarType::Float && got == VarType::Int) ||
                          got == VarType::Unknown;
                if (!ok)
                    error("AEG-E021",
                          "argument " + std::to_string(i + 1) + " of '" + decl.name +
                          "' is " + typeName(got) + ", expected " + typeName(want),
                          st.loc);
            }
            for (size_t i = n; i < st.args.size(); ++i) inferExpr(st.args[i]);
            continue;
        }

        // ---- assignment --------------------------------------------------
        auto it = m_.varIndex.find(st.target);
        if (it == m_.varIndex.end()) {
            error("AEG-E014",
                  "assignment to undefined context variable '" + st.target + "' in " + where,
                  st.loc);
            inferExpr(st.value);
            continue;
        }
        VarType lhs = m_.context[it->second].type;
        VarType rhs = inferExpr(st.value);
        bool ok = (lhs == rhs) ||
                  (lhs == VarType::Float && rhs == VarType::Int) ||
                  rhs == VarType::Unknown;
        if (!ok)
            error("AEG-E009",
                  "cannot assign " + std::string(typeName(rhs)) + " to " +
                  typeName(lhs) + " '" + st.target + "'",
                  st.loc);
    }
}

void Validator::typeCheck() {
    for (auto& v : m_.context)
        if (v.init) {
            VarType rhs = inferExpr(v.init);
            bool ok = (v.type == rhs) ||
                      (v.type == VarType::Float && rhs == VarType::Int) ||
                      rhs == VarType::Unknown;
            if (!ok)
                error("AEG-E009",
                      "initialiser for '" + v.name + "' is " + typeName(rhs) +
                      ", declared " + typeName(v.type),
                      v.loc);
        }

    for (auto& s : m_.states) {
        checkBlock(s.onEnter,  "state '" + s.name + "' on enter");
        checkBlock(s.onUpdate, "state '" + s.name + "' on update");
        checkBlock(s.onExit,   "state '" + s.name + "' on exit");

        for (auto& t : s.transitions) {
            if (!t.guard) continue;
            VarType g = inferExpr(t.guard);
            if (g != VarType::Bool && g != VarType::Unknown)
                error("AEG-E010",
                      "guard on '" + s.name + " -> " + t.target + "' must be bool, got " + typeName(g),
                      t.loc);
        }
    }
}

// ---------------------------------------------------------- determinism ----
void Validator::checkDeterminism() {
    for (auto& s : m_.states) {
        std::map<std::string, int> unguarded;   // event -> count
        std::map<std::string, int> guarded;
        for (auto& t : s.transitions) {
            if (t.guard) guarded[t.event]++;
            else         unguarded[t.event]++;
        }
        for (const auto& kv : unguarded) {
            if (kv.second > 1) {
                facts_.deterministic = false;
                error("AEG-E011",
                      "state '" + s.name + "' has " + std::to_string(kv.second) +
                      " unconditional transitions on event '" + kv.first +
                      "'; the FSM is non-deterministic",
                      s.loc);
            }
        }
        for (const auto& kv : guarded) {
            if (!unguarded.count(kv.first))
                warn("AEG-W002",
                     "every transition on '" + kv.first + "' in state '" + s.name +
                     "' is guarded; the event is silently dropped when all guards are false",
                     s.loc);
        }
    }
}

// --------------------------------------------------------- graph theory ----
void Validator::analyseGraph() {
    const int n = static_cast<int>(m_.states.size());
    if (n == 0) return;

    int start = -1;
    for (const auto& s : m_.states)
        if (s.isInitial) { start = s.id; break; }
    if (start < 0) return;   // AEG-E001 already reported

    // --- forward reachability (BFS from the entry state) --------------------
    std::vector<char> seen(n, 0);
    std::queue<int> q;
    q.push(start);
    seen[start] = 1;
    while (!q.empty()) {
        int cur = q.front(); q.pop();
        for (const auto& t : m_.states[cur].transitions) {
            if (t.targetId >= 0 && !seen[t.targetId]) {
                seen[t.targetId] = 1;
                q.push(t.targetId);
            }
        }
    }
    for (auto& s : m_.states) {
        s.reachable = seen[s.id] != 0;
        if (!s.reachable)
            error("AEG-E013",
                  "state '" + s.name + "' is unreachable from initial state '" +
                  m_.states[start].name + "'",
                  s.loc);
    }

    // --- sinkhole detection (reachable, non-final, out-degree 0) ------------
    for (auto& s : m_.states) {
        if (!s.reachable) continue;
        if (s.transitions.empty() && !s.isFinal) {
            ++facts_.sinkholeCount;
            error("AEG-E012",
                  "state '" + s.name + "' is a sinkhole: it is reachable, has no outgoing "
                  "transitions and is not declared final; the NPC would freeze here",
                  s.loc);
        }
    }

    // --- liveness: can every reachable state still reach a final state? -----
    if (facts_.hasFinalStates) {
        std::vector<std::vector<int>> rev(n);
        for (const auto& s : m_.states)
            for (const auto& t : s.transitions)
                if (t.targetId >= 0) rev[t.targetId].push_back(s.id);

        std::vector<char> co(n, 0);
        std::queue<int> rq;
        for (const auto& s : m_.states)
            if (s.isFinal) { co[s.id] = 1; rq.push(s.id); }
        while (!rq.empty()) {
            int cur = rq.front(); rq.pop();
            for (int p : rev[cur])
                if (!co[p]) { co[p] = 1; rq.push(p); }
        }
        for (const auto& s : m_.states)
            if (s.reachable && !co[s.id])
                warn("AEG-W004",
                     "no path from state '" + s.name + "' to any final state; "
                     "the NPC can be trapped in a terminal cycle",
                     s.loc);
    }

    // --- unread context variables ------------------------------------------
    std::set<std::string> read;
    struct Walk {
        std::set<std::string>& out;
        void operator()(Expr* e) {
            if (!e) return;
            if (e->kind == ExprKind::VarRef) out.insert(e->name);
            (*this)(e->lhs);
            (*this)(e->rhs);
        }
    } walk{read};

    for (auto& s : m_.states) {
        for (auto& t : s.transitions) walk(t.guard);
        for (Block* b : {&s.onEnter, &s.onUpdate, &s.onExit})
            for (auto& st : *b) {
                walk(st.value);
                for (auto* a : st.args) walk(a);
            }
    }
    for (const auto& v : m_.context)
        if (!read.count(v.name))
            warn("AEG-W003",
                 "context variable '" + v.name + "' is written but never read",
                 v.loc);
}

// ---------------------------------------------------------------- facts ----
void Validator::collectFacts() {
    facts_.stateCount = static_cast<int>(m_.states.size());
    facts_.eventCount = static_cast<int>(m_.events.size());
    facts_.reachableStates = 0;
    for (const auto& s : m_.states) {
        if (s.reachable) ++facts_.reachableStates;
        int fan = static_cast<int>(s.transitions.size());
        facts_.maxFanOut = std::max(facts_.maxFanOut, fan);
        facts_.transitionCount += fan;
        for (const auto& t : s.transitions)
            if (t.guard) ++facts_.guardedCount;
    }
    const long long cells =
        static_cast<long long>(facts_.reachableStates) * facts_.eventCount;
    facts_.density = cells > 0
                   ? static_cast<double>(facts_.transitionCount) / static_cast<double>(cells)
                   : 0.0;
}

} // namespace aegis
