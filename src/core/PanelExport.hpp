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
