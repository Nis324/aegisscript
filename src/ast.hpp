// ============================================================================
//  AegisScript - Abstract Syntax Tree
//  Project: AegisScript DSL & Transpiler for Game AI Finite State Machines
//  Author : Nishant Kumar (24BCE2192), VIT Vellore
// ----------------------------------------------------------------------------
//  CONFIDENTIAL - PATENT PENDING MATERIAL. Do not distribute publicly.
// ============================================================================
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

namespace aegis {

// ---------------------------------------------------------------- types ----
enum class VarType { Int, Float, Bool, Unknown };
const char* typeName(VarType t);

struct SourceLoc {
    int line = 0;
    int col  = 0;
};

// ---------------------------------------------------------- expressions ----
enum class ExprKind { IntLit, FloatLit, BoolLit, VarRef, Binary, Unary };
enum class BinOp { Add, Sub, Mul, Div, Lt, Le, Gt, Ge, Eq, Ne, And, Or };
enum class UnOp  { Neg, Not };

const char* binOpCxx(BinOp op);
const char* unOpCxx(UnOp op);

struct Expr {
    ExprKind  kind;
    SourceLoc loc;

    long long   ival = 0;       // IntLit
    double      fval = 0.0;     // FloatLit
    bool        bval = false;   // BoolLit
    std::string name;           // VarRef

    BinOp bop = BinOp::Add;
    UnOp  uop = UnOp::Neg;
    Expr* lhs = nullptr;
    Expr* rhs = nullptr;

    VarType type = VarType::Unknown;   // annotated by the semantic pass
};

// ------------------------------------------------------- engine actions ----
// An action the host engine provides. Declared in the machine, called from
// entry / update / exit blocks, and lowered to a free function call -- so the
// generated code gains engine integration without gaining virtual dispatch or
// an allocation.
struct ActionDecl {
    std::string          name;
    std::vector<VarType> params;
    std::vector<std::string> paramNames;
    SourceLoc            loc;
};

// ----------------------------------------------------------- statements ----
enum class StmtKind { Assign, Call };

struct Stmt {
    StmtKind    kind = StmtKind::Assign;

    // Assign:  target = value;
    std::string target;
    Expr*       value = nullptr;

    // Call:    name(arg, arg, ...);
    std::vector<Expr*> args;
    int         actionId = -1;      // resolved by the semantic pass

    SourceLoc   loc;
};

using Block = std::vector<Stmt>;

// ---------------------------------------------------------- transitions ----
struct Transition {
    std::string event;
    std::string target;
    Expr*       guard = nullptr;    // nullptr == unconditional
    SourceLoc   loc;

    int eventId  = -1;              // resolved by the semantic pass
    int targetId = -1;

    // Hierarchy support, filled by the flattening pass. Node ids (indices into
    // Machine::nodes) whose exit / entry actions run when this transition
    // fires: exits innermost-first, entries outermost-first. For a flat
    // machine these are just the source and target states themselves.
    std::vector<int> exitNodes;
    std::vector<int> enterNodes;
    int declaredBy = -1;            // node that declared it (may be an ancestor)
};

struct State {
    std::string name;               // C++-safe identifier; nested states are Parent_Child
    std::string path;               // display form: Parent.Child
    bool isInitial  = false;
    bool isFinal    = false;
    bool isParallel = false;   // children are orthogonal regions

    Block onEnter, onUpdate, onExit;
    bool  hasEnter = false, hasUpdate = false, hasExit = false;

    std::vector<Transition> transitions;
    SourceLoc loc;

    int  id        = -1;
    bool reachable = false;         // filled by reachability analysis

    // --- hierarchy (set on Machine::nodes by the parser) ------------------
    int              parent = -1;   // node id, -1 at the top level
    std::vector<int> children;      // node ids; non-empty means composite
    int              nodeId = -1;   // for a leaf in Machine::states: its node id
    int              depth  = 0;

    bool isComposite() const { return !children.empty(); }

    const std::string& display() const { return path.empty() ? name : path; }
};

struct VarDecl {
    VarType     type = VarType::Unknown;
    std::string name;
    Expr*       init = nullptr;
    SourceLoc   loc;
};

struct Machine {
    std::string              name;
    std::vector<VarDecl>     context;
    std::vector<std::string> events;
    std::vector<ActionDecl>  actions;

    // The state tree exactly as written. Composite states live only here.
    std::vector<State>       nodes;

    // The flattened machine: leaf states only, each carrying the transitions
    // it actually responds to (its own plus everything inherited from its
    // ancestors). Every pass after flattening -- validation, synthesis
    // planning, code generation -- works on this and never sees the tree.
    std::vector<State>       states;

    // Node ids entered on reset, outermost first.
    std::vector<int>         initEnterChain;

    bool hierarchical = false;      // true if any state has children
    bool hasParallel  = false;      // true if any state is a parallel composite
    int  maxDepth     = 0;

    SourceLoc                loc;

    std::map<std::string,int> stateIndex;
    std::map<std::string,int> eventIndex;
    std::map<std::string,int> varIndex;
    std::map<std::string,int> actionIndex;
};

// ------------------------------------------------------- parse context -----
// Owns every Expr node so the grammar can pass raw pointers through the
// Bison %union without fighting smart-pointer semantics.
struct ParseContext {
    Machine machine;
    std::vector<std::unique_ptr<Expr>> arena;
    std::vector<std::string> parseErrors;
    std::string file;

    // Open state declarations, innermost last. The parser pushes on '{' and
    // pops on '}', so nested states attach to the right parent.
    std::vector<int> stateStack;
    State& cur() { return machine.nodes[stateStack.back()]; }

    Expr* alloc() {
        arena.emplace_back(new Expr());
        return arena.back().get();
    }
    Expr* intLit(long long v, SourceLoc l)   { Expr* e=alloc(); e->kind=ExprKind::IntLit;   e->ival=v; e->loc=l; return e; }
    Expr* floatLit(double v, SourceLoc l)    { Expr* e=alloc(); e->kind=ExprKind::FloatLit; e->fval=v; e->loc=l; return e; }
    Expr* boolLit(bool v, SourceLoc l)       { Expr* e=alloc(); e->kind=ExprKind::BoolLit;  e->bval=v; e->loc=l; return e; }
    Expr* varRef(const std::string& n, SourceLoc l) { Expr* e=alloc(); e->kind=ExprKind::VarRef; e->name=n; e->loc=l; return e; }
    Expr* binary(BinOp op, Expr* a, Expr* b, SourceLoc l) {
        Expr* e=alloc(); e->kind=ExprKind::Binary; e->bop=op; e->lhs=a; e->rhs=b; e->loc=l; return e;
    }
    Expr* unary(UnOp op, Expr* a, SourceLoc l) {
        Expr* e=alloc(); e->kind=ExprKind::Unary; e->uop=op; e->lhs=a; e->loc=l; return e;
    }
};

extern ParseContext* gCtx;

} // namespace aegis
