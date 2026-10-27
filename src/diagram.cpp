#include "diagram.hpp"

#include <cmath>
#include <map>
#include <sstream>
#include <vector>
#include <algorithm>

namespace aegis {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Pt { double x = 0, y = 0; };

struct NodeStyle {
    const char* fill;
    const char* stroke;
    const char* text;
    bool dashed;
};

// A merged edge: all transitions from A to B collapse into one arrow whose
// label lists the triggering events.
struct Edge {
    int from = 0, to = 0;
    std::vector<std::string> labels;
};

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;";  break;
            case '<': o += "&lt;";   break;
            case '>': o += "&gt;";   break;
            case '"': o += "&quot;"; break;
            default:  o += c;
        }
    }
    return o;
}

std::string num(double v) {
    std::ostringstream o;
    o.setf(std::ios::fixed);
    o.precision(1);
    o << v;
    return o.str();
}

std::string joinLabels(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ", ";
        s += v[i];
    }
    return s;
}

} // namespace

// ---------------------------------------------------------------- SVG ------
std::string renderSvg(const Machine& m, const GraphFacts& f, const std::string& strategy) {
    const int n = static_cast<int>(m.states.size());
    if (n == 0) return "<svg xmlns=\"http://www.w3.org/2000/svg\"/>";

    // ---- layout: states on a circle ---------------------------------------
    const double nodeR = 38.0;
    const double ringR = std::max(185.0, n * 40.0);
    const double pad   = 150.0;
    const double cx    = ringR + pad;
    const double cy    = ringR + pad + 40.0;
    const double W     = 2 * (ringR + pad);
    const double H     = 2 * (ringR + pad) + 120.0;

    std::vector<Pt> pos(n);
    for (int i = 0; i < n; ++i) {
        double a = -kPi / 2 + (2 * kPi * i) / n;
        pos[i] = { cx + ringR * std::cos(a), cy + ringR * std::sin(a) };
    }

    // ---- merge transitions into edges -------------------------------------
    std::map<std::pair<int,int>, Edge> merged;
    for (const auto& s : m.states) {
        for (const auto& t : s.transitions) {
            if (t.targetId < 0) continue;      // dangling target: nothing to draw
            auto key = std::make_pair(s.id, t.targetId);
            auto& e = merged[key];
            e.from = s.id;
            e.to   = t.targetId;
            e.labels.push_back(t.guard ? t.event + " [g]" : t.event);
        }
    }

    std::ostringstream o;
    o.setf(std::ios::fixed);

    o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << num(W) << "\" height=\"" << num(H)
      << "\" viewBox=\"0 0 " << num(W) << " " << num(H) << "\" font-family=\""
         "ui-sans-serif,-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif\">\n";

    o << "  <defs>\n"
      << "    <marker id=\"ar\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" markerWidth=\"7\""
         " markerHeight=\"7\" orient=\"auto-start-reverse\">\n"
      << "      <path d=\"M0,0 L10,5 L0,10 z\" fill=\"#5A6B7D\"/>\n"
      << "    </marker>\n"
      << "    <marker id=\"arEntry\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" markerWidth=\"8\""
         " markerHeight=\"8\" orient=\"auto-start-reverse\">\n"
      << "      <path d=\"M0,0 L10,5 L0,10 z\" fill=\"#1F4E79\"/>\n"
      << "    </marker>\n"
      << "  </defs>\n";

    o << "  <rect width=\"100%\" height=\"100%\" fill=\"#FFFFFF\"/>\n";

    // ---- title -------------------------------------------------------------
    o << "  <text x=\"28\" y=\"40\" font-size=\"21\" font-weight=\"700\" fill=\"#1F4E79\">"
      << esc(m.name) << "</text>\n";
    {
        std::ostringstream sub;
        sub << f.stateCount << " states \xC2\xB7 " << f.eventCount << " events \xC2\xB7 "
            << f.transitionCount << " transitions \xC2\xB7 occupancy "
            << static_cast<int>(f.density * 100.0 + 0.5) << "%";
        if (m.hierarchical)
            sub << " \xC2\xB7 " << (m.maxDepth + 1) << " levels, flattened";
        if (!strategy.empty()) sub << " \xC2\xB7 " << strategy;
        o << "  <text x=\"28\" y=\"62\" font-size=\"13\" fill=\"#6B7A8A\">"
          << esc(sub.str()) << "</text>\n";
    }

    // ---- edges -------------------------------------------------------------
    for (const auto& kv : merged) {
        const Edge& e = kv.second;

        if (e.from == e.to) {
            // self-loop, bulging away from the ring centre
            const Pt& p = pos[e.from];
            double a = std::atan2(p.y - cy, p.x - cx);
            double ox = std::cos(a), oy = std::sin(a);
            double px = -oy, py = ox;                       // perpendicular

            double sx = p.x + ox * nodeR * 0.55 + px * nodeR * 0.7;
            double sy = p.y + oy * nodeR * 0.55 + py * nodeR * 0.7;
            double ex = p.x + ox * nodeR * 0.55 - px * nodeR * 0.7;
            double ey = p.y + oy * nodeR * 0.55 - py * nodeR * 0.7;
            double c1x = p.x + ox * (nodeR + 62) + px * nodeR * 1.15;
            double c1y = p.y + oy * (nodeR + 62) + py * nodeR * 1.15;
            double c2x = p.x + ox * (nodeR + 62) - px * nodeR * 1.15;
            double c2y = p.y + oy * (nodeR + 62) - py * nodeR * 1.15;

            o << "  <path d=\"M" << num(sx) << "," << num(sy)
              << " C" << num(c1x) << "," << num(c1y)
              << " " << num(c2x) << "," << num(c2y)
              << " " << num(ex) << "," << num(ey) << "\" fill=\"none\" stroke=\"#5A6B7D\""
                 " stroke-width=\"1.6\" marker-end=\"url(#ar)\"/>\n";

            double lx = p.x + ox * (nodeR + 58);
            double ly = p.y + oy * (nodeR + 58);
            o << "  <text x=\"" << num(lx) << "\" y=\"" << num(ly)
              << "\" font-size=\"11.5\" fill=\"#3D4C5A\" text-anchor=\"middle\">"
              << esc(joinLabels(e.labels)) << "</text>\n";
            continue;
        }

        Pt a = pos[e.from], b = pos[e.to];
        double dx = b.x - a.x, dy = b.y - a.y;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6) continue;

        double sign = (e.from < e.to) ? 1.0 : -1.0;
        double bow  = 0.16 * len * sign;
        double mx = (a.x + b.x) / 2 + (-dy / len) * bow;
        double my = (a.y + b.y) / 2 + ( dx / len) * bow;

        // trim the endpoints back to the node boundaries
        double s1x = mx - a.x, s1y = my - a.y;
        double l1 = std::sqrt(s1x * s1x + s1y * s1y);
        double sx = a.x + s1x / l1 * nodeR;
        double sy = a.y + s1y / l1 * nodeR;

        double s2x = mx - b.x, s2y = my - b.y;
        double l2 = std::sqrt(s2x * s2x + s2y * s2y);
        double ex = b.x + s2x / l2 * (nodeR + 5);
        double ey = b.y + s2y / l2 * (nodeR + 5);

        o << "  <path d=\"M" << num(sx) << "," << num(sy)
          << " Q" << num(mx) << "," << num(my) << " " << num(ex) << "," << num(ey)
          << "\" fill=\"none\" stroke=\"#5A6B7D\" stroke-width=\"1.6\""
             " marker-end=\"url(#ar)\"/>\n";

        // label near the source end of the curve (t = 0.28) rather than the
        // midpoint: on a dense graph every midpoint lands in the middle of the
        // ring and the labels collide.
        const double t = 0.28, u = 1.0 - t;
        double lx = u * u * sx + 2 * u * t * mx + t * t * ex;
        double ly = u * u * sy + 2 * u * t * my + t * t * ey;
        std::string lab = joinLabels(e.labels);
        double wpx = lab.size() * 5.9 + 10;

        o << "  <rect x=\"" << num(lx - wpx / 2) << "\" y=\"" << num(ly - 9)
          << "\" width=\"" << num(wpx) << "\" height=\"17\" rx=\"4\""
             " fill=\"#FFFFFF\" fill-opacity=\"0.92\"/>\n";
        o << "  <text x=\"" << num(lx) << "\" y=\"" << num(ly + 4)
          << "\" font-size=\"11.5\" fill=\"#3D4C5A\" text-anchor=\"middle\">"
          << esc(lab) << "</text>\n";
    }

    // ---- nodes -------------------------------------------------------------
    for (const auto& s : m.states) {
        const Pt& p = pos[s.id];

        bool sinkhole = s.reachable && s.transitions.empty() && !s.isFinal;
        NodeStyle st;
        if (!s.reachable)     st = { "#FDECEA", "#C0392B", "#8E2A1E", true  };
        else if (sinkhole)    st = { "#FDF3E3", "#C77700", "#8A5400", false };
        else                  st = { "#EAF2FA", "#1F4E79", "#12314D", false };

        // entry arrow for the initial state
        if (s.isInitial) {
            double a = std::atan2(p.y - cy, p.x - cx);
            double ox = std::cos(a), oy = std::sin(a);
            o << "  <path d=\"M" << num(p.x + ox * (nodeR + 46)) << "," << num(p.y + oy * (nodeR + 46))
              << " L" << num(p.x + ox * (nodeR + 9)) << "," << num(p.y + oy * (nodeR + 9))
              << "\" stroke=\"#1F4E79\" stroke-width=\"2.4\" fill=\"none\""
                 " marker-end=\"url(#arEntry)\"/>\n";
        }

        // final states get a double ring
        if (s.isFinal)
            o << "  <circle cx=\"" << num(p.x) << "\" cy=\"" << num(p.y) << "\" r=\"" << num(nodeR + 5)
              << "\" fill=\"none\" stroke=\"" << st.stroke << "\" stroke-width=\"1.6\"/>\n";

        o << "  <circle cx=\"" << num(p.x) << "\" cy=\"" << num(p.y) << "\" r=\"" << num(nodeR)
          << "\" fill=\"" << st.fill << "\" stroke=\"" << st.stroke << "\" stroke-width=\"2.2\"";
        if (st.dashed) o << " stroke-dasharray=\"6 4\"";
        o << "/>\n";

        // name, wrapped onto two lines if it is long
        std::string nm = s.display();
        if (nm.size() <= 11) {
            o << "  <text x=\"" << num(p.x) << "\" y=\"" << num(p.y + 4)
              << "\" font-size=\"12.5\" font-weight=\"600\" fill=\"" << st.text
              << "\" text-anchor=\"middle\">" << esc(nm) << "</text>\n";
        } else {
            size_t half = nm.find('.');
            if (half == std::string::npos || half > nm.size() - 2) half = nm.size() / 2;
            else ++half;   // keep the dot on the first line
            o << "  <text x=\"" << num(p.x) << "\" y=\"" << num(p.y - 2)
              << "\" font-size=\"11\" font-weight=\"600\" fill=\"" << st.text
              << "\" text-anchor=\"middle\">" << esc(nm.substr(0, half)) << "</text>\n"
              << "  <text x=\"" << num(p.x) << "\" y=\"" << num(p.y + 11)
              << "\" font-size=\"11\" font-weight=\"600\" fill=\"" << st.text
              << "\" text-anchor=\"middle\">" << esc(nm.substr(half)) << "</text>\n";
        }

        if (!s.reachable)
            o << "  <text x=\"" << num(p.x) << "\" y=\"" << num(p.y + nodeR + 18)
              << "\" font-size=\"10.5\" font-weight=\"600\" fill=\"#C0392B\""
                 " text-anchor=\"middle\">UNREACHABLE</text>\n";
        else if (sinkhole)
            o << "  <text x=\"" << num(p.x) << "\" y=\"" << num(p.y + nodeR + 18)
              << "\" font-size=\"10.5\" font-weight=\"600\" fill=\"#C77700\""
                 " text-anchor=\"middle\">SINKHOLE</text>\n";
    }

    // ---- legend ------------------------------------------------------------
    double ly = H - 52;
    const char* items[4][3] = {
        { "#EAF2FA", "#1F4E79", "state" },
        { "#FFFFFF", "#1F4E79", "final state (double ring)" },
        { "#FDF3E3", "#C77700", "sinkhole - no way out" },
        { "#FDECEA", "#C0392B", "unreachable" },
    };
    double lx = 28;
    for (auto& it : items) {
        o << "  <circle cx=\"" << num(lx + 8) << "\" cy=\"" << num(ly - 4) << "\" r=\"7\" fill=\""
          << it[0] << "\" stroke=\"" << it[1] << "\" stroke-width=\"2\"/>\n"
          << "  <text x=\"" << num(lx + 22) << "\" y=\"" << num(ly)
          << "\" font-size=\"11.5\" fill=\"#5A6B7D\">" << it[2] << "</text>\n";
        lx += 42 + std::string(it[2]).size() * 6.2;
    }
    o << "  <text x=\"28\" y=\"" << num(H - 24)
      << "\" font-size=\"11\" fill=\"#8A97A4\">[g] marks a guarded transition."
         "  Generated by aegisc --emit-svg.</text>\n";

    o << "</svg>\n";
    return o.str();
}

// ---------------------------------------------------------------- DOT ------
std::string renderDot(const Machine& m) {
    std::ostringstream o;
    o << "// Generated by aegisc --emit-dot.  Render with:  dot -Tpng -O " << m.name << ".dot\n"
      << "digraph " << m.name << " {\n"
      << "    rankdir=LR;\n"
      << "    node [shape=circle, style=filled, fontname=\"Helvetica\", fontsize=11];\n"
      << "    edge [fontname=\"Helvetica\", fontsize=10];\n"
      << "    __entry [shape=point, width=0.12, label=\"\"];\n\n";

    for (const auto& s : m.states) {
        bool sinkhole = s.reachable && s.transitions.empty() && !s.isFinal;
        o << "    " << s.name << " [label=\"" << s.display() << "\", ";
        if (!s.reachable)   o << "fillcolor=\"#FDECEA\", color=\"#C0392B\", style=\"filled,dashed\"";
        else if (sinkhole)  o << "fillcolor=\"#FDF3E3\", color=\"#C77700\"";
        else                o << "fillcolor=\"#EAF2FA\", color=\"#1F4E79\"";
        if (s.isFinal) o << ", shape=doublecircle";
        o << "];\n";
        if (s.isInitial) o << "    __entry -> " << s.name << ";\n";
    }
    o << "\n";

    for (const auto& s : m.states)
        for (const auto& t : s.transitions) {
            if (t.targetId < 0) continue;
            o << "    " << s.name << " -> " << t.target
              << " [label=\"" << t.event << (t.guard ? " [g]" : "") << "\"];\n";
        }

    o << "}\n";
    return o.str();
}

} // namespace aegis
