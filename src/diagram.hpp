// ============================================================================
//  AegisScript - transition graph visualiser
//  Renders the verified (or rejected) transition digraph as a self-contained
//  SVG and as Graphviz DOT. No external tooling required for the SVG.
// ============================================================================
#pragma once

#include "ast.hpp"
#include "validator.hpp"
#include <string>

namespace aegis {

// Self-contained SVG. Opens in any browser. Unreachable states are drawn red,
// sinkholes amber, so the diagram shows what the validator found.
std::string renderSvg(const Machine& m, const GraphFacts& f, const std::string& strategy);

// Graphviz source, for anyone who has `dot` installed.
std::string renderDot(const Machine& m);

} // namespace aegis
