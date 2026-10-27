/* ==========================================================================
 *  AegisScript - Lexical Specification (Flex)
 *  Phase 1a: Regular-expression driven tokenisation
 * ========================================================================== */
%{
#include <cstdlib>
#include <cstring>
#include <string>
#include "ast.hpp"
#include "parser.hpp"

int aegisColumn = 1;

#define YY_USER_ACTION  aegisColumn += yyleng;
%}

%option noyywrap
%option yylineno
%option nounput
%option noinput

DIGIT       [0-9]
LETTER      [A-Za-z_]
IDENTIFIER  {LETTER}({LETTER}|{DIGIT})*
INTEGER     {DIGIT}+
FLOAT       {DIGIT}+"."{DIGIT}+

%%

"//"[^\n]*              { /* line comment */ }
"/*"([^*]|\*+[^*/])*\*+"/"   { /* block comment */ }

[ \t\r]+                { /* whitespace */ }
\n                      { aegisColumn = 1; }

"machine"               { return MACHINE; }
"context"               { return CONTEXT; }
"events"                { return EVENTS;  }
"actions"               { return ACTIONS; }
"state"                 { return STATE;   }
"initial"               { return KW_INITIAL; }
"final"                 { return KW_FINAL;   }
"parallel"              { return KW_PARALLEL; }
"on"                    { return ON;      }
"enter"                 { return ENTER;   }
"update"                { return UPDATE;  }
"exit"                  { return EXIT;    }
"when"                  { return WHEN;    }

"int"                   { return KW_INT;   }
"float"                 { return KW_FLOAT; }
"bool"                  { return KW_BOOL;  }
"true"                  { return TRUE_LIT; }
"false"                 { return FALSE_LIT;}

"->"                    { return ARROW; }
"<="                    { return LE; }
">="                    { return GE; }
"=="                    { return EQ; }
"!="                    { return NE; }
"&&"                    { return AND; }
"||"                    { return OR; }

{FLOAT}                 { yylval.fval = atof(yytext); return FLOAT_LIT; }
{INTEGER}               { yylval.ival = atoll(yytext); return INT_LIT;  }
{IDENTIFIER}            { yylval.str  = strdup(yytext); return IDENT;   }

[{}();,=<>+\-*/!]       { return yytext[0]; }

.                       {
                          char buf[128];
                          snprintf(buf, sizeof(buf),
                                   "line %d: unrecognised character '%s'",
                                   yylineno, yytext);
                          aegis::gCtx->parseErrors.push_back(buf);
                        }

%%
