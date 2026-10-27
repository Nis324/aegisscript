/* ==========================================================================
 *  AegisScript - Context-Free Grammar (GNU Bison)
 *  Phase 1b: Syntax analysis and AST construction
 * ========================================================================== */
%{
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "ast.hpp"

using namespace aegis;

extern int yylex();
extern int yylineno;
extern int aegisColumn;
void yyerror(const char* s);

static SourceLoc here() { SourceLoc l; l.line = yylineno; l.col = aegisColumn; return l; }

/* modifier flags captured before the state record is pushed */
static aegis::ActionDecl gPendingAction;
static bool gPendingInitial  = false;
static bool gPendingFinal    = false;
static bool gPendingParallel = false;
%}

%union {
    long long                 ival;
    std::vector<aegis::Expr*>* args;
    double                    fval;
    int                       bval;
    char*                     str;
    aegis::VarType            vtype;
    aegis::Expr*              expr;
    aegis::Stmt*              stmt;
    std::vector<aegis::Stmt>* block;
    aegis::Transition*        tran;
    aegis::State*             state;
    aegis::VarDecl*           decl;
}

%token MACHINE CONTEXT EVENTS ACTIONS STATE KW_INITIAL KW_FINAL KW_PARALLEL ON ENTER UPDATE EXIT WHEN ARROW
%token KW_INT KW_FLOAT KW_BOOL TRUE_LIT FALSE_LIT
%token LE GE EQ NE AND OR
%token <ival> INT_LIT
%token <fval> FLOAT_LIT
%token <str>  IDENT

%type <vtype> type_spec
%type <expr>  expr opt_guard
%type <block> stmt_list block
%type <stmt>  stmt
%type <args>  arg_list arg_list_ne

%left OR
%left AND
%nonassoc EQ NE
%nonassoc '<' '>' LE GE
%left '+' '-'
%left '*' '/'
%right UMINUS '!'

%start program

%%

program
    : MACHINE IDENT '{' { gCtx->machine.name = $2; gCtx->machine.loc = here(); free($2); }
      machine_body '}'
    ;

machine_body
    : /* empty */
    | machine_body machine_item
    ;

machine_item
    : context_block
    | events_block
    | actions_block
    | state_decl
    ;

/* ------------------------------------------------------------ actions --- */
/* Engine hooks the behaviour may call. Declared up front so the compiler can
 * check every call site against a known signature. */
actions_block
    : ACTIONS '{' action_decls '}'
    ;

action_decls
    : /* empty */
    | action_decls action_decl
    ;

action_decl
    : IDENT '(' param_list ')' ';'
      {
        gPendingAction.name = $1;
        gPendingAction.loc  = here();
        gCtx->machine.actions.push_back(gPendingAction);
        gPendingAction = ActionDecl();
        free($1);
      }
    | error ';'        /* panic-mode recovery */
      { gPendingAction = ActionDecl(); }
    ;

param_list
    : /* empty */
    | param_list_ne
    ;

param_list_ne
    : param
    | param_list_ne ',' param
    ;

param
    : type_spec IDENT
      {
        gPendingAction.params.push_back($1);
        gPendingAction.paramNames.push_back($2);
        free($2);
      }
    | type_spec
      {
        gPendingAction.params.push_back($1);
        gPendingAction.paramNames.push_back("");
      }
    ;

/* ------------------------------------------------------------ context --- */
context_block
    : CONTEXT '{' var_decls '}'
    ;

var_decls
    : /* empty */
    | var_decls var_decl
    ;

var_decl
    : type_spec IDENT '=' expr ';'
      {
        VarDecl d; d.type = $1; d.name = $2; d.init = $4; d.loc = here();
        gCtx->machine.context.push_back(d);
        free($2);
      }
    | type_spec IDENT ';'
      {
        VarDecl d; d.type = $1; d.name = $2; d.init = nullptr; d.loc = here();
        gCtx->machine.context.push_back(d);
        free($2);
      }
    | error ';'        /* panic-mode recovery: resynchronise on the semicolon */
    ;

type_spec
    : KW_INT   { $$ = VarType::Int;   }
    | KW_FLOAT { $$ = VarType::Float; }
    | KW_BOOL  { $$ = VarType::Bool;  }
    ;

/* ------------------------------------------------------------- events --- */
events_block
    : EVENTS '{' event_list '}'
    ;

event_list
    : /* empty */
    | event_item
    | event_list ',' event_item
    ;

event_item
    : IDENT { gCtx->machine.events.push_back($1); free($1); }
    ;

/* ------------------------------------------------------------- states --- */
state_decl
    : state_modifiers STATE IDENT '{'
      {
        State s;
        s.name      = $3;
        s.loc       = here();
        s.isInitial  = gPendingInitial;
        s.isFinal    = gPendingFinal;
        s.isParallel = gPendingParallel;
        if (s.isParallel) gCtx->machine.hasParallel = true;
        s.parent    = gCtx->stateStack.empty() ? -1 : gCtx->stateStack.back();
        s.depth     = static_cast<int>(gCtx->stateStack.size());

        int id = static_cast<int>(gCtx->machine.nodes.size());
        if (s.parent >= 0) {
            gCtx->machine.nodes[s.parent].children.push_back(id);
            gCtx->machine.hierarchical = true;
        }
        if (s.depth > gCtx->machine.maxDepth) gCtx->machine.maxDepth = s.depth;

        gCtx->machine.nodes.push_back(s);
        gCtx->stateStack.push_back(id);
        free($3);
      }
      state_body '}'
      { gCtx->stateStack.pop_back(); }
    ;

/* Modifiers reduce before the state record is pushed, so they are parked in
 * file-static flags and folded onto the state in the mid-rule action above. */
state_modifiers
    : /* empty */
      { gPendingInitial = false; gPendingFinal = false; gPendingParallel = false; }
    | state_modifiers state_modifier
    ;

state_modifier
    : KW_INITIAL  { gPendingInitial  = true; }
    | KW_FINAL    { gPendingFinal    = true; }
    | KW_PARALLEL { gPendingParallel = true; }
    ;

state_body
    : /* empty */
    | state_body state_item
    ;

state_item
    : ON ENTER  block { gCtx->cur().onEnter  = *$3; gCtx->cur().hasEnter  = true; delete $3; }
    | ON UPDATE block { gCtx->cur().onUpdate = *$3; gCtx->cur().hasUpdate = true; delete $3; }
    | ON EXIT   block { gCtx->cur().onExit   = *$3; gCtx->cur().hasExit   = true; delete $3; }
    | transition
    | state_decl                 /* nested state: this is what makes the grammar hierarchical */
    | error ';'                  /* panic-mode recovery: skip the bad item, keep the state */
    ;

transition
    : ON IDENT ARROW IDENT opt_guard ';'
      {
        Transition t;
        t.event  = $2;
        t.target = $4;
        t.guard  = $5;
        t.loc    = here();
        gCtx->cur().transitions.push_back(t);
        free($2); free($4);
      }
    ;

opt_guard
    : /* empty */   { $$ = nullptr; }
    | WHEN expr     { $$ = $2;      }
    ;

/* --------------------------------------------------------- statements --- */
block
    : '{' stmt_list '}' { $$ = $2; }
    ;

stmt_list
    : /* empty */        { $$ = new std::vector<Stmt>(); }
    | stmt_list stmt     { $$ = $1; if (!$2->target.empty()) $$->push_back(*$2); delete $2; }
    ;

stmt
    : IDENT '=' expr ';'
      {
        $$ = new Stmt();
        $$->kind   = StmtKind::Assign;
        $$->target = $1;
        $$->value  = $3;
        $$->loc    = here();
        free($1);
      }
    | IDENT '(' arg_list ')' ';'
      {
        $$ = new Stmt();
        $$->kind   = StmtKind::Call;
        $$->target = $1;
        $$->args   = *$3;
        $$->loc    = here();
        delete $3;
        free($1);
      }
    | error ';'
      { $$ = new Stmt(); $$->kind = StmtKind::Assign; $$->target = ""; $$->loc = here(); }
    ;

arg_list
    : /* empty */        { $$ = new std::vector<Expr*>(); }
    | arg_list_ne   { $$ = $1; }
    ;

arg_list_ne
    : expr                   { $$ = new std::vector<Expr*>(); $$->push_back($1); }
    | arg_list_ne ',' expr   { $$ = $1; $$->push_back($3); }
    ;

/* -------------------------------------------------------- expressions --- */
expr
    : INT_LIT                 { $$ = gCtx->intLit($1, here());   }
    | FLOAT_LIT               { $$ = gCtx->floatLit($1, here()); }
    | TRUE_LIT                { $$ = gCtx->boolLit(true, here());  }
    | FALSE_LIT               { $$ = gCtx->boolLit(false, here()); }
    | IDENT                   { $$ = gCtx->varRef($1, here()); free($1); }
    | '(' expr ')'            { $$ = $2; }
    | expr '+' expr           { $$ = gCtx->binary(BinOp::Add, $1, $3, here()); }
    | expr '-' expr           { $$ = gCtx->binary(BinOp::Sub, $1, $3, here()); }
    | expr '*' expr           { $$ = gCtx->binary(BinOp::Mul, $1, $3, here()); }
    | expr '/' expr           { $$ = gCtx->binary(BinOp::Div, $1, $3, here()); }
    | expr '<' expr           { $$ = gCtx->binary(BinOp::Lt,  $1, $3, here()); }
    | expr '>' expr           { $$ = gCtx->binary(BinOp::Gt,  $1, $3, here()); }
    | expr LE  expr           { $$ = gCtx->binary(BinOp::Le,  $1, $3, here()); }
    | expr GE  expr           { $$ = gCtx->binary(BinOp::Ge,  $1, $3, here()); }
    | expr EQ  expr           { $$ = gCtx->binary(BinOp::Eq,  $1, $3, here()); }
    | expr NE  expr           { $$ = gCtx->binary(BinOp::Ne,  $1, $3, here()); }
    | expr AND expr           { $$ = gCtx->binary(BinOp::And, $1, $3, here()); }
    | expr OR  expr           { $$ = gCtx->binary(BinOp::Or,  $1, $3, here()); }
    | '-' expr %prec UMINUS   { $$ = gCtx->unary(UnOp::Neg, $2, here()); }
    | '!' expr                { $$ = gCtx->unary(UnOp::Not, $2, here()); }
    ;

%%

/* Reports the offending token as well as the line.
 *
 * Bison's own verbose messages are spelled differently in Bison 2 and Bison 3,
 * and this grammar is kept buildable with both, so the useful part is assembled
 * here instead: at the point yyerror runs, yytext still holds the lookahead
 * token that could not be shifted. */
extern char* yytext;

void yyerror(const char* s) {
    char buf[512];
    const char* tok = (yytext && *yytext) ? yytext : nullptr;

    if (tok && std::strcmp(tok, "\n") != 0)
        snprintf(buf, sizeof(buf), "%d: %s near '%s'", yylineno, s, tok);
    else
        snprintf(buf, sizeof(buf), "%d: %s at end of input", yylineno, s);

    /* Panic-mode recovery resynchronises on ';'. Bison suppresses further
     * reports until three tokens have shifted cleanly, so a single fault
     * produces one message rather than a cascade. */
    gCtx->parseErrors.push_back(buf);
}
