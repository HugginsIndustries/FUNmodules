#pragma once
#include "../plugin.hpp"
#include <vector>
#include <string>

// PanelExport: extraction of the existing "Export layout SVG (user folder)" logic from PolyQuantaWidget.
// Provides a stable API so other module widgets could reuse the panel snapshot and overlay exporters.
// Behavior MUST remain identical to the original inline implementation (file names, styles, geometry, math).
// (Overlay export helper from original file is defined elsewhere; only panel snapshot API is required here.)

namespace PanelExport {
    // Export a rich panel snapshot SVG that embeds the panel artwork plus simplified component geometry
    // (knob bodies + pointer angle, switches, buttons, jacks, LEDs). The output path is the same location
    // and naming scheme as the original implementation unless outPath is explicitly provided.
    // Returns true on success (file created) and false on failure to open the output file.
    bool exportPanelSnapshot(rack::app::ModuleWidget* mw,
                             const std::string& moduleName,
                             const std::string& panelSvgRelPath,
                             const std::string& outPath = "");

}

// ----------------------------------------------------------------------------
// Overlay export support (migrated from original inline static implementation
// in PolyQuanta.cpp). The goal is to centralize the logic while keeping the
// original namespaces, types, and behavior completely unchanged.
// ----------------------------------------------------------------------------

namespace hi { namespace ui { namespace overlay {
    // Kind and Marker were originally declared inline in PolyQuanta.cpp. They
    // are hoisted here verbatim so other modules (and the forwarding wrapper
    // below) can reuse them without duplication.
    enum class Kind { Knob, Jack, Led, Button, Switch, Screw };
    struct Marker { Kind kind; float xMM; float yMM; float rMM; };
    // Helper to map Kind -> CSS class name (unchanged semantics).
    inline const char* cls(Kind k) {
        switch (k) { case Kind::Knob: return "knob"; case Kind::Jack: return "jack"; case Kind::Led: return "led";
            case Kind::Button: return "btn"; case Kind::Switch: return "sw"; case Kind::Screw: return "screw"; }
        return "mark";
    }
}}} // namespace hi::ui::overlay

namespace PanelExport {
    // Export an overlay-only SVG (outline + component circles / crosses).
    // Mirrors the original static inline function signature and default.
    bool exportOverlay(const std::string& moduleName,
                       float wMM,
                       float hMM,
                       const std::vector<hi::ui::overlay::Marker>& marks,
                       const std::string& outPath = "");
}

// Backwards compatibility: preserve hi::ui::overlay::exportOverlay symbol so
// any existing call sites remain valid. This is a thin inline forwarder that
// simply invokes PanelExport::exportOverlay().
namespace hi { namespace ui { namespace overlay {
    inline bool exportOverlay(const std::string& moduleName,
                               float wMM,
                               float hMM,
                               const std::vector<Marker>& marks,
                               const std::string& outPath = "") {
        return PanelExport::exportOverlay(moduleName, wMM, hMM, marks, outPath);
    }
}}} // namespace hi::ui::overlay (forwarder)
