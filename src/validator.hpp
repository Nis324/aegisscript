// ============================================================================
//  AegisScript - Semantic Validator (Phase 2 / Middle-end)
//  Graph-theoretic and type-theoretic analysis over the AST.
// ============================================================================
#pragma once

#include "ast.hpp"
#include <string>
#include <vector>

namespace aegis {

enum class Severity { Error, Warning, Note };

struct Diagnostic {
    Severity    severity;
    std::string code;        // e.g. "AEG-E012"
    std::string message;
    SourceLoc   loc;
};

// Graph facts produced by the analysis pass and consumed by the synthesis
// strategy selector (see layout.hpp).
struct GraphFacts {
    int  stateCount        = 0;
    int  reachableStates   = 0;
    int  eventCount        = 0;
    int  transitionCount   = 0;
    int  guardedCount      = 0;
    int  sinkholeCount     = 0;
    int  maxFanOut         = 0;     // largest out-degree of any state
    double density         = 0.0;   // transitions / (reachableStates * events)
    bool  deterministic    = true;
    bool  hasFinalStates   = false;
};

class Validator {
public:
    explicit Validator(Machine& m) : m_(m) {}

    // Runs the full pass. Returns false if any Error-severity diagnostic fired.
    bool run();

    const std::vector<Diagnostic>& diagnostics() const { return diags_; }
    const GraphFacts& facts() const { return facts_; }
    int errorCount()   const;
    int warningCount() const;

private:
    Machine& m_;
    std::vector<Diagnostic> diags_;
    GraphFacts facts_;

    void error(const std::string& code, const std::string& msg, SourceLoc l);
    void warn (const std::string& code, const std::string& msg, SourceLoc l);

    // passes
    void buildSymbolTables();
    void resolveReferences();
    void typeCheck();
    void checkDeterminism();
    void analyseGraph();          // reachability + sinkhole + liveness
    void collectFacts();

    VarType inferExpr(Expr* e);
    void    checkBlock(Block& b, const std::string& where);
};

} // namespace aegis
