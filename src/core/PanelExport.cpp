#include "PanelExport.hpp"
#include "../plugin.hpp" // for pluginInstance and Rack core symbols
#include <rack.hpp>
#include <app/ModuleWidget.hpp>
#include <app/ParamWidget.hpp>
#include <app/PortWidget.hpp>
#include <app/LightWidget.hpp>
#include <cmath>
#include <fstream>
#include <typeinfo>

// NOTE: This file contains code MOVED from PolyQuanta.cpp (PolyQuantaWidget context menu lambda)
// for the "Export layout SVG (user folder)" feature. The logic, math, strings, CSS, and output
// file naming are preserved EXACTLY to ensure behavior parity. Only minimal refactoring into
// functions has been performed, and explanatory comments were added.

namespace PanelExport {

using namespace rack;

bool exportPanelSnapshot(rack::app::ModuleWidget* mw,
                         const std::string& moduleName,
                         const std::string& panelSvgRelPath,
                         const std::string& outPath) {
    if (!mw) return false; // defensive

    // 1) Recreate original constants / conversions (identical to previous lambda)
    const float pxPerMM = rack::app::RACK_GRID_WIDTH / 5.08f; // Rack constant: 1 HP = 5.08 mm
    const float wMM = mw->box.size.x / pxPerMM;
    const float hMM = rack::app::RACK_GRID_HEIGHT / pxPerMM;

    // 2) Load panel SVG (same path construction) and strip outer <svg> wrapper.
    std::string panelPath = ::rack::asset::plugin(::pluginInstance, panelSvgRelPath);
    std::ifstream pf(panelPath, std::ios::binary);
    std::string panelSrc; if (pf) panelSrc.assign(std::istreambuf_iterator<char>(pf), std::istreambuf_iterator<char>());
    auto stripOuterSvg = [](const std::string& src)->std::string {
        size_t open = src.find("<svg"); if (open == std::string::npos) return src;
        size_t gt = src.find('>', open); if (gt == std::string::npos) return src;
        size_t close = src.rfind("</svg"); if (close == std::string::npos || close <= gt) return src.substr(gt+1);
        return src.substr(gt+1, close - (gt+1));
    };
    std::string panelInner = stripOuterSvg(panelSrc);

    // 3) Prepare output directory & filename – identical naming (moduleName + "-panel-snapshot.svg")
    std::string dir = ::rack::asset::user(::rack::string::f("%s/overlays", ::pluginInstance->slug.c_str()));
    ::rack::system::createDirectories(dir);
    std::string finalPath = outPath.empty() ? (dir + "/" + moduleName + "-panel-snapshot.svg") : outPath;
    std::ofstream out(finalPath, std::ios::binary); if (!out) return false;

    auto pxToMM = [pxPerMM](float px){ return px / pxPerMM; };

    // 4) Emit SVG header, defs, and embedded panel artwork group EXACTLY as original.
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << rack::string::f("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.3fmm\" height=\"%.3fmm\" viewBox=\"0 0 %.3f %.3f\" font-family=\"ShareTechMono,monospace\" font-size=\"3.2\" stroke-linejoin=\"round\" stroke-linecap=\"round\">\n", wMM, hMM, wMM, hMM);
    out <<
        "  <defs>\n"
        "    <style><![CDATA[\n"
        "      .knob-body{fill:#222;stroke:#888;stroke-width:0.3}\n"
        "      .knob-pointer{stroke:#ffb300;stroke-width:0.45}\n"
        "      .jack{fill:#111;stroke:#5c6bc0;stroke-width:0.35}\n"
        "      .btn{fill:#303030;stroke:#aaa;stroke-width:0.35}\n"
        "      .sw{fill:#252525;stroke:#ba68c8;stroke-width:0.35}\n"
        "      .led{fill:#000}\n"
        "      .panel-group *{vector-effect:non-scaling-stroke}\n"
        "    ]]></style>\n"
        "  </defs>\n";
    out << "  <g class=\"panel-group\" id=\"panelArtwork\">\n" << panelInner << "\n  </g>\n";
    out << "  <g id=\"components\">\n";

    // 5) Iterate widget children and write simplified geometry (same classification logic).
    for (widget::Widget* w : mw->children) {
        if (!w) continue;
    math::Vec center = w->box.getCenter();
        float cxMM = pxToMM(center.x);
        float cyMM = pxToMM(center.y);
        std::string cls = typeid(*w).name();

    if (dynamic_cast<app::LightWidget*>(w)) {
            float r = 1.2f; // approximate radius (identical)
            out << rack::string::f("    <circle class=\"led\" cx=\"%.3f\" cy=\"%.3f\" r=\"%.3f\"/>\n", cxMM, cyMM, r);
            continue;
        }

    if (auto* pw = dynamic_cast<app::ParamWidget*>(w)) {
            float r = pxToMM(std::max(w->box.size.x, w->box.size.y) * 0.5f);
            bool isKnob = (cls.find("Knob") != std::string::npos) || (cls.find("Trimpot") != std::string::npos);
            bool isSwitch = (cls.find("CKSS") != std::string::npos) || (cls.find("Switch") != std::string::npos);
            bool isButton = (cls.find("Button") != std::string::npos);
            rack::engine::ParamQuantity* pq = pw->getParamQuantity();
            if (isKnob) {
                float bodyR = r * 0.95f;
                out << rack::string::f("    <circle class=\"knob-body\" cx=\"%.3f\" cy=\"%.3f\" r=\"%.3f\"/>\n", cxMM, cyMM, bodyR);
                if (pq) {
                    float v = pq->getValue();
                    float norm = 0.f;
                    float minV = pq->getMinValue();
                    float maxV = pq->getMaxValue();
                    if (maxV > minV) norm = (v - minV) / (maxV - minV);
                    float angleDeg = -150.f + 300.f * math::clamp(norm, 0.f, 1.f);
                    float ang = angleDeg * (float)M_PI / 180.f;
                    float pr = bodyR * 0.78f;
                    float px2 = cxMM + pr * std::sin(ang);
                    float py2 = cyMM - pr * std::cos(ang);
                    out << rack::string::f("    <line class=\"knob-pointer\" x1=\"%.3f\" y1=\"%.3f\" x2=\"%.3f\" y2=\"%.3f\"/>\n", cxMM, cyMM, px2, py2);
                }
                continue;
            }
            if (isSwitch) {
                float wMMb = pxToMM(w->box.size.x);
                float hMMb = pxToMM(w->box.size.y);
                out << rack::string::f("    <rect class=\"sw\" x=\"%.3f\" y=\"%.3f\" width=\"%.3f\" height=\"%.3f\" rx=\"0.8\" ry=\"0.8\"/>\n", cxMM - wMMb*0.5f, cyMM - hMMb*0.5f, wMMb, hMMb);
                continue;
            }
            if (isButton) {
                out << rack::string::f("    <circle class=\"btn\" cx=\"%.3f\" cy=\"%.3f\" r=\"%.3f\"/>\n", cxMM, cyMM, r*0.85f);
                continue;
            }
            // Generic param fallback (unchanged)
            out << rack::string::f("    <circle class=\"knob-body\" cx=\"%.3f\" cy=\"%.3f\" r=\"%.3f\"/>\n", cxMM, cyMM, r*0.6f);
            continue;
        }

    if (dynamic_cast<app::PortWidget*>(w)) {
            float rr = pxToMM(std::max(w->box.size.x, w->box.size.y) * 0.5f) * 0.85f;
            out << rack::string::f("    <circle class=\"jack\" cx=\"%.3f\" cy=\"%.3f\" r=\"%.3f\"/>\n", cxMM, cyMM, rr);
            continue;
        }
    }
    out << "  </g>\n";
    out << "</svg>\n";
    return true;
}

// ----------------------------------------------------------------------------
// Overlay exporter implementation (moved from PolyQuanta.cpp). Produces a
// minimalist SVG containing the panel outline plus marker circles and cross
// hairs for each provided Marker. File naming, CSS classes, and structure are
// preserved exactly ("<moduleName>-overlay.svg"). Units are millimeters.
// ----------------------------------------------------------------------------
bool exportOverlay(const std::string& moduleName,
                float wMM,
                float hMM,
                const std::vector<hi::ui::overlay::Marker>& marks,
                const std::string& outPath) {
    std::string dir = ::rack::asset::user(::rack::string::f("%s/overlays", ::pluginInstance->slug.c_str()));
    ::rack::system::createDirectories(dir);
    std::string path = outPath.empty() ? (dir + "/" + moduleName + "-overlay.svg") : outPath;
    std::ofstream f(path, std::ios::binary); if (!f) return false;
    f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    f << ::rack::string::f("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.3fmm\" height=\"%.3fmm\" viewBox=\"0 0 %.3f %.3f\">\n", wMM, hMM, wMM, hMM);
    f << "  <defs>\n"
        "    <style><![CDATA[\n"
        "      .outline{fill:none;stroke:#888;stroke-width:0.3}\n"
        "      .knob{fill:none;stroke:#ff9800;stroke-width:0.3}\n"
        "      .jack{fill:none;stroke:#3f51b5;stroke-width:0.3}\n"
        "      .led{fill:none;stroke:#4caf50;stroke-width:0.25}\n"
        "      .sw{fill:none;stroke:#9c27b0;stroke-width:0.25}\n"
        "      .btn{fill:none;stroke:#795548;stroke-width:0.3}\n"
        "      .screw{fill:none;stroke:#607d8b;stroke-width:0.25}\n"
        "      .x{stroke:#999;stroke-width:0.2;stroke-dasharray:0.6,0.6}\n"
        "    ]]></style>\n"
        "  </defs>\n";
    f << ::rack::string::f("  <rect class=\"outline\" x=\"0\" y=\"0\" width=\"%.3f\" height=\"%.3f\"/>\n", wMM, hMM);
    auto cross = [&](float x, float y){ const float c = 2.5f; f << ::rack::string::f("  <path class=\"x\" d=\"M %.3f %.3f H %.3f M %.3f %.3f V %.3f\"/>\n", x - c, y, x + c, x, y - c, y + c); };
    auto circle = [&](const char* cls, float x, float y, float r){ f << ::rack::string::f("  <circle class=\"%s\" cx=\"%.3f\" cy=\"%.3f\" r=\"%.3f\"/>\n", cls, x, y, r); };
    for (const auto& m : marks) { circle(hi::ui::overlay::cls(m.kind), m.xMM, m.yMM, m.rMM); cross(m.xMM, m.yMM); }
    f << "</svg>\n"; return true;
}

} // namespace PanelExport
