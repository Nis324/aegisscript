// ============================================================================
//  AegisScript - compiler driver (aegisc)
// ============================================================================
#include "ast.hpp"
#include "validator.hpp"
#include "layout.hpp"
#include "codegen.hpp"
#include "diagram.hpp"
#include "flatten.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

extern int   yyparse();
extern FILE* yyin;
extern int   yylineno;

namespace aegis { ParseContext* gCtx = nullptr; }
using namespace aegis;

namespace {

constexpr const char* kVersion = "0.3.0";

bool gColor = true;
const char* C_RED()   { return gColor ? "\033[1;31m" : ""; }
const char* C_YEL()   { return gColor ? "\033[1;33m" : ""; }
const char* C_CYA()   { return gColor ? "\033[1;36m" : ""; }
const char* C_GRN()   { return gColor ? "\033[1;32m" : ""; }
const char* C_DIM()   { return gColor ? "\033[2m"    : ""; }
const char* C_OFF()   { return gColor ? "\033[0m"    : ""; }

void usage() {
    std::printf(
        "AegisScript transpiler v%s\n"
        "\n"
        "  aegisc <input.aegis> [options]\n"
        "\n"
        "Options:\n"
        "  -o <dir>       output directory for generated C++      (default: ./out)\n"
        "  --check-only   run the validator, emit no code\n"
        "  --metrics      print the synthesis report\n"
        "  --emit-demo    also write <Machine>_demo.cpp smoke test\n"
        "  --emit-svg     write <Machine>.svg, the transition graph (opens in a browser)\n"
        "  --emit-dot     write <Machine>.dot, Graphviz source\n"
        "  --force-strategy=<s>  override the selector: dense | sparse | linear\n"
        "                 (calibration only -- the point of the tool is that it chooses)\n"
        "  --no-color     plain output\n"
        "  --version      print version and exit\n",
        kVersion);
}

bool writeFile(const std::string& path, const std::string& body) {
    std::ofstream f(path);
    if (!f) return false;
    f << body;
    return true;
}

void printDiagnostics(const std::string& file, const Validator& v) {
    for (const auto& d : v.diagnostics()) {
        const char* col = d.severity == Severity::Error ? C_RED()
                        : d.severity == Severity::Warning ? C_YEL() : C_CYA();
        const char* tag = d.severity == Severity::Error ? "error"
                        : d.severity == Severity::Warning ? "warning" : "note";
        std::printf("%s:%d: %s%s%s [%s] %s\n",
                    file.c_str(), d.loc.line, col, tag, C_OFF(),
                    d.code.c_str(), d.message.c_str());
    }
}

void printMetrics(const GraphFacts& f, const LayoutPlan& p, const BaselineCost& b) {
    std::printf("\n%s== synthesis report ==%s\n", C_CYA(), C_OFF());
    std::printf("  states declared      : %d\n", f.stateCount);
    std::printf("  states reachable     : %d%s\n", f.reachableStates,
                p.prunedStates ? "   (unreachable states pruned)" : "");
    std::printf("  events               : %d\n", f.eventCount);
    std::printf("  transitions          : %d  (%d guarded)\n", f.transitionCount, f.guardedCount);
    std::printf("  max out-degree       : %d\n", f.maxFanOut);
    std::printf("  graph occupancy      : %.1f%%  (%d of %ld cells)\n",
                f.density * 100.0, f.transitionCount, p.cells);
    std::printf("  deterministic        : %s\n", f.deterministic ? "yes" : "no");

    std::printf("\n  %sstrategy selected   : %s%s\n", C_GRN(), strategyName(p.strategy), C_OFF());
    for (const auto& r : p.rationale)
        std::printf("    %s- %s%s\n", C_DIM(), r.c_str(), C_OFF());

    std::printf("\n  %sstatic properties of the emitted code%s\n", C_CYA(), C_OFF());
    std::printf("  %-24s %12s %12s\n", "", "naive", "aegis");
    std::printf("  %-24s %12d %12d\n", "worst-case probes", b.worstCaseProbes, p.worstCaseProbes);
    std::printf("  %-24s %12ld %12ld\n", "transition bytes", b.tableBytes, p.tableBytes);
    std::printf("  %-24s %12s %12s\n", "heap allocations", "n/a", "0");
    std::printf("\n  %sThese are static counts, not timings. A lower probe count does not\n"
                "  translate into a proportional speed-up on real hardware: caching and\n"
                "  branch prediction absorb most of a short comparison chain. Run\n"
                "  'make bench' for measured wall-clock results.%s\n\n", C_DIM(), C_OFF());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }

    std::string input, outDir = "out";
    bool checkOnly = false, metrics = false, emitDemo = false;
    bool emitSvg = false, emitDot = false;
    bool haveForced = false;
    Strategy forced = Strategy::DenseTable;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--version")         { std::printf("aegisc %s\n", kVersion); return 0; }
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "--check-only") checkOnly = true;
        else if (a == "--metrics")    metrics   = true;
        else if (a == "--emit-demo")  emitDemo  = true;
        else if (a == "--emit-svg")   emitSvg   = true;
        else if (a == "--emit-dot")   emitDot   = true;
        else if (a.rfind("--force-strategy=", 0) == 0) {
            if (!parseStrategy(a.substr(17), forced)) {
                std::fprintf(stderr, "unknown strategy '%s' (use dense, sparse or linear)\n",
                             a.substr(17).c_str());
                return 1;
            }
            haveForced = true;
        }
        else if (a == "--no-color")   gColor    = false;
        else if (a == "-o" && i + 1 < argc) outDir = argv[++i];
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); return 1; }
        else input = a;
    }

    if (input.empty()) { usage(); return 1; }

    yyin = std::fopen(input.c_str(), "r");
    if (!yyin) { std::fprintf(stderr, "cannot open '%s'\n", input.c_str()); return 1; }

    ParseContext ctx;
    ctx.file = input;
    gCtx = &ctx;
    yylineno = 1;

    // ---------------------------------------------------- phase 1 -------
    yyparse();
    std::fclose(yyin);

    if (!ctx.parseErrors.empty()) {
        // Same file:line: severity [CODE] shape as every other diagnostic, so
        // one format covers the whole compiler.
        for (const auto& e : ctx.parseErrors)
            std::printf("%s:%s: %serror%s [AEG-E000] %s\n",
                        input.c_str(), e.substr(0, e.find(':')).c_str(),
                        C_RED(), C_OFF(), e.substr(e.find(": ") + 2).c_str());
        std::printf("\n%s%zu syntax error(s)%s -- compilation aborted\n",
                    C_RED(), ctx.parseErrors.size(), C_OFF());
        return 2;
    }

    // ---------------------------------------------------- phase 1c ------
    // Flatten the state tree into the leaf machine every later stage uses.
    FlattenResult fl = flatten(ctx.machine);
    for (const auto& d : fl.diagnostics) {
        const char* col = d.severity == Severity::Error ? C_RED() : C_YEL();
        const char* tag = d.severity == Severity::Error ? "error" : "warning";
        std::printf("%s:%d: %s%s%s [%s] %s\n", input.c_str(), d.loc.line,
                    col, tag, C_OFF(), d.code.c_str(), d.message.c_str());
    }
    if (!fl.ok) {
        std::printf("\n%shierarchy could not be flattened%s -- no code generated\n",
                    C_RED(), C_OFF());
        return 3;
    }

    // ---------------------------------------------------- phase 2 -------
    Validator v(ctx.machine);
    bool ok = v.run();
    printDiagnostics(input, v);

    const GraphFacts& facts = v.facts();
    LayoutPlan plan = planLayout(ctx.machine, facts, haveForced ? &forced : nullptr);
    BaselineCost base = baselineCost(ctx.machine, facts);

    if (emitSvg || emitDot) {
        ::mkdir(outDir.c_str(), 0755);
        const std::string stem = outDir + "/" + ctx.machine.name;
        if (emitSvg) {
            writeFile(stem + ".svg", renderSvg(ctx.machine, facts, ok ? strategyName(plan.strategy) : ""));
            std::printf("  wrote %s.svg\n", stem.c_str());
        }
        if (emitDot) {
            writeFile(stem + ".dot", renderDot(ctx.machine));
            std::printf("  wrote %s.dot\n", stem.c_str());
        }
    }

    if (!ok) {
        std::printf("\n%s%d error(s), %d warning(s)%s in machine '%s' -- no code generated\n",
                    C_RED(), v.errorCount(), v.warningCount(), C_OFF(), ctx.machine.name.c_str());
        return 3;
    }

    std::printf("%s%s validated%s: %d state(s), %d event(s), %d transition(s), %d warning(s)\n",
                C_GRN(), ctx.machine.name.c_str(), C_OFF(),
                facts.stateCount, facts.eventCount, facts.transitionCount, v.warningCount());
    if (ctx.machine.hierarchical)
        std::printf("  hierarchy: %d composite state(s), %d leaf state(s), depth %d, "
                    "%d inherited transition(s), %d shadowed\n",
                    fl.compositeCount, fl.leafCount, ctx.machine.maxDepth + 1,
                    fl.inheritedEdges, fl.shadowedEdges);
    if (ctx.machine.hasParallel)
        std::printf("  parallel : %d composite(s), %d region(s) multiplied out into "
                    "%d product state(s) (largest product %d)\n",
                    fl.parallelCount, fl.regionCount, fl.productStates, fl.productFactor);

    if (metrics) printMetrics(facts, plan, base);
    if (checkOnly) return 0;

    // ---------------------------------------------------- phase 3 -------
    ::mkdir(outDir.c_str(), 0755);

    CodegenResult gen = generate(ctx.machine, facts, plan, input);
    const std::string base_ = outDir + "/" + ctx.machine.name;

    if (!writeFile(base_ + ".h", gen.header) || !writeFile(base_ + ".cpp", gen.source)) {
        std::fprintf(stderr, "failed to write output into '%s'\n", outDir.c_str());
        return 4;
    }
    std::printf("  wrote %s.h\n  wrote %s.cpp\n", base_.c_str(), base_.c_str());

    if (emitDemo) {
        writeFile(base_ + "_demo.cpp", gen.demo);
        std::printf("  wrote %s_demo.cpp\n", base_.c_str());
    }
    return 0;
}
