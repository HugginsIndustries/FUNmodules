/**
 * @file PolyQuanta.hpp
 * @author Christian Huggins (hugginsindustry@gmail.com)
 * @brief PolyQuanta — 16-channel polyphonic Swiss-army quantizer with slew/offset processor, musical quantizer (pre/post),
 *        dual-mode globals, syncable randomization, strum, range conditioning, and pop-free poly fades.
 * @version 2.0.2
 * @date 2025-09-19
 * @license GPL-3.0-or-later
 * @sdk VCV Rack 2.6.4
 * @ingroup FUNmodules
 * 
 * @copyright
 * Copyright (c) 2025 Huggins Industries
 * 
 * @details
 * This header file contains the forward declarations and includes needed for the PolyQuanta module.
 **/

 #pragma once

 #include <rack.hpp> // Include the Rack SDK
#include "plugin.hpp" // Include the main plugin header which provides access to VCV Rack's Module class and basic types
#include "core/PolyQuantaCore.hpp" // Core DSP functionality
#include "core/ScaleDefs.hpp" // Centralized musical scale definitions
#include "core/EdoTetPresets.hpp" // Curated presets for Equal Division of Octave (EDO) and Temperament (TET) systems
#include "core/Strum.hpp" // Strum timing functionality for creating delays between polyphonic channels
#include "core/ui/Quantities.hpp" // UI quantity classes for parameter display and input parsing
#include "core/ui/MenuHelpers.hpp" // UI menu helper functions for creating consistent context menus
#include "core/PanelExport.hpp" // Panel export functionality for generating SVG snapshots of the module layout

 using namespace rack;

// Forward declaration of the main module class
struct PolyQuanta;

// Forward declaration of the widget class
struct PolyQuantaWidget;

// External model declaration (defined in PolyQuanta.cpp)
extern Model* modelPolyQuanta;