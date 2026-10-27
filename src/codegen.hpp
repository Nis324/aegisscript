// ============================================================================
//  AegisScript - C++ Backend (Phase 3)
//  Lowers the verified AST + LayoutPlan into standalone .h / .cpp sources.
// ============================================================================
#pragma once

#include "ast.hpp"
#include "layout.hpp"
#include "validator.hpp"
#include <string>

namespace aegis {

struct CodegenResult {
    std::string header;    // <Machine>.h
    std::string source;    // <Machine>.cpp
    std::string demo;      // <Machine>_demo.cpp  (integration smoke test)
    int emittedStates = 0;
    int emittedEvents = 0;
};

CodegenResult generate(const Machine& m,
                       const GraphFacts& f,
                       const LayoutPlan& plan,
                       const std::string& sourceFile);

} // namespace aegis
