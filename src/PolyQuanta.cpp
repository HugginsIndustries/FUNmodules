/**
 * @file PolyQuanta.cpp
 * @author Christian Huggins (hugginsindustry@gmail.com)
 * @brief PolyQuanta — 16-channel polyphonic Swiss-army quantizer with slew/offset processor, musical quantizer (pre/post),
 *        dual-mode globals, syncable randomization, strum, range conditioning, and pop-free poly fades.
 * @version 2.0.2
 * @date 2025-09-16
 * @license GPL-3.0-or-later
 * @sdk VCV Rack 2.6.4
 * @ingroup FUNmodules
 * 
 * @copyright
 * Copyright (c) 2025 Huggins Industries
 * 
 * @details
 * # Purpose & Design
 * PolyQuanta shapes polyphonic CV/pitch lines by combining per-channel slew and offset with a
 * blendable quantizer that you can place **PRE** or **POST** slew. It separates local per-channel
 * control from global gestures (dual-mode globals), keeps the audio path alloc-free, and manages
 * poly width changes with fade ramps to avoid clicks.
 *
 * ## Musical Role
 * Glide-and-land for pitch lines and stacked chords; precise CV conditioning with safe output limits.
 *
 * ## Constraints
 * - Alloc-free and lock-free in the audio thread.
 * - Deterministic per-channel loop for up to 16 channels.
 * - Pop-free channel-count transitions (fade-managed).
 * - JSON-stable state for preset compatibility.
 *
 * # Signal Flow (per channel)
 * `In → [Pre-range: Scale/Offset] → [Global attenuverter (optional)] → Slew (pitch-safe opt.)
 *  → Quantizer (PRE/POST; strength blend; rounding + hysteresis) → [Global/Range offsets mix] → Out`
 *
 * Polyphony: up to 16 channels; forced or auto width; width changes are crossfaded.
 *
 * # Ports
 * **Inputs**
 * - `IN_INPUT` — Main poly signal (typically ±10 V).
 * - `RND_TRIG_INPUT` — Gate/clock for randomization (manual or sync timing).
 *
 * **Outputs**
 * - `OUT_OUTPUT` — Processed poly signal (or summed mono if enabled).
 *
 * **Params (groups)**
 * - Per-channel Slew: `SL1_PARAM … SL16_PARAM` — time (exp mapping).
 * - Per-channel Offset: `OFF1_PARAM … OFF16_PARAM` — voltage offset; per-channel snap modes.
 * - (Legacy) Per-channel Quantize toggles: `QZ1_PARAM … QZ16_PARAM` — kept for preset compat.
 * - Global Shapes: `RISE_SHAPE_PARAM`, `FALL_SHAPE_PARAM` — curve shape (log/lin/exp).
 * - Randomization: `RND_PARAM` (momentary), `RND_TIME_PARAM` (free/log or sync ratios),
 *   `RND_AMT_PARAM` (0–100%), `RND_AUTO_PARAM` (on/off), `RND_SYNC_PARAM` (free vs sync).
 * - Dual-mode Global Slew: `GLOBAL_SLEW_PARAM` + `GLOBAL_SLEW_MODE_PARAM`
 *   - Mode A = **Slew add** (time)  Mode B = **Attenuverter** (gain).
 * - Dual-mode Global Offset: `GLOBAL_OFFSET_PARAM` + `GLOBAL_OFFSET_MODE_PARAM`
 *   - Mode A = **Global offset** (±10 V) Mode B = **Range offset** (±5 V; pre-quant/post-range).
 *
 * **Lights**
 * - 16 × bipolar channel indicators (activity/polarity).
 *
 * # DSP Core
 * - **Slew/glide** with independent rise/fall shaping; optional **pitch-safe** normalization (1 V/oct).
 * - **Quantizer** with **PRE/POST** placement and **strength** blend (0% raw → 100% snapped).
 * - **Rounding**: Directional (slope-aware), Nearest, Ceil, Floor.
 * - **Hysteresis (stickiness)** in cents; center-anchored Schmitt latch to avoid boundary chatter.
 * - **Strum**: up/down/random; time-stretch or start-delay behavior.
 * - **Output safety**: soft-clip or hard clamp. **Range conditioning**: Clip/Scale around 0 V with selectable Vpp.
 *
 * ## Invariants
 * 1) Root-relative scale masks only (no chromatic leakage).  
 * 2) Center-anchored hysteresis; stable latching; at most ±1 allowed step per event.  
 * 3) Directional Snap uses the **latched** step and slope sign to pick the next degree.
 *
 * # Randomization
 * - **Manual**: front-panel button or `RND_TRIG_INPUT` gate.
 * - **Auto (free)**: logarithmic time (≈1 ms → 10,000 s).
 * - **Auto (sync)**: ratio grid ÷64..×64 from measured clock with smoothing.
 * - **Scope**: toggles for Slews, Offsets, Shapes; **Amount** 0..100%.
 * - **Per-mode knob memory** preserves your time setting when switching Free/Sync.
 *
 * # Strum
 * - Enable/disable; **Behavior**: Time-stretch vs Start-delay; **Direction**: Up/Down/Random;
 *   **Spread** presets in ms. Sensible defaults when enabling.
 *
 * # Timing & Threading
 * **Audio thread**
 * - Snapshot params, read inputs; compute targets; apply strum delays; perform range conditioning,
 *   slew, quantization, and output limiting/summing; manage fade ramps for poly width changes.
 *
 * **UI/Menu thread**
 * - Context menus for batch operations, snap modes, dual-mode globals, quantization, range/safety,
 *   randomization scope, strum, and status readouts. Panel snapshot export utility.
 *
 * # State & Persistence (JSON)
 * - **Poly & output**: `forcedChannels`, `sumToMonoOut`, `avgWhenSumming`, `softClipOut`, `polyFadeSec`.
 * - **Range & safety**: `clipVppIndex` (20/15/10/5/2/1 V), `rangeMode` (0=Clip, 1=Scale).
 * - **Globals**: always-on flags for attenuverter/slew/offset; dual-mode banks for Slew/Offset +
 *   current mode selectors.
 * - **Randomization**: `rndAutoEnabled`, `rndSyncMode`, `rndTimeRawFree`, `rndTimeRawSync`,
 *   `randMaxPct`, scope flags for Slews/Offsets/Shapes.
 * - **Per-channel**: `preScale[16]`, `preOffset[16]`, `qzEnabled[16]`, `postOctShift[16]`,
 *   `slewDisabledMask` (bitfield), per-channel `snapOffsetMode`.
 * - **Quantizer**: `quantizerPos` (Pre/Post), `quantStrength` (0..1).
 * - **Tuning** via CoreState: `edo`, `tuningMode`, `tetSteps`, `tetPeriodOct`, `rootNote`,
 *   `scaleIndex`, masks for 12/24/generic.
 * - **Migrations**: legacy QZ* preserved; classic “Quantize→Slew” patches map to **Pre**.
 *
 * # UI & Menus (highlights)
 * - **Output**: channel count (Auto or 1–16), poly fade time presets, sum-to-mono (+avg),
 *   soft-clip toggle, Vpp selection, range mode (Clip/Scale), panel export.
 * - **Quantization**: PRE vs POST, strength %, rounding & stickiness, EDO/TET selection,
 *   custom masks (12/24/generic), per-channel octave shifts, status line.
 * - **Randomize Scope**: Slews / Offsets / Shapes.
 * - **Per-param menus**: per-channel offset snap (Volts/Semitones/Cents); slew enable/disable
 *   (including batch ALL/ODD/EVEN); dual-mode globals with “always-on” overrides.
 *
 * # Failure Modes / Notes
 * - Scope-locked targets are not randomized.
 * - In POST with strength < 100%, glide can pull off-scale (set 100% for strict post-slew quant).
 * - Range mode **Scale** compresses toward 0 V; **Clip** limits (or soft-clips if enabled).
 *
 * # Extensibility
 * - New params/ports: extend *_Id enums and `config*`; update JSON and CoreState glue; keep
 *   ParamQuantity helpers near their feature clusters.
 * - New scales/tunings: extend CoreState + mask editors; preserve bit widths (12/24) and generic vector.
 * - Larger features: consider extracting helper structs adjacent to CoreState; keep audio path alloc-free.
 *
 * # Implementation Index (jump points; current file)
 * - ParamIds enum .............................................. line ~332
 * - InputId / OutputId / LightId enums .......................... lines ~371 / ~378 / ~385
 * - Module class (`struct PolyQuanta`) .......................... line ~330
 * - Constructor ................................................. line ~886
 * - process(const ProcessArgs&) .................................. line ~1925
 * - onReset() ................................................... line ~1752
 * - dataToJson() / dataFromJson() ............................... lines ~1332 / ~1509
 * - CoreState glue (fill/apply) ................................. lines ~2926 / ~2972
 * - Widget (`struct PolyQuantaWidget`) .......................... line ~3292
 * - Widget::appendContextMenu() (main module menu) .............. line ~3851
 * - UI construction (addParam/addInput/…) ....................... line ~3662 onward
 * - Quantization menu section ................................... ~3966–4051
 * - Output range & safety options ............................... ~3854–3965
 * - Strum menu .................................................. ~4816–4861
 */

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "PolyQuanta.hpp" // Include the main module header
// Standard C++ library includes for various functionality:
#include <cstdio>        // For C-style I/O functions like snprintf
#include <cctype>        // For character classification functions like isdigit, tolower
#include <limits>        // For numeric limits (std::numeric_limits)
#include <fstream>       // For file stream operations (not used in current code)
#include <unordered_set> // For hash-based set containers
#include <set>           // For ordered set containers
#include <map>           // For associative containers (key-value pairs)
#include <cmath>         // For mathematical functions like round, log2, pow
#include <vector>        // For dynamic arrays
#include <string>        // For string manipulation
#include <algorithm>     // For algorithms like min_element, sort, unique
#include <cstdint>       // For fixed-width integer types like uint32_t

// -----------------------------------------------------------------------------
// Inline helpers
// -----------------------------------------------------------------------------

// MOS (Moment of Symmetry) helper functions for musical scale analysis
// These functions detect and build scales that have rotational symmetry properties
namespace hi { namespace music { namespace mos {
    // Build a custom scale mask from a cycle of pitch classes
    // mod: pointer to module instance to modify
    // N: number of divisions in the tuning system (EDO steps)
    // pcs: vector of pitch class indices that form the scale
    void buildMaskFromCycle(PolyQuanta* mod, int N, const std::vector<int>& pcs);
    
    // Detect if the current custom scale is a Moment of Symmetry (MOS) scale
    // MOS scales have special mathematical properties and sound balanced
    // mod: pointer to module instance to analyze
    // mOut: output parameter for scale size (number of notes)
    // gOut: output parameter for generator step size
    // Returns: true if current scale is detected as MOS, false otherwise
    bool detectCurrentMOS(PolyQuanta* mod, int& mOut, int& gOut);
} } }

// Scale detection utilities
namespace hi { namespace music { namespace scale {
    // Detect if a custom scale matches any predefined scale for the given EDO
    // customMask: the custom scale mask to check
    // edo: the EDO system (1-120)
    // Returns: pointer to matching scale, or nullptr if no match found
    const Scale* detectMatchingScale(const std::vector<uint8_t>& customMask, int edo);
    
    // Check if two scale masks are equivalent (accounting for root note rotation)
    // mask1, mask2: scale masks to compare
    // Returns: true if masks represent the same scale (possibly rotated)
    bool masksEqual(const std::vector<uint8_t>& mask1, const std::vector<uint8_t>& mask2);
} } }

// LED control utilities for bipolar (green/red) channel activity indicators
namespace hi { namespace ui { namespace led {
// Set brightness of bipolar LED pair based on positive/negative voltage
// Positive voltages light the green LED, negative voltages light the red LED
// g: reference to green LED light object
// r: reference to red LED light object  
// val: input voltage value (positive = green, negative = red)
// dt: delta time for smooth brightness transitions
static inline void setBipolar(rack::engine::Light& g, rack::engine::Light& r, float val, float dt) {
    // Scale positive voltage to green LED brightness (0.0 to 1.0)
    float gs = rack::clamp( val / hi::consts::LED_SCALE_V, 0.f, 1.f);
    // Scale negative voltage to red LED brightness (0.0 to 1.0)
    float rs = rack::clamp(-val / hi::consts::LED_SCALE_V, 0.f, 1.f);
    // Apply smooth brightness changes to avoid LED flickering
    g.setBrightnessSmooth(gs, dt); 
    r.setBrightnessSmooth(rs, dt);
}
}}} // namespace hi::ui::led

// Dual-mode control utilities for knobs that can switch between two different functions
namespace hi { namespace ui { namespace dual {
// Template structure for storing two values and a mode flag
// Used for knobs that can switch between two different parameter types
// T: the data type for the stored values (typically float)
template<typename T>
struct DualBank { 
    T a{};           // Value for mode A (e.g., slew-add time)
    T b{};           // Value for mode B (e.g., attenuverter gain)
    bool mode = false; // Current mode: false = use 'a', true = use 'b'
    
    // Synchronize knob position when mode is toggled
    // Updates the physical knob value to match the stored value for the new mode
    void syncOnToggle(float& knobVal) const { 
        knobVal = mode ? (float)b : (float)a; 
    } 
};

// Utility functions for converting between raw knob values and attenuverter gain
struct AttenuverterMap { 
    // Convert raw knob position [0,1] to attenuverter gain [-10,+10]
    static inline float rawToGain(float raw) { 
        raw = rack::clamp(raw, 0.f, 1.f); 
        return -10.f + 20.f * raw; 
    } 
    
    // Convert attenuverter gain [-10,+10] to raw knob position [0,1]
    static inline float gainToRaw(float g) { 
        float r = (g + 10.f) / 20.f; 
        return rack::clamp(r, 0.f, 1.f); 
    } 
};
}}} // namespace hi::ui::dual

// Random number utilities for parameter randomization
namespace hi { namespace util { namespace rnd {
// Generate a random delta value within a symmetric range
// width: maximum absolute deviation from zero
// Returns: random value in range [-width, +width]
static inline float delta(float width) { 
    return (2.f * rack::random::uniform() - 1.f) * width; 
}

// Apply random change to a value within specified bounds
// v: reference to value to be randomized (modified in place)
// lo: minimum allowed value after randomization
// hi: maximum allowed value after randomization  
// maxPct: maximum change as percentage of total range (0.0 to 1.0)
static inline void randSpanClamp(float& v, float lo, float hi, float maxPct) { 
    float span = hi - lo;  // Calculate total parameter range
    if (span <= 0.f) return;  // Skip if invalid range
    float dv = delta(maxPct * span);  // Generate random change
    v = rack::clamp(v + dv, lo, hi);  // Apply change and clamp to bounds
}
}}} // namespace hi::util::rnd

// JSON helper utilities for reading and writing boolean values
namespace hi { namespace util { namespace jsonh {
// Write a boolean value to a JSON object
// root: JSON object to write to
// key: string key for the boolean value
// value: boolean value to store
static inline void writeBool(::json_t* root, const char* key, bool value) { 
    json_object_set_new(root, key, json_boolean(value)); 
}

// Read a boolean value from a JSON object with default fallback
// root: JSON object to read from (can be null)
// key: string key to look up
// def: default value to return if key not found or root is null
// Returns: boolean value from JSON, or default if not found
static inline bool readBool(::json_t* root, const char* key, bool def) { 
    if (!root) return def;  // Return default if no JSON object
    if (auto* j = json_object_get(root, key)) { 
        return json_boolean_value(j);  // Extract boolean value
    } 
    return def;  // Return default if key not found
}
}}} // namespace hi::util::jsonh

// Polyphony transition utilities for smooth channel count changes
namespace hi { namespace dsp { namespace polytrans {
// Enumeration for polyphony transition phases
enum Phase { 
    TRANS_STABLE = 0,   // Normal operation, no transition in progress
    TRANS_FADE_OUT,     // Fading out before changing channel count
    TRANS_FADE_IN       // Fading in after changing channel count
};

// State structure for managing polyphony transitions
struct State { 
    int curProcN = 0;           // Current number of processing channels
    int curOutN = 0;            // Current number of output channels
    int pendingProcN = 0;       // Target number of processing channels
    int pendingOutN = 0;        // Target number of output channels
    float polyRamp = 1.f;       // Fade multiplier (0.0 = silent, 1.0 = full volume)
    Phase transPhase = TRANS_STABLE;  // Current transition phase
    bool initToTargetsOnSwitch = false;  // Flag to reinitialize on channel switch
};
}}} // namespace hi::dsp::polytrans
// Import VCV Rack namespaces for convenience
using namespace rack;                    // Core VCV Rack types and functions
using namespace rack::componentlibrary;  // Standard UI components (knobs, ports, etc.)
namespace hconst = hi::consts;           // Alias for audio/math constants
// Import transition phase enum values for brevity in this file
using namespace hi::dsp::polytrans;

// Forward declarations for CoreState serialization functions
// These functions bridge between the module's state and a centralized core state structure
// Definitions are placed after the full PolyQuanta class to avoid incomplete-type errors

// Copy quantization settings from module instance to core state structure
// Used during JSON serialization to centralize quantization data
static void fillCoreStateFromModule(const struct PolyQuanta& m, hi::dsp::CoreState& cs) noexcept;

// Apply quantization settings from core state structure to module instance  
// Used during JSON deserialization to restore quantization data
static void applyCoreStateToModule(const hi::dsp::CoreState& cs, struct PolyQuanta& m) noexcept;

/*
-----------------------------------------------------------------------
 Section: Forward declarations and ParamQuantity helpers
 - Forward‑declare the module type for dynamic_cast inside quantities.
 - OffsetQuantity customizes display/parse to show semitones when the
     module prefers quantized offsets, and volts otherwise.
 - Additional ParamQuantity subclasses appear later (global dual‑mode
     controls, shape quantities) and are defined inline in the ctor.
-----------------------------------------------------------------------
*/
// Forward declaration for OffsetQuantity dynamic_cast
struct PolyQuanta;
// (Auto-randomize ParamQuantities are defined locally in constructor to allow
// access to full PolyQuanta type without separate forward dependency.)

// Keep a minimal alias for legacy name but back it by shared SemitoneVoltQuantity
struct OffsetQuantity : hi::ui::SemitoneVoltQuantity {};

/*
-----------------------------------------------------------------------
 Section: Module class (PolyQuanta)
 - Comprehensive 16-channel polyphonic slew/glide processor with advanced
   musical quantization, dual-mode controls, and sophisticated randomization
-----------------------------------------------------------------------

This is the main module class that implements PolyQuanta's complete feature set.
It serves as both the VCV Rack module interface and the central state manager
for all DSP processing, user interface, and persistence operations.

┌─ VCV RACK INTERFACE DECLARATIONS ───────────────────────────────────────────┐
│ • Parameter enumeration: 50+ parameters including per-channel controls,     │
│   global shape parameters, dual-mode knobs, and randomization controls      │
│ • Input/Output ports: polyphonic main I/O and randomization trigger input   │
│ • LED indicators: 32 lights (16 channels × 2 colors) for voltage display    │
│ • Static parameter index arrays: channel-to-enum mapping for DSP loops      │
└─────────────────────────────────────────────────────────────────────────────┘

┌─ DSP STATE VARIABLES ───────────────────────────────────────────────────────┐
│ • Per-channel slew limiters: 16 independent smooth transition processors    │
│ • Step tracking arrays: direction and magnitude detection for strum timing  │
│ • Performance optimization: cached slew rates and last outputs              │
│ • Trigger detection: randomization button and external gate processing      │
└─────────────────────────────────────────────────────────────────────────────┘

┌─ MODULE CONFIGURATION OPTIONS ──────────────────────────────────────────────┐
│ • Polyphony control: forced channel counts, mono summing, averaging modes   │
│ • DSP processing: pitch-safe glide, soft clipping, voltage range selection  │
│ • Range enforcement: clip vs scale modes for pre-quantization conditioning  │
│ • Offset quantization: per-channel snap modes (voltage/semitones/cents)     │
└─────────────────────────────────────────────────────────────────────────────┘

┌─ ADVANCED DUAL-MODE GLOBAL CONTROLS ────────────────────────────────────────┐
│ • Sophisticated knob banking: each global control has two independent       │
│   functions with value persistence across mode switches                     │
│ • Cross-mode application: "always on" flags allow simultaneous use of       │
│   both knob functions for maximum flexibility                               │
│ • Global slew: Mode A (slew-add time) ↔ Mode B (attenuverter gain)          │
│ • Global offset: Mode A (global offset) ↔ Mode B (range offset)             │
└─────────────────────────────────────────────────────────────────────────────┘

┌─ STRUM TIMING SYSTEM ────────────────────────────────────────────────────────┐
│ • Sequential channel triggering: up/down/random patterns for chord effects   │
│ • Dual timing modes: time-stretch (extends glide) vs start-delay (holds)     │
│ • Runtime state tracking: per-channel delay assignment and countdown         │
│ • Musical articulation: configurable inter-channel delays in milliseconds    │
└──────────────────────────────────────────────────────────────────────────────┘

┌─ SOPHISTICATED QUANTIZATION ENGINE ──────────────────────────────────────────┐
│ • Multi-tuning support: EDO (octave-based) and TET (non-octave) systems      │
│ • Scale flexibility: preset scales, custom masks, and MOS pattern detection  │
│ • Advanced quantizer behavior: directional snap, hysteresis, rounding modes  │
│ • Per-channel controls: individual enable/disable and octave shifting        │
│ • Configuration tracking: change detection for smooth state transitions      │
└──────────────────────────────────────────────────────────────────────────────┘

┌─ COMPREHENSIVE RANDOMIZATION SYSTEM ─────────────────────────────────────────┐
│ • Flexible scope control: per-parameter-type inclusion/exclusion             │
│ • Advanced timing: free-running logarithmic scale and clock synchronization  │
│ • Clock sync features: division/multiplication ratios with precise timing    │
│ • Fine-grained control: per-parameter locks and allows for surgical control  │
│ • Auto-randomization: sophisticated engine with external clock integration   │
└──────────────────────────────────────────────────────────────────────────────┘

┌─ POLYPHONY TRANSITION SYSTEM ────────────────────────────────────────────────┐
│ • Pop-free channel switching: fade-out → switch → fade-in state machine      │
│ • Configurable fade times: smooth transitions prevent audio artifacts        │
│ • Intelligent target reinitialization: prevents output jumps during fades    │
│ • Centralized state management: robust handling of complex transition logic  │
└──────────────────────────────────────────────────────────────────────────────┘

┌─ UTILITY METHODS & HELPERS ──────────────────────────────────────────────────┐
│ • quantizeToScale(): Musical voltage quantizer with full configuration       │
│ • currentClipLimit(): Voltage range mapper for consistent limiting           │
│ • MOS detection cache: performance optimization for UI menu generation       │
│ • Hash fingerprinting: efficient scale configuration change detection        │
└──────────────────────────────────────────────────────────────────────────────┘

┌─ CORE METHODS (see detailed documentation below) ────────────────────────────┐
│ • Constructor: Comprehensive parameter setup, custom quantities, I/O ports,  │
│   dual-mode banking, randomization system, and runtime state initialization  │
│ • process(): Main DSP loop with two-pass architecture, polyphony management, │
│   randomization engine, slew processing, quantization, and output handling   │
└──────────────────────────────────────────────────────────────────────────────┘

The PolyQuanta class represents a sophisticated balance between powerful features
and maintainable code organization. Its modular state management and extensive
configuration options support both simple and complex musical workflows while
maintaining real-time performance for up to 16 simultaneous channels.
*/

// Main module class - 16-channel polyphonic slew/glide processor with quantization
struct PolyQuanta : Module {
    // Parameter enumeration - defines all knobs, buttons, and controls
    enum ParamId {
        // Per-channel slew (SL) and offset (OFF) parameters for 16 channels
        // SL parameters control slew/glide time with exponential scaling
        // OFF parameters control voltage offset with optional semitone quantization
        SL1_PARAM, SL2_PARAM, OFF1_PARAM, OFF2_PARAM,     // Channel 1-2
        SL3_PARAM, SL4_PARAM, OFF3_PARAM, OFF4_PARAM,     // Channel 3-4
        SL5_PARAM, SL6_PARAM, OFF5_PARAM, OFF6_PARAM,     // Channel 5-6
        SL7_PARAM, SL8_PARAM, OFF7_PARAM, OFF8_PARAM,     // Channel 7-8
        SL9_PARAM, SL10_PARAM, OFF9_PARAM, OFF10_PARAM,   // Channel 9-10
        SL11_PARAM, SL12_PARAM, OFF11_PARAM, OFF12_PARAM, // Channel 11-12
        SL13_PARAM, SL14_PARAM, OFF13_PARAM, OFF14_PARAM, // Channel 13-14
        SL15_PARAM, SL16_PARAM, OFF15_PARAM, OFF16_PARAM, // Channel 15-16
        // Per-channel quantization enable parameters (legacy - now handled via menu)
        // These were originally front-panel buttons but are now controlled via context menus
        QZ1_PARAM, QZ2_PARAM, QZ3_PARAM, QZ4_PARAM,       // Channels 1-4
        QZ5_PARAM, QZ6_PARAM, QZ7_PARAM, QZ8_PARAM,       // Channels 5-8
        QZ9_PARAM, QZ10_PARAM, QZ11_PARAM, QZ12_PARAM,    // Channels 9-12
        QZ13_PARAM, QZ14_PARAM, QZ15_PARAM, QZ16_PARAM,   // Channels 13-16
        // Global shape parameters for slew curve control
        RISE_SHAPE_PARAM,         // Rise curve shape: -1=logarithmic, 0=linear, +1=exponential
        FALL_SHAPE_PARAM,         // Fall curve shape: -1=logarithmic, 0=linear, +1=exponential
        
        // Manual randomization trigger button
        RND_PARAM,                // Momentary button to trigger immediate randomization
        
        // Auto-randomization control parameters
        RND_TIME_PARAM,           // Time interval (free mode) or division/multiplication (sync mode)
        RND_AMT_PARAM,            // Maximum randomization amount as percentage of parameter range
        RND_AUTO_PARAM,           // Enable/disable auto-randomization
        RND_SYNC_PARAM,           // Mode: 0=free timing, 1=sync to external clock
        
        // Dual-mode global controls with mode switches
        GLOBAL_SLEW_PARAM,        // Dual function: slew-add time OR attenuverter gain
        GLOBAL_SLEW_MODE_PARAM,   // Mode switch: 0=Slew add (time), 1=Attenuverter (gain)
        GLOBAL_OFFSET_PARAM,      // Dual function: global offset OR range offset
        GLOBAL_OFFSET_MODE_PARAM, // Mode switch: 0=Global offset, 1=Range offset
        PARAMS_LEN
    };
    // Input enumeration - defines all input jacks
    enum InputId { 
        IN_INPUT,        // Main polyphonic input signal
        RND_TRIG_INPUT,  // External trigger/clock for randomization
        INPUTS_LEN       // Total count of inputs
    };
    
    // Output enumeration - defines all output jacks
    enum OutputId { 
        OUT_OUTPUT,      // Main polyphonic output signal (processed)
        OUTPUTS_LEN      // Total count of outputs
    };

    // Light enumeration - defines all LED indicators
    // Each channel has 2 LEDs: green for positive voltage, red for negative voltage
    enum LightId { 
        ENUMS(CH_LIGHT, 32),  // 32 lights total (16 channels × 2 colors each)
        LIGHTS_LEN            // Total count of lights
    };

    // ═══════════════════════════════════════════════════════════════════════════
    // DSP STATE VARIABLES - Runtime processing state for audio computation
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Per-channel slew limiting units for smooth voltage transitions
    dsp::SlewLimiter slews[16];
    
    // Per-channel step tracking for shape-aware slew processing
    float stepNorm[16] = {10.f};  // Current step magnitude in volts (defaults to 10V)
    int   stepSign[16] = {0};     // Direction of current voltage change (+1=rising, -1=falling, 0=stable)
    
    // Trigger detection for randomization controls
    dsp::BooleanTrigger rndBtnTrig;  // Detects front-panel randomize button presses
    dsp::SchmittTrigger rndGateTrig; // Detects external randomization trigger/clock signals

    // Performance optimization: cache slew rates to avoid redundant calculations
    float prevRiseRate[16] = {0};  // Previously calculated rise slew rates
    float prevFallRate[16] = {0};  // Previously calculated fall slew rates
    float lastOut[16]      = {0};  // Last output voltage per channel for continuity

    // ═══════════════════════════════════════════════════════════════════════════
    // MODULE CONFIGURATION OPTIONS - User-configurable behavior settings
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Polyphony control settings
    int forcedChannels = 0;        // Channel count: 0=Auto (match input), 1-16=force specific count
    bool sumToMonoOut = false;     // Output mode: false=polyphonic, true=sum all channels to mono
    bool avgWhenSumming = false;   // Summing behavior: false=add voltages, true=average voltages
    
    // DSP processing options
    bool pitchSafeGlide = false;   // Slew scaling: false=voltage-based, true=semitone-based (1V/oct)
    bool softClipOut = false;      // Clipping type: false=hard clamp, true=soft saturation curve
    // Output voltage range selection (affects both range mode and safety clipping)
    // Index maps to: 0=±10V, 1=±7.5V, 2=±5V, 3=±2.5V, 4=±1V, 5=±0.5V
    int clipVppIndex = 0;
    
    // Range enforcement mode for pre-quantization signal conditioning
    int rangeMode = 0;             // 0=Clip (hard limit), 1=Scale (compress to fit)
    
    // Per-channel offset parameter quantization modes
    int snapOffsetModeCh[16] = {0}; // Per-channel: 0=Voltage, 1=Semitones, 2=Cents
    int snapOffsetMode = 0;         // Global batch setting applied to all channels

    // ═══════════════════════════════════════════════════════════════════════════
    // DUAL-MODE GLOBAL CONTROLS - Advanced knob behavior with mode switching
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Dual-function knob state storage - each knob remembers values for both modes
    hi::ui::dual::DualBank<float> gSlew;    // Mode A: slew-add time, Mode B: attenuverter gain
    hi::ui::dual::DualBank<float> gOffset;  // Mode A: global offset voltage, Mode B: range center offset
    
    // Cross-mode application flags - allow simultaneous use of both knob functions
    bool attenuverterAlwaysOn = true;     // Apply gain scaling even in slew-add mode
    bool slewAddAlwaysOn = true;          // Apply slew time addition even in attenuverter mode
    bool globalOffsetAlwaysOn = true;     // Apply global offset even in range offset mode
    bool rangeOffsetAlwaysOn = true;      // Apply range centering even in global offset mode

    // ═══════════════════════════════════════════════════════════════════════════
    // STRUM TIMING SYSTEM - Sequential channel triggering for musical effects
    // ═══════════════════════════════════════════════════════════════════════════
    
    bool strumEnabled = false;     // Master enable for strum timing (default: disabled)
    int strumMode = 0;             // Pattern: 0=Up (1→16), 1=Down (16→1), 2=Random order
    //   0 = Time-stretch (adds per-channel delay to effective glide time)
    //   1 = Start-delay (holds start by per-channel delay, glide duration unchanged)
    int strumType = 1;             // Timing behavior: 0=Time-stretch, 1=Start-delay (default)
    float strumMs = 0.f;           // Inter-channel delay in milliseconds (0=simultaneous)
    
    // ═══════════════════════════════════════════════════════════════════════════
    // STRUM DELAY STATE - Runtime tracking for per-channel timing offsets
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Per-channel strum delay state (runtime only, not saved to patches)
    float strumDelayAssigned[16] = {0};  // Initial delay assigned to each channel (ms)
    float strumDelayLeft[16] = {0};      // Remaining delay countdown for each channel (ms)
    float strumPrevTarget[16] = {0.f};   // Last processed target per channel for strum change detection
    bool  strumPrevInit[16] = {false};   // Tracks whether strumPrevTarget has been primed
    static constexpr float STRUM_TARGET_TOL = 1e-4f; // Strum target change hysteresis (volts)

    // ═══════════════════════════════════════════════════════════════════════════
    // MODE TRACKING AND MIGRATION FLAGS - State change detection and compatibility
    // ═══════════════════════════════════════════════════════════════════════════
    
    bool prevPitchSafeGlide = false;     // Track pitch-safe glide mode changes for step recalc
    bool migratedQZ = false;             // One-time migration flag from old button params

    // ═══════════════════════════════════════════════════════════════════════════
    // QUANTIZER CONFIGURATION - Musical quantization behavior and signal chain order
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Quantizer position in the signal chain determines processing order
    enum QuantizerPos { Pre = 0, Post = 1 };
    int quantizerPos = QuantizerPos::Post;   // Default: Slew→Quantize (pitch-accurate)
    // Signal-chain options:
    // - Pre (legacy): Quantize→Slew - allows pitch bending but may detune during long glides
    // - Post (default): Slew→Quantize - maintains pitch accuracy throughout glide transitions

    // Quantization strength: blend between raw input and quantized output
    float quantStrength = 1.f;              // Range: 0.0 (bypass) to 1.0 (full quantization)
    
    // Quantization rounding behavior when input falls between scale degrees
    int quantRoundMode = 0;                  // Default: Directional Snap for musical expression
    // Rounding modes:
    // 0 = Directional Snap: ceil when rising, floor when falling (follows musical gesture)
    // 1 = Nearest: standard closest-note rounding (traditional quantizer behavior)
    // 2 = Up: always ceil to next higher note (chromatic runs upward)
    // 3 = Down: always floor to next lower note (chromatic runs downward)
    
    // Hysteresis (stickiness) prevents rapid switching between adjacent notes during slow slides
    float stickinessCents = 5.f;            // User range: 0-20 cents (auto-clamped to 40% of step size)

    // ═══════════════════════════════════════════════════════════════════════════
    // TUNING SYSTEM CONFIGURATION - EDO/TET settings and scale definitions
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Tuning system type selector
    int tuningMode = 0;                      // 0 = EDO (octave-based), 1 = TET (non-octave)
    
    // EDO (Equal Division of Octave) - traditional Western and microtonal systems
    int edo = 12;                            // Default: 12-EDO (standard Western tuning)
                                            // Menu offers curated presets from 5-EDO to 120-EDO
    
    // TET (Equal Temperament) - non-octave repeating systems like Carlos temperaments
    int   tetSteps = 9;                      // Default: Carlos Alpha (9 divisions of perfect fifth)
    float tetPeriodOct = std::log2(3.f/2.f); // Period size in octaves (log2 of frequency ratio)
                                            // Perfect fifth ≈ 0.5849625 octaves
    
    // ═══════════════════════════════════════════════════════════════════════════
    // SCALE SYSTEM - Custom and predefined musical scales
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Scale source selection (always use custom scales with unified selection)
    bool useCustomScale = true;              // Always true - unified scale selection system
    
    // Scale mask preservation is now always enabled by default
    


    // Custom scale storage - unified vector-based system for all EDOs
    std::vector<uint8_t> customMaskGeneric; // Dynamic array: 0/1 flag per scale degree

    // ═══════════════════════════════════════════════════════════════════════════
    // MOS DETECTION CACHE - Moment of Symmetry analysis for UI display optimization
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Cache structure to avoid expensive MOS detection recalculation in UI menus
    struct MOSCache {
        bool     valid       = false;        // Cache validity flag
        bool     found       = false;        // Whether a MOS pattern was detected
        int      N           = 0;            // Number of divisions (edo or tetSteps)
        int      m           = 0;            // MOS size (number of notes in the scale)
        int      g           = 0;            // MOS generator (step size that creates the pattern)
        int      tuningMode  = 0;            // Tuning system: 0=EDO, 1=TET
        int      edo         = 0;            // EDO value when cached
        int      tetSteps    = 0;            // TET steps when cached
        int      rootNote    = 0;            // Root note when cached
        bool     useCustom   = false;        // Custom scale usage when cached
        uint64_t maskHash    = 0;            // Hash fingerprint of scale mask configuration
    } mosCache;

    // Invalidate MOS cache when scale configuration changes
    void invalidateMOSCache() { mosCache.valid = false; }
    
    // Generate stable hash fingerprint for current scale mask configuration
    // Used to detect when cache needs refreshing due to scale changes
    uint64_t hashMask(int N) const {
        uint64_t h = 1469598103934665603ull;  // FNV-1a hash basis constant
        auto fnv1a = [&h](uint64_t v){ h ^= v; h *= 1099511628211ull; };  // FNV-1a hash function
        fnv1a((uint64_t)N);                   // Include division count
        fnv1a((uint64_t)useCustomScale);      // Include custom scale flag
        fnv1a((uint64_t)rootNote);            // Include current root note
        // Hash the appropriate mask based on EDO size
        if (!useCustomScale) {
            fnv1a(0xFFFFFFFFull);           // Standard marker for non-custom scales
            return h;
        }
        // Hash custom mask for all EDO sizes using unified vector system
        size_t len = customMaskGeneric.size();
        for(size_t i=0;i<std::min<size_t>(len,(size_t)N);++i) 
            fnv1a((uint64_t)(customMaskGeneric[i]&1));  // Hash each bit
        fnv1a((uint64_t)len);               // Include mask length
        return h;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // RANDOMIZATION SYSTEM - Scope control and automatic timing
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Randomization scope flags - determine which parameter types are affected
    bool randSlew = true;                    // Include slew time parameters in randomization
    bool randOffset = true;                  // Include offset voltage parameters in randomization
    bool randShapes = true;                  // Include rise/fall shape parameters in randomization
    
    // Randomization magnitude control
    float randMaxPct = 1.f;                  // Maximum delta as fraction of full range (0.1-1.0)
    
    // Auto-randomization timing system
    bool rndAutoEnabled = false;             // Master enable for automatic randomization
    bool rndSyncMode = false;                // Timing mode: false=free-running, true=sync to clock
    // Clock measurement and timing state for sync mode
    dsp::SchmittTrigger rndClockTrig;        // Trigger detector for external clock input
    double rndTimerSec = 0.0;                // Free-running timer accumulator (seconds, double precision for long runs)
    double rndClockPeriodSec = -1.0;         // Measured clock period with smoothing (seconds, double precision for stability)
    double rndClockLastEdge = -1.0;          // Timestamp of last rising edge (absolute seconds, stored with extra precision)
    bool  rndClockReady = false;             // Flag: at least two edges measured for period calc
    double rndAbsTimeSec = 0.0;              // Running absolute time counter for edge timing (double precision to prevent drift)
    
    // Per-mode knob position memory - preserves settings when switching timing modes
    float rndTimeRawFree = 0.5f;             // Free-running mode: raw knob value (0.0-1.0)
    float rndTimeRawSync = 0.5f;             // Sync mode: raw knob value (0.0-1.0)
    float rndTimeRawLoaded = 0.5f;           // Legacy: single stored value for compatibility
    bool  prevRndSyncMode = false;           // Previous sync mode state for change detection
    double rndNextFireTime = -1.0;           // Scheduled randomization time (sync mode, absolute, kept in double precision)
    
    // Clock division/multiplication state for sync mode rhythmic patterns
    int   rndDivCounter = 0;                 // Edge counter for division ratios (÷2, ÷4, etc.)
    int   rndCurrentDivide = 1;              // Active division factor (1=no division)
    int   rndCurrentMultiply = 1;            // Active multiplication factor (1=no multiplication)
    int   rndMulIndex = 0;                   // Current subdivision index within multiply window
    double rndMulBaseTime = -1.0;            // Anchor timestamp for current multiplication cycle (double precision anchor)
    double rndMulNextTime = -1.0;            // Next subdivision event time (absolute seconds, high precision for scheduling)
    int   rndPrevRatioIdx = -1;              // Previous ratio index for phase reset detection

    // ═══════════════════════════════════════════════════════════════════════════
    // RANDOMIZATION LOCKS - Per-control exclusion from randomization
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Per-channel parameter locks (prevent randomization when scope is enabled)
    bool lockSlew[16] = {false};             // Lock individual slew time controls
    bool lockOffset[16] = {false};           // Lock individual offset voltage controls
    
    // Global parameter locks
    bool lockRiseShape = false;              // Lock rise shape curve parameter
    bool lockFallShape = false;              // Lock fall shape curve parameter
    
    // Per-control randomize allows (opt-in when scope is disabled)
    bool allowSlew[16] = {false};            // Allow individual slew controls when scope OFF
    bool allowOffset[16] = {false};          // Allow individual offset controls when scope OFF
    bool allowRiseShape = false;             // Allow rise shape when scope OFF
    bool allowFallShape = false;             // Allow fall shape when scope OFF

    // ═══════════════════════════════════════════════════════════════════════════
    // PARAMETER INDEX MAPPING - Static arrays for channel-to-enum translation
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Map channel index (0-15) to interleaved parameter enum IDs
	static const int SL_PARAM[16];           // Slew time parameter indices
	static const int OFF_PARAM[16];          // Offset voltage parameter indices
    static const int QZ_PARAM[16];           // Legacy quantize button parameter indices

    // ═══════════════════════════════════════════════════════════════════════════
    // QUANTIZATION STATE - Per-channel quantizer behavior and history
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Per-channel quantization enable flags (replaces legacy front-panel buttons)
    bool qzEnabled[16] = {false};            // Enable quantization for each channel
    
    // Quantizer state tracking for smooth operation
    float prevYRel[16] = {0.f};              // Previous relative voltage for directional snap
    
    // Latched quantizer state for hysteresis and directional snap
    double lastFs[16] = {0.0};               // Last fractional step position (high precision)
    int    lastDir[16] = {0};                // Last movement direction: -1=down, 0=hold, +1=up
    int  latchedStep[16];                    // Current latched step index (0..N-1)
    bool latchedInit[16];                    // Initialization flag for each channel

    // ═══════════════════════════════════════════════════════════════════════════
    // PER-CHANNEL PRE-RANGE TRANSFORMS - Individual channel scaling and offset
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Applied BEFORE global Range Clip/Scale processing
    // Formula: output = (input * preScale[ch]) + preOffset[ch]
    // Allows per-voice window adjustment without requiring additional panel controls
    float preScale[16]  = {0};               // Scaling factor: -10.0 to +10.0 (default: 1.0)
    float preOffset[16] = {0};               // Offset voltage: -10.0V to +10.0V (default: 0.0V)

    // ═══════════════════════════════════════════════════════════════════════════
    // QUANTIZER CONFIGURATION CHANGE TRACKING - Invalidation detection
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Previous quantizer configuration state for change detection
    // When any of these values change, all channel latches are reset
    int prevRootNote = -999;                 // Previous root note setting
    int prevScaleIndex = -999;               // Previous scale selection index
    int prevEdo = -999;                      // Previous EDO division count
    int prevTetSteps = -999;                 // Previous TET step count
    float prevTetPeriodOct = -999.f;         // Previous TET period size
    int prevTuningMode = -999;               // Previous tuning system mode
    bool prevUseCustomScale = false;         // Previous custom scale usage flag

    // ═══════════════════════════════════════════════════════════════════════════
    // PER-CHANNEL PROCESSING CONTROLS - Individual channel behavior modifiers
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Post-quantization octave shifting (applied after scale quantization)
    int postOctShift[16] = {0};              // Octave shift per channel: -5 to +5 octaves (0 default)

    // Per-channel slew processing enable/disable flags (all channels enabled by default)
    bool slewEnabled[16] = {true, true, true, true, true, true, true, true,
                            true, true, true, true, true, true, true, true};

    // ═══════════════════════════════════════════════════════════════════════════
    // GLOBAL QUANTIZATION SETTINGS - Root note and scale selection
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Musical root note for quantization (scale degree 0)
    int rootNote = 0;                        // Index: 0..(edo-1). For 12-EDO: 0=C, 1=C#, ..., 11=B
    
    // Preset scale selection index for current EDO
    int scaleIndex = 0;                      // Index into scales table (ignored when useCustomScale=true)

    // Note: Preset scale data tables are defined in hi::music::scales12()/scales24()

    // ═══════════════════════════════════════════════════════════════════════════
    // POLYPHONY TRANSITION SYSTEM - Pop-free channel count changes
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Channel switching fade behavior:
    // 1. When desired channel count changes: fade OUT current channels
    // 2. Switch to new channel count while silent
    // 3. Fade IN new channels to prevent audio pops
    // Fade time is user-configurable via context menu (default: 100ms)
    
    // Centralized polyphony transition state manager
    hi::dsp::polytrans::State polyTrans;    // Handles fade phases and channel count management
    
    // Configurable fade duration for smooth channel transitions
    float polyFadeSec = 0.1f;               // Fade time in seconds (0.1s = 100ms default)

    // ═══════════════════════════════════════════════════════════════════════════
    // UTILITY METHODS - Musical quantization and voltage range mapping
    // ═══════════════════════════════════════════════════════════════════════════

    /*
    -------------------------------------------------------------------
        quantizeToScale(): Musical voltage quantizer utility
        - Snaps input voltage (1V/oct standard) to active EDO/TET scale
        - Supports both preset scales and custom scale masks
        - Optional voltage bounds limiting for range control
        
        currentClipLimit(): Range voltage mapper
        - Maps clipVppIndex selection to ±limit voltage values
        - Used by range processing for consistent voltage limiting
    -------------------------------------------------------------------
    */
    
    // Musical quantizer: snap voltage to current scale configuration
    // Parameters:
    //   v: Input voltage in 1V/oct format
    //   shiftSteps: Additional step offset (for octave shifts, etc.)
    //   boundLimit: Maximum voltage limit (±volts)
    //   boundToLimit: Whether to enforce voltage bounds on output
    float quantizeToScale(float v, int shiftSteps = 0, float boundLimit = 10.f, bool boundToLimit = false) const {
        // Configure quantizer with current module settings
        hi::dsp::QuantConfig qc;
        
        // Set up tuning system parameters
        if (tuningMode == 0) {
            // EDO (Equal Division of Octave) mode
            qc.edo = (edo <= 0) ? 12 : edo;      // Use 12-EDO as fallback for invalid values
            qc.periodOct = 1.f;                  // Octave period (1.0 = 2:1 frequency ratio)
        } else {
            // TET (Equal Temperament) mode - non-octave repeating systems
            qc.edo = tetSteps > 0 ? tetSteps : 9;                                    // Use 9 steps as fallback
            qc.periodOct = (tetPeriodOct > 0.f) ? tetPeriodOct : std::log2(3.f/2.f); // Use perfect fifth as fallback
        }
        
        // Configure scale and root settings
        qc.root = rootNote;                      // Current root note (scale degree 0)
        qc.useCustom = useCustomScale;           // Use custom mask vs. preset scales
        qc.customFollowsRoot = true;             // Custom scales always follow root transposition
        qc.scaleIndex = scaleIndex;              // Preset scale selection index
        // Handle custom masks for all EDO sizes using unified vector system
        if (qc.useCustom) {
            // Validate mask size matches current EDO
            if ((int)customMaskGeneric.size() == qc.edo) {
                qc.customMaskGeneric = customMaskGeneric.data();    // Provide mask data pointer
                qc.customMaskLen = (int)customMaskGeneric.size();   // Set mask length
            } else {
                // Invalid or missing mask - disable custom scale
                qc.customMaskGeneric = nullptr;                     // No mask data
                qc.customMaskLen = 0;                               // Zero length
            }
        }
        
        // Perform quantization using configured parameters
        return hi::dsp::snapEDO(v, qc, boundLimit, boundToLimit, shiftSteps);
    }

    // Range voltage mapper: convert clipVppIndex to actual voltage limit
    // Returns half-range value (±limit) for symmetric voltage clipping/scaling
    float currentClipLimit() const { 
        return hi::dsp::range::clipLimitFromIndex(clipVppIndex); 
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // MODULE CONSTRUCTOR - Parameter configuration and initialization
    // ═══════════════════════════════════════════════════════════════════════════
    //
    // This constructor performs comprehensive module initialization, setting up all VCV Rack 
    // parameters, custom parameter quantities, I/O ports, and internal state management.
    // It establishes the foundation for the module's sophisticated dual-mode controls,
    // polyphonic processing capabilities, and advanced quantization features.
    //
    // ┌─ PER-CHANNEL PARAMETER SETUP ───────────────────────────────────────────────────────────────────┐
    // │ • 16 offset parameters: ±10V range with custom OffsetQuantity for semitone/cent display         │
    // │ • 16 slew parameters: exponential time scaling (0ms-10s) with ExpTimeQuantity                   │
    // │ • Per-channel quantizer enable states managed via context menu (not front-panel)                │
    // │ • Quantizer latching state initialization for all channels                                      │
    // │ • Per-channel pre-processing transform defaults (scale=1.0x, offset=0V)                         │
    // └─────────────────────────────────────────────────────────────────────────────────────────────────┘
    //
    // ┌─ GLOBAL CURVE SHAPING CONTROLS ─────────────────────────────────────────────────────────────────┐
    // │ • Rise shape parameter: logarithmic (-1) ↔ linear (0) ↔ exponential (+1) curves                 │
    // │ • Fall shape parameter: independent curve control for falling transitions                       │
    // │ • Custom ShapeQuantity provides intuitive curve type display and input parsing                  │
    // └─────────────────────────────────────────────────────────────────────────────────────────────────┘
    //
    // ┌─ ADVANCED DUAL-MODE GLOBAL CONTROLS ────────────────────────────────────────────────────────────┐
    // │ Global Slew Control (Mode A ↔ Mode B):                                                          │
    // │ • Mode A: Slew-add seconds (exponential 0ms-10s) - adds to per-channel slew times               │
    // │ • Mode B: Attenuverter gain (-10x to +10x) - multiplies input signals before processing         │
    // │ • Custom GlobalSlewDualQuantity: mode-aware display, input parsing, and default values          │
    // │ • Bank switching with value persistence across mode changes                                     │
    // │                                                                                                 │
    // │ Global Offset Control (Mode A ↔ Mode B):                                                        │
    // │ • Mode A: Global offset ±10V - combined with per-channel offsets                                │
    // │ • Mode B: Range offset ±5V - applied after range processing, before quantizer                   │
    // │ • Custom GlobalOffsetDualQuantity: semitone/volt display with mode-specific formatting          │
    // │ • Integrated with offset quantization system for musical snap-to-grid functionality             │
    // └─────────────────────────────────────────────────────────────────────────────────────────────────┘
    //
    // ┌─ INPUT/OUTPUT PORT CONFIGURATION ───────────────────────────────────────────────────────────────┐
    // │ • Polyphonic input: main signal processing input (1-16 channels)                                │
    // │ • Randomization trigger: external gate/trigger input for randomization events                   │
    // │ • Polyphonic output: processed signal with slew, quantization, and offset applied               │
    // │ • VCV Rack bypass routing: direct input-to-output connection when module is bypassed            │
    // └─────────────────────────────────────────────────────────────────────────────────────────────────┘
    //
    // ┌─ SOPHISTICATED RANDOMIZATION SYSTEM ────────────────────────────────────────────────────────────┐
    // │ Manual Controls:                                                                                │
    // │ • Randomize button: momentary trigger for immediate randomization                               │
    // │ • Randomization amount: 0-100% strength control with PercentQuantity display                    │
    // │                                                                                                 │
    // │ Auto-Randomization Engine:                                                                      │
    // │ • Auto enable/disable toggle for automatic randomization                                        │
    // │ • Sync mode toggle: free-running timer vs. external clock synchronization                       │
    // │ • Advanced timing control with custom RandomTimeQuantity:                                       │
    // │   - Free mode: logarithmic time scale (1ms to 10000s) with ms/s unit parsing                    │
    // │   - Sync mode: clock division (÷64..÷2), unity (1×), multiplication (×2..×64)                   │
    // │   - Intelligent input parsing: handles ÷, ×, UTF-8 symbols, and various text formats            │
    // │   - 127-position ratio mapping with precise index-to-factor conversion                          │
    // └─────────────────────────────────────────────────────────────────────────────────────────────────┘
    //
    // ┌─ RUNTIME STATE INITIALIZATION ──────────────────────────────────────────────────────────────────┐
    // │ • Dual-mode bank seeding: sensible defaults for both modes of global controls                   │
    // │ • Per-channel step tracking: direction, magnitude, and rate change detection                    │
    // │ • Quantizer state management: latching initialization and configuration change detection        │
    // │ • Processing optimization: rate change caching to prevent zipper noise                          │
    // └─────────────────────────────────────────────────────────────────────────────────────────────────┘
    //
    // ┌─ CUSTOM PARAMETER QUANTITY ARCHITECTURE ────────────────────────────────────────────────────────┐
    // │ • OffsetQuantity: semitone/cent/volt display with EDO-aware formatting                          │
    // │ • ExpTimeQuantity: exponential time scaling with ms/s unit conversion                           │
    // │ • ShapeQuantity: curve type display (log/linear/exp) with intuitive controls                    │
    // │ • GlobalSlewDualQuantity: context-sensitive display and parsing for dual-mode operation         │
    // │ • GlobalOffsetDualQuantity: mode-aware voltage/semitone formatting and input handling           │
    // │ • RandomTimeQuantity: complex sync ratio and time duration parsing with UTF-8 support           │
    // │ • PercentQuantity: simple percentage display with optional % symbol parsing                     │
    // │ • Mode toggle quantities: clear mode indication for dual-function controls                      │
    // └─────────────────────────────────────────────────────────────────────────────────────────────────┘
    //
    // The constructor establishes a sophisticated parameter ecosystem that supports both novice and
    // expert users through intuitive displays, flexible input parsing, and context-aware behavior.
    // All parameters are designed for musical workflows with proper unit conversions, range limiting,
    // and error handling to ensure stable operation across all use cases.
    
    // Constructor - Initialize module with all parameters, inputs, outputs, and lights
    PolyQuanta() {
        // Configure VCV Rack module with total counts of each component type
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Configure per-channel parameter controls (16 channels total)
        for (int i = 0; i < 16; ++i) {
            // Offset parameter: voltage offset with optional semitone/cent quantization
            // Range: -10V to +10V, default: 0V (no offset)
            auto* pq = configParam<OffsetQuantity>(OFF_PARAM[i], -10.f, 10.f, 0.f, string::f("Ch %d offset", i+1), "");
            pq->snapOffsetModePtr = &snapOffsetModeCh[i];  // Link to per-channel snap mode
            pq->edoPtr = &edo;                             // Link to current EDO for semitone display
            
            // Slew parameter: exponential time scaling for smooth voltage transitions
            // Range: 0.0 to 1.0 (mapped exponentially to ~0ms to ~10s), default: 0.0 (instant)
            configParam<hi::ui::ExpTimeQuantity>(SL_PARAM[i], 0.f, 1.f, 0.0f, string::f("Ch %d slew (rise & fall)", i+1), "");
            
            // Note: Per-channel quantization enable is now controlled via context menu (qzEnabled[])
            // rather than front-panel buttons for cleaner UI
        }
        // Initialize quantizer state for all channels
        for (int i = 0; i < 16; ++i) { 
            latchedInit[i] = false;  // No quantizer step latched yet
            latchedStep[i] = 0;      // Default to step 0 when latching begins
        }
        
        // Configure global slew curve shape parameters
        // Range: -1.0 (logarithmic) to +1.0 (exponential), default: 0.0 (linear)
        configParam<hi::ui::ShapeQuantity>(RISE_SHAPE_PARAM, -1.f, 1.f, 0.f, "Rise shape");
        configParam<hi::ui::ShapeQuantity>(FALL_SHAPE_PARAM, -1.f, 1.f, 0.f, "Fall shape");

        // Initialize per-channel pre-processing transform defaults
        // These transforms are applied before global range processing
        for (int i = 0; i < 16; ++i) {
            preScale[i] = 1.f;   // No scaling by default (1.0x multiplier)
            preOffset[i] = 0.f;  // No pre-offset by default (0V addition)
        }

        // ═══════════════════════════════════════════════════════════════════════════
        // DUAL-MODE GLOBAL CONTROL CONFIGURATION - Advanced parameter quantities
        // ═══════════════════════════════════════════════════════════════════════════
        
        // ═══════════════════════════════════════════════════════════════════════════
        // DUAL-MODE PARAMETER QUANTITIES - Custom UI behavior for mode-switching controls
        // ═══════════════════════════════════════════════════════════════════════════
        
        // Custom parameter quantity for the dual-function global slew control
        // Provides mode-aware display formatting and input parsing
        struct GlobalSlewDualQuantity : hi::ui::ExpTimeQuantity {
            
            // Dynamic display string based on current knob mode
            std::string getDisplayValueString() override {
                auto* m = dynamic_cast<PolyQuanta*>(module);  // Get module reference for mode checking
                float raw = getValue();                       // Current raw knob position [0.0-1.0]
                
                if (m && m->gSlew.mode) {
                    // Mode B: Attenuverter mode - display as gain multiplier
                    // Transform raw knob value [0..1] to bipolar gain range [-10x..+10x]
                    float g = -10.f + 20.f * rack::math::clamp(raw, 0.f, 1.f);
                    return rack::string::f("Attenuverter: %+.2fx", g);
                }
                
                // Mode A: Slew-add mode - display as additional time duration
                // Use exponential time mapping from base class for musical timing
                float sec = hi::ui::ExpTimeQuantity::knobToSec(raw);
                if (sec < 1.f) return rack::string::f("Slew add: %.0f ms", sec * 1000.f);
                return rack::string::f("Slew add: %.2f s", sec);
            }
            
            void setValue(float v) override {
                // Preserve standard parameter behavior - no special handling needed
                hi::ui::ExpTimeQuantity::setValue(v);
            }
            
            // Parse user text input with mode-aware format recognition
            void setDisplayValueString(std::string s) override {
                auto* m = dynamic_cast<PolyQuanta*>(module);
                
                if (m && m->gSlew.mode) {
                    // Mode B: Attenuverter mode - parse input as gain multiplier
                    // Accept formats like "2", "2x", "+2.5", "-1.5x", etc.
                    const char* c = s.c_str();
                    char* end = nullptr;
                    float g = std::strtof(c, &end);  // Parse floating-point number
                    
                    // If parsing failed, fall back to default behavior
                    if (end == c) { 
                        ParamQuantity::setDisplayValueString(s); 
                        return; 
                    }
                    
                    // Convert gain [-10..+10] back to raw knob value [0..1]
                    float raw = rack::math::clamp((g + 10.f) / 20.f, 0.f, 1.f);
                    setValue(raw);
                    return;
                }
                
                // Mode A: Slew-add mode - use base class time parsing (ms/s formats)
                ParamQuantity::setDisplayValueString(s);
            }
            
            float getDefaultValue() override {
                // Context-sensitive default value based on current mode
                auto* m = dynamic_cast<PolyQuanta*>(module);
                
                if (m && m->gSlew.mode) {
                    // Attenuverter mode: default to unity gain (1.00x)
                    // Unity gain = raw value 0.55 (maps to 1.0x in [-10..+10] range)
                    return 0.55f;
                }
                
                // Slew-add mode: default to no additional slew (0 seconds)
                return 0.f;
            }
        };
        
        // Configure the dual-mode global slew parameter with custom quantity
        configParam<GlobalSlewDualQuantity>(GLOBAL_SLEW_PARAM, 0.f, 1.f, 0.0f, "Global Slew (dual)", "");
        
        // Simple toggle quantity for slew mode switch display
        struct SlewModeQuantity : ParamQuantity {
            std::string getDisplayValueString() override { 
                return (getValue() > 0.5f) ? "Attenuverter" : "Slew add"; 
            }
        };
        configParam<SlewModeQuantity>(GLOBAL_SLEW_MODE_PARAM, 0.f, 1.f, 0.f, "Global Slew knob mode");
        
        // Custom parameter quantity for the dual-function global offset control
        // Displays different units and ranges based on current mode
        struct GlobalOffsetDualQuantity : hi::ui::SemitoneVoltQuantity {
            
            std::string getUnit() override {
                // Units are embedded in the display value string to avoid duplication
                return "";
            }
            
            std::string getDisplayValueString() override {
                auto* m = dynamic_cast<PolyQuanta*>(module);
                
                if (m && m->gOffset.mode) {
                    // Mode B: Range offset mode - display as voltage with clear labeling
                    float v = getValue();
                    return rack::string::f("Range offset: %.2f V", v);
                }
                
                // Mode A: Global offset mode - use semitone/volt formatting with label
                return std::string("Global offset: ") + hi::ui::SemitoneVoltQuantity::getDisplayValueString();
            }
            
            void setDisplayValueString(std::string s) override {
                auto* m = dynamic_cast<PolyQuanta*>(module);
                
                if (m && m->gOffset.mode) {
                    // Mode B: Range offset mode - parse input as voltage value
                    // Accept formats like "-5", "2.5", "+1.0", etc.
                    const char* c = s.c_str();
                    char* end = nullptr;
                    float v = std::strtof(c, &end);  // Parse floating-point voltage
                    
                    // If parsing failed, fall back to base class behavior
                    if (end == c) {
                        hi::ui::SemitoneVoltQuantity::setDisplayValueString(s);
                    } else {
                        // Clamp to range offset limits (±5V) and apply
                        setValue(clamp(v, -5.f, 5.f));
                    }
                    return;
                }
                
                // Mode A: Global offset mode - use base class parsing (volts or semitones)
                hi::ui::SemitoneVoltQuantity::setDisplayValueString(s);
            }
        };
        
        // Configure the dual-mode global offset parameter with custom quantity
        {
            auto* gq = configParam<GlobalOffsetDualQuantity>(GLOBAL_OFFSET_PARAM, -10.f, 10.f, 0.0f, "Global Offset (dual)", "");
            gq->snapOffsetModePtr = &snapOffsetMode;  // Link to global snap mode setting
            gq->edoPtr = &edo;                        // Link to current EDO for semitone display
        }
        
        // Simple toggle quantity for offset mode switch display
        struct OffsetModeQuantity : ParamQuantity {
            std::string getDisplayValueString() override {
                return (getValue() > 0.5f) ? "Range offset" : "Global offset";
            }
        };
        configParam<OffsetModeQuantity>(GLOBAL_OFFSET_MODE_PARAM, 0.f, 1.f, 0.f, "Global Offset knob mode");

        // ═══════════════════════════════════════════════════════════════════════════
        // INPUT/OUTPUT PORT CONFIGURATION - Audio signal routing
        // ═══════════════════════════════════════════════════════════════════════════
        
        // Configure polyphonic input and output ports with descriptive hover text
        configInput(IN_INPUT, "Poly signal");                           // Main polyphonic audio input
        configInput(RND_TRIG_INPUT, "Randomize trigger (gate)");        // External randomization trigger
        configOutput(OUT_OUTPUT, "Poly signal (slewed + offset)");      // Processed polyphonic output

        // ═══════════════════════════════════════════════════════════════════════════
        // DUAL-MODE BANK INITIALIZATION - Default values for mode switching
        // ═══════════════════════════════════════════════════════════════════════════
        
        // Initialize global slew dual-mode banks with sensible defaults
        gSlew.a = params[GLOBAL_SLEW_PARAM].getValue();  // Mode A: current knob position (slew-add)
        gSlew.b = 0.55f;                                 // Mode B: unity gain (1.0x attenuverter)
        gSlew.mode = false;                              // Start in Mode A (slew-add)
        
        // Initialize global offset dual-mode banks with sensible defaults
        gOffset.a = params[GLOBAL_OFFSET_PARAM].getValue(); // Mode A: current knob position (global offset)
        gOffset.b = 0.f;                                    // Mode B: centered range offset (0V)
        gOffset.mode = false;                               // Start in Mode A (global offset)

        // ═══════════════════════════════════════════════════════════════════════════
        // BYPASS AND RANDOMIZATION CONFIGURATION - Additional module controls
        // ═══════════════════════════════════════════════════════════════════════════
        
        // Configure VCV Rack bypass routing (input passes directly to output when bypassed)
        configBypass(IN_INPUT, OUT_OUTPUT);
        
        // Configure momentary randomization button (edge-detected in process() method)
        configParam(RND_PARAM, 0.f, 1.f, 0.f, "Randomize");

        // ═══════════════════════════════════════════════════════════════════════════
        // AUTO-RANDOMIZATION PARAMETER QUANTITIES - Complex timing and ratio controls
        // ═══════════════════════════════════════════════════════════════════════════
        
        // Custom parameter quantity for randomization timing control
        // Handles both free-running time and sync mode division/multiplication ratios
        struct RandomTimeQuantity : rack::engine::ParamQuantity {
            
            // Logarithmic time conversion utilities for free-running mode
            static float rawToSec(float r) { 
                const float mn = 0.001f, mx = 10000.f;  // 1ms to 10000s range
                float lmn = std::log10(mn), lmx = std::log10(mx);
                float lx = lmn + rack::clamp(r, 0.f, 1.f) * (lmx - lmn);
                return std::pow(10.f, lx); 
            }
            static float secToRaw(float s) { 
                const float mn = 0.001f, mx = 10000.f;
                s = rack::clamp(s, mn, mx);
                float lmn = std::log10(mn), lmx = std::log10(mx);
                return (std::log10(s) - lmn) / (lmx - lmn); 
            }
            
            // Mode-aware display formatting
            std::string getDisplayValueString() override {
                auto* m = dynamic_cast<PolyQuanta*>(module);
                bool syncMode = m ? m->rndSyncMode : false;
                float r = getValue();
                
                if (syncMode) {
                    // Sync mode: Display clock division/multiplication ratios
                    // Layout: 64÷...2÷ | 1× | ×2...×64 (127 total positions)
                    const int DIV_MAX = 64;                                    // Maximum division/multiplication factor
                    const int TOTAL = (DIV_MAX-1) + 1 + (DIV_MAX-1);          // Total positions: 63 + 1 + 63 = 127
                    int idx = (int)std::lround(rack::clamp(r,0.f,1.f)*(TOTAL-1)); // Map [0..1] to [0..126]
                    
                    if(idx < (DIV_MAX-1)) { 
                        // Left side: division ratios (÷64 to ÷2)
                        int d = DIV_MAX - idx;                              // Convert index to division factor
                        return rack::string::f("÷%d", d); 
                    }
                    if(idx == (DIV_MAX-1)) return std::string("1×");        // Center: unity (no division/multiplication)
                    
                    // Right side: multiplication ratios (×2 to ×64)
                    int mfac = (idx - (DIV_MAX-1)) + 1;                     // Convert index to multiplication factor
                    return rack::string::f("×%d", mfac);
                }
                
                // Free-running mode: Display time duration with appropriate units
                float sec = rawToSec(r);
                if(sec < 10.f) return rack::string::f("%.2f ms", sec * 1000.f);
                return rack::string::f("%.2f s", sec);
            }
            
            // Parse user text input with mode-aware format recognition
            void setDisplayValueString(std::string s) override {
                auto* m = dynamic_cast<PolyQuanta*>(module);
                bool syncMode = m ? m->rndSyncMode : false;
                std::string t = s;
                
                // Convert input to lowercase for case-insensitive parsing
                for(char& c : t) c = std::tolower((unsigned char)c);
                
                if(syncMode) {
                    // Sync mode: Parse division/multiplication ratio formats
                    // Accept formats: "-N" (divide), "÷N", "xN", "×N", "N" (multiply), "1"
                    const int DIV_MAX = 64;
                    const int TOTAL = (DIV_MAX-1) + 1 + (DIV_MAX-1);
                    
                    // Utility function to trim whitespace from string
                    auto trim = [&](std::string& x) { 
                        while(!x.empty() && isspace((unsigned char)x.front())) x.erase(x.begin()); 
                        while(!x.empty() && isspace((unsigned char)x.back())) x.pop_back(); 
                    };
                    trim(t);
                    
                    // Normalize UTF-8 division/multiplication symbols to ASCII for consistent parsing
                    // Converts ÷ (U+00F7) and × (U+00D7) to ASCII equivalents
                    auto normalizeSymbols = [](std::string& u) {
                        std::string out; 
                        out.reserve(u.size());
                        for(size_t i = 0; i < u.size();) {
                            unsigned char c0 = (unsigned char)u[i];
                            // Check for UTF-8 two-byte sequences starting with 0xC3
                            if(c0 == 0xC3 && i + 1 < u.size()) { 
                                unsigned char c1 = (unsigned char)u[i + 1];
                                if(c1 == 0xB7) { out.push_back('/'); i += 2; continue; } // ÷ → '/'
                                if(c1 == 0x97) { out.push_back('x'); i += 2; continue; } // × → 'x'
                            }
                            // Copy regular ASCII characters unchanged
                            out.push_back(u[i]); 
                            ++i;
                        }
                        u.swap(out);                                        // Replace original string with normalized version
                    };
                    normalizeSymbols(t);
                    
                    // Handle special unity cases (1×, 1*, 1/1, etc.)
                    if(t == "1" || t == "1x" || t == "1*" || t == "1/1") { 
                        setValue((float)(DIV_MAX - 1) / (TOTAL - 1));      // Set to center position (unity)
                        return; 
                    }
                    
                    // Parse sign prefix and extract numeric digits
                    int sign = 1;                                           // Default to multiplication
                    size_t pos = 0;
                    
                    // Check for explicit sign prefix
                    if(!t.empty() && (t[0] == '-' || t[0] == '+')) {
                        if(t[0] == '-') sign = -1;                          // Negative sign indicates division
                        pos = 1;
                    }
                    
                    // Check for explicit division markers
                    if(pos < t.size() && (t[pos] == '/' || t[pos] == 'd')) { 
                        sign = -1;                                          // Force division mode
                        ++pos; 
                    }
                    
                    // Skip explicit multiplication markers (already default)
                    if(pos < t.size() && (t[pos] == 'x' || t[pos] == '*')) { 
                        ++pos; 
                    }
                    
                    // Extract consecutive digits from remaining string
                    std::string digits; 
                    for(; pos < t.size(); ++pos) { 
                        if(isdigit((unsigned char)t[pos])) digits.push_back(t[pos]); 
                        else break; 
                    }
                    if(digits.empty()) { return; }                         // No valid number found
                    
                    // Convert digits to integer with error handling
                    int val = 0; 
                    try { val = std::stoi(digits); } 
                    catch(...) { return; }
                    
                    if(sign < 0) { 
                        // Division factor: map ÷2..÷64 to positions 0..62
                        if(val < 2) return;                                 // Division by 1 or less is invalid
                        if(val > DIV_MAX) val = DIV_MAX;                    // Clamp to maximum division
                        int idx = DIV_MAX - val;                            // Convert ÷64..÷2 to 0..62
                        setValue((float)idx / (TOTAL - 1));
                        return;
                    }
                    
                    // Handle unity multiplication explicitly
                    if(val == 1) {
                        setValue((float)(DIV_MAX - 1) / (TOTAL - 1));       // Set to center position
                        return;
                    }
                    
                    // Multiplication factor: map ×2..×64 to positions 63..126
                    if(val >= 2) {
                        if(val > DIV_MAX) val = DIV_MAX;                    // Clamp to maximum multiplication
                        int idx = (DIV_MAX - 1) + (val - 1);               // Convert ×2..×64 to 63..126
                        setValue((float)idx / (TOTAL - 1));
                        return;
                    }
                    return; 
                }
                
                // Free-running mode: Parse time duration with unit detection
                bool ms = false;                                            // Default to seconds
                if(t.find("ms") != std::string::npos) { 
                    ms = true;                                              // Milliseconds detected
                    t.erase(t.find("ms")); 
                } 
                if(t.find("s") != std::string::npos) { 
                    ms = false;                                             // Seconds detected (overrides ms)
                    t.erase(t.find("s")); 
                } 
                
                try { 
                    float v = std::stof(t);                                 // Parse numeric value
                    if(ms) v /= 1000.f;                                     // Convert milliseconds to seconds
                    setValue(secToRaw(v));                                  // Convert to logarithmic parameter value
                } catch(...) {}                                             // Ignore parse errors
            }
        };
        
        // ═══════════════════════════════════════════════════════════════════════════════════════════════════
        // PercentQuantity: Simple percentage display parameter quantity
        // ═══════════════════════════════════════════════════════════════════════════════════════════════════
        struct PercentQuantity : rack::engine::ParamQuantity {
            // Display parameter value as percentage with % symbol
            std::string getDisplayValueString() override { 
                return rack::string::f("%.0f%%", getValue() * 100.f); 
            }
            
            // Parse user input with optional % symbol, clamp to [0..1] range
            void setDisplayValueString(std::string s) override { 
                std::string t = s; 
                for(char& c : t) c = std::tolower((unsigned char)c);    // Case-insensitive parsing
                if(t.find('%') != std::string::npos) t.erase(t.find('%')); // Remove % symbol if present
                try { 
                    float v = std::stof(t) / 100.f;                     // Convert percentage to [0..1] range
                    setValue(rack::clamp(v, 0.f, 1.f));                // Clamp to valid range
                } catch(...) {}                                         // Ignore parse errors
            }
        };
        
        // ═══════════════════════════════════════════════════════════════════════════════════════════════════
        // Auto-Randomization Parameter Configuration
        // ═══════════════════════════════════════════════════════════════════════════════════════════════════
        configParam<RandomTimeQuantity>(RND_TIME_PARAM, 0.f, 1.f, 0.5f, "Time");     // Randomization timing (sync ratios or free time)
        configParam<PercentQuantity>(RND_AMT_PARAM, 0.f, 1.f, 1.f, "Amount");        // Randomization strength (0-100%)
        configParam(RND_AUTO_PARAM, 0.f, 1.f, 0.f, "Auto (On/Off)");                 // Enable/disable auto-randomization
        configParam(RND_SYNC_PARAM, 0.f, 1.f, 0.f, "Sync (Sync/Trig)");      // Sync mode toggle (clock sync vs trigger)
        
        // ═══════════════════════════════════════════════════════════════════════════════════════════════════
        // Per-Channel Step Tracking Initialization
        // ═══════════════════════════════════════════════════════════════════════════════════════════════════
        for (int i = 0; i < 16; ++i) {
            stepNorm[i] = 10.f;                                         // Initialize step normalization to safe default
            stepSign[i] = 0;                                            // Initialize step direction to neutral
            prevRiseRate[i] = -1.f;                                     // Mark rise rate as uninitialized
            prevFallRate[i] = -1.f;                                     // Mark fall rate as uninitialized
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════
    // JSON Persistence: dataToJson() - Save Module State
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════
    // Saves all module options, dual-mode states/banks, quantization settings, per-channel toggles 
    // and octave shifts, and randomize locks/allows. Keys are stable for forward/backward compatibility.
    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Core Module Configuration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        json_object_set_new(rootJ, "forcedChannels", json_integer(forcedChannels));    // Forced channel count (0 = Auto)
        hi::util::jsonh::writeBool(rootJ, "sumToMonoOut", sumToMonoOut);                // Sum polyphonic output to mono
        hi::util::jsonh::writeBool(rootJ, "avgWhenSumming", avgWhenSumming);            // Average instead of sum when combining
        hi::util::jsonh::writeBool(rootJ, "pitchSafeGlide", pitchSafeGlide);           // Pitch-safe glide mode
        hi::util::jsonh::writeBool(rootJ, "softClipOut", softClipOut);                 // Soft clipping on output
        json_object_set_new(rootJ, "clipVppIndex", json_integer(clipVppIndex));        // Voltage clipping range index
        json_object_set_new(rootJ, "rangeMode", json_integer(rangeMode));              // Range handling mode (clip/scale)
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Offset Snap Mode Configuration (Global and Per-Channel)
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        json_object_set_new(rootJ, "snapOffsetMode", json_integer(snapOffsetMode));    // Global snap offset mode enum
        
        // Save per-channel snap offset modes as JSON array
        {
            json_t* arr = json_array();
            for (int i = 0; i < 16; ++i) {
                json_array_append_new(arr, json_integer(snapOffsetModeCh[i]));         // Per-channel snap mode
            }
            json_object_set_new(rootJ, "snapOffsetModeCh", arr);
        }
        
        // Legacy compatibility: write boolean for old versions (true only if semitone mode)
        hi::util::jsonh::writeBool(rootJ, "snapOffsets", snapOffsetMode == 1);
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Dual-Mode Global Controls (Slew and Offset)
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Global slew control: slew-add mode vs attenuverter mode
        hi::util::jsonh::writeBool(rootJ, "globalSlewMode", gSlew.mode);               // Current mode (false=slew-add, true=attenuverter)
        json_object_set_new(rootJ, "globalSlewBank0", json_real(gSlew.a));             // Bank A value (slew-add mode)
        json_object_set_new(rootJ, "globalSlewBank1", json_real(gSlew.b));             // Bank B value (attenuverter mode)
        
        // Global offset control: global offset mode vs range offset mode
        hi::util::jsonh::writeBool(rootJ, "globalOffsetMode", gOffset.mode);           // Current mode (false=global, true=range)
        json_object_set_new(rootJ, "globalOffsetBank0", json_real(gOffset.a));        // Bank A value (global offset mode)
        json_object_set_new(rootJ, "globalOffsetBank1", json_real(gOffset.b));        // Bank B value (range offset mode)
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Always-On Flags for Dual-Mode Controls
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        hi::util::jsonh::writeBool(rootJ, "attenuverterAlwaysOn", attenuverterAlwaysOn);   // Force attenuverter mode always active
        hi::util::jsonh::writeBool(rootJ, "slewAddAlwaysOn", slewAddAlwaysOn);             // Force slew-add mode always active
        hi::util::jsonh::writeBool(rootJ, "globalOffsetAlwaysOn", globalOffsetAlwaysOn);   // Force global offset mode always active
        hi::util::jsonh::writeBool(rootJ, "rangeOffsetAlwaysOn", rangeOffsetAlwaysOn);     // Force range offset mode always active
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Strum Configuration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        hi::util::jsonh::writeBool(rootJ, "strumEnabled", strumEnabled);               // Enable strum timing patterns
        json_object_set_new(rootJ, "strumMode", json_integer(strumMode));              // Strum pattern mode (up/down/random)
        json_object_set_new(rootJ, "strumType", json_integer(strumType));             // Strum timing type (time-stretch vs start-delay)
        json_object_set_new(rootJ, "strumMs", json_real(strumMs));                    // Strum timing duration in milliseconds
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Randomization Scope Configuration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        hi::util::jsonh::writeBool(rootJ, "randSlew", randSlew);                      // Include slew rates in randomization
        hi::util::jsonh::writeBool(rootJ, "randOffset", randOffset);                  // Include offsets in randomization
        hi::util::jsonh::writeBool(rootJ, "randShapes", randShapes);                  // Include curve shapes in randomization
        json_object_set_new(rootJ, "randMaxPct", json_real(randMaxPct));              // Maximum randomization percentage
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Auto-Randomization Configuration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        hi::util::jsonh::writeBool(rootJ, "rndAutoEnabled", rndAutoEnabled);          // Enable automatic randomization
        hi::util::jsonh::writeBool(rootJ, "rndSyncMode", rndSyncMode);                // Sync mode (clock ratios vs free time)
        
        // Persist per-mode raw time knob values for dual-mode time parameter
        json_object_set_new(rootJ, "rndTimeRawFree", json_real(rndTimeRawFree));      // Free-running time mode value
        json_object_set_new(rootJ, "rndTimeRawSync", json_real(rndTimeRawSync));      // Clock sync mode value
        json_object_set_new(rootJ, "rndTimeRaw", json_real(params[RND_TIME_PARAM].getValue())); // Legacy compatibility
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Per-Channel Quantization and Octave Shift Settings
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        for (int i = 0; i < 16; ++i) {
            char key[32];
            // Save quantization enable state for each channel
            std::snprintf(key, sizeof(key), "qzEnabled%d", i + 1);
            hi::util::jsonh::writeBool(rootJ, key, qzEnabled[i]);
            
            // Save post-quantization octave shift for each channel
            std::snprintf(key, sizeof(key), "postOctShift%d", i + 1);
            json_object_set_new(rootJ, key, json_integer(postOctShift[i]));
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Quantization Core State Delegation
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Delegate quantization settings (scales, tuning, etc.) to core DSP module for persistence
        {
            hi::dsp::CoreState cs;                                      // Create core state structure
            fillCoreStateFromModule(*this, cs);                        // Copy module state to core structure
            hi::dsp::coreToJson(rootJ, cs);                            // Serialize core state to JSON
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Randomization Lock and Allow States
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Per-channel randomization control settings
        for (int i = 0; i < 16; ++i) {
            char key[32];
            // Lock states: prevent randomization of specific parameters
            std::snprintf(key, sizeof(key), "lockSlew%d", i + 1);
            hi::util::jsonh::writeBool(rootJ, key, lockSlew[i]);        // Lock slew rate from randomization
            std::snprintf(key, sizeof(key), "lockOffset%d", i + 1);
            hi::util::jsonh::writeBool(rootJ, key, lockOffset[i]);      // Lock offset from randomization
            
            // Allow states: enable randomization of specific parameters
            std::snprintf(key, sizeof(key), "allowSlew%d", i + 1);
            hi::util::jsonh::writeBool(rootJ, key, allowSlew[i]);       // Allow slew rate randomization
            std::snprintf(key, sizeof(key), "allowOffset%d", i + 1);
            hi::util::jsonh::writeBool(rootJ, key, allowOffset[i]);     // Allow offset randomization
        }
        
        // Global curve shape randomization control
        hi::util::jsonh::writeBool(rootJ, "lockRiseShape", lockRiseShape);     // Lock rise curve shape from randomization
        hi::util::jsonh::writeBool(rootJ, "lockFallShape", lockFallShape);     // Lock fall curve shape from randomization
        hi::util::jsonh::writeBool(rootJ, "allowRiseShape", allowRiseShape);   // Allow rise curve shape randomization
        hi::util::jsonh::writeBool(rootJ, "allowFallShape", allowFallShape);   // Allow fall curve shape randomization
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Per-Channel Slew Enable State (Compact Bitmask Storage)
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Store slew enable states as a compact 16-bit bitmask (bit=1 means disabled)
        {
            uint16_t slewDisabledMask = 0;
            for (int i = 0; i < 16; ++i) {
                if (!slewEnabled[i]) slewDisabledMask |= (1 << i);         // Set bit if slew is disabled
            }
            json_object_set_new(rootJ, "slewDisabledMask", json_integer(slewDisabledMask));
        }
        
        // Note: rootNote/scaleIndex are now serialized via CoreState delegation above
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Per-Channel Pre-Range Transform Settings
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Save per-channel voltage scaling factors as JSON array
        {
            json_t* a = json_array();
            for (int i = 0; i < 16; ++i) {
                json_array_append_new(a, json_real(preScale[i]));          // Voltage scaling factor for each channel
            }
            json_object_set_new(rootJ, "preScale", a);
        }
        
        // Save per-channel voltage offset values as JSON array
        {
            json_t* a = json_array();
            for (int i = 0; i < 16; ++i) {
                json_array_append_new(a, json_real(preOffset[i]));         // Voltage offset for each channel
            }
            json_object_set_new(rootJ, "preOffset", a);
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Additional Module Settings
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        json_object_set_new(rootJ, "polyFadeSec", json_real(polyFadeSec));            // Polyphony transition fade duration
        json_object_set_new(rootJ, "quantizerPos", json_integer(quantizerPos));       // Quantizer position (0=Pre, 1=Post)
        
        return rootJ;
    }
    
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════
    // JSON Persistence: dataFromJson() - Restore Module State
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════
    // Restores all saved fields with legacy compatibility. Also restores dual-mode bank values to knobs 
    // and modes to switches. Performs one-time migrations guarded by flags as needed.
    void dataFromJson(json_t* rootJ) override {
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Core Module Configuration Restoration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Handle legacy forcePolyOut => forcedChannels migration
        if (auto* j = json_object_get(rootJ, "forcedChannels")) {
            forcedChannels = (int)json_integer_value(j);                   // Read new format
        } else {
            bool legacyForce = hi::util::jsonh::readBool(rootJ, "forcePolyOut", false);
            if (legacyForce) forcedChannels = 16;                          // Legacy: force 16 channels
        }
        
        // Restore core module settings with fallback defaults
        sumToMonoOut = hi::util::jsonh::readBool(rootJ, "sumToMonoOut", sumToMonoOut);
        avgWhenSumming = hi::util::jsonh::readBool(rootJ, "avgWhenSumming", avgWhenSumming);
        pitchSafeGlide = hi::util::jsonh::readBool(rootJ, "pitchSafeGlide", pitchSafeGlide);
        softClipOut = hi::util::jsonh::readBool(rootJ, "softClipOut", softClipOut);
        
        // Restore integer settings with JSON validation
        if (auto* j = json_object_get(rootJ, "clipVppIndex")) clipVppIndex = (int)json_integer_value(j);
        if (auto* j = json_object_get(rootJ, "rangeMode")) rangeMode = (int)json_integer_value(j);
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Offset Snap Mode Configuration Restoration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Read new enum format; if absent, fall back to legacy boolean
        if (auto* jm = json_object_get(rootJ, "snapOffsetMode")) {
            if (json_is_integer(jm)) snapOffsetMode = (int)json_integer_value(jm);
        }
        
        // Restore per-channel modes (new format). If missing, seed from global/legacy.
        bool seededFromLegacy = false;
        if (auto* arr = json_object_get(rootJ, "snapOffsetModeCh")) {
            if (json_is_array(arr) && json_array_size(arr) == 16) {
                for (int i = 0; i < 16; ++i) {
                    auto* v = json_array_get(arr, i);
                    if (v && json_is_integer(v)) snapOffsetModeCh[i] = (int)json_integer_value(v);
                }
                seededFromLegacy = true;
            }
        }
        
        // Handle legacy compatibility if per-channel modes weren't found
        if (!seededFromLegacy) {
            // Legacy paths: use old enum or boolean format
            int legacyMode = snapOffsetMode;
            if (legacyMode == 0) {
                // Check for even older boolean format
                bool legacyBool = hi::util::jsonh::readBool(rootJ, "snapOffsets", false);
                if (legacyBool) legacyMode = 1;                         // Convert legacy boolean to semitone mode
            }
            // Seed all channels with the legacy global mode
            for (int i = 0; i < 16; ++i) snapOffsetModeCh[i] = legacyMode;
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Dual-Mode Global Controls Restoration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Global slew control: restore mode and bank values
        gSlew.mode = hi::util::jsonh::readBool(rootJ, "globalSlewMode", gSlew.mode);
        if (auto* j = json_object_get(rootJ, "globalSlewBank0")) gSlew.a = (float)json_number_value(j);
        if (auto* j = json_object_get(rootJ, "globalSlewBank1")) gSlew.b = (float)json_number_value(j);
        
        // Global offset control: restore mode and bank values
        gOffset.mode = hi::util::jsonh::readBool(rootJ, "globalOffsetMode", gOffset.mode);
        if (auto* j = json_object_get(rootJ, "globalOffsetBank0")) gOffset.a = (float)json_number_value(j);
        if (auto* j = json_object_get(rootJ, "globalOffsetBank1")) gOffset.b = (float)json_number_value(j);
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Always-On Flags for Dual-Mode Controls
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        attenuverterAlwaysOn = hi::util::jsonh::readBool(rootJ, "attenuverterAlwaysOn", attenuverterAlwaysOn);
        slewAddAlwaysOn = hi::util::jsonh::readBool(rootJ, "slewAddAlwaysOn", slewAddAlwaysOn);
        globalOffsetAlwaysOn = hi::util::jsonh::readBool(rootJ, "globalOffsetAlwaysOn", globalOffsetAlwaysOn);
        rangeOffsetAlwaysOn = hi::util::jsonh::readBool(rootJ, "rangeOffsetAlwaysOn", rangeOffsetAlwaysOn);
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Parameter Synchronization: Update Knob Positions to Reflect Loaded Modes
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Ensure physical knob positions match the loaded dual-mode states
        params[GLOBAL_SLEW_PARAM].setValue(gSlew.mode ? gSlew.b : gSlew.a);            // Set knob to active bank value
        params[GLOBAL_SLEW_MODE_PARAM].setValue(gSlew.mode ? 1.f : 0.f);               // Set mode switch position
        params[GLOBAL_OFFSET_PARAM].setValue(gOffset.mode ? gOffset.b : gOffset.a);    // Set knob to active bank value
        params[GLOBAL_OFFSET_MODE_PARAM].setValue(gOffset.mode ? 1.f : 0.f);           // Set mode switch position
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Strum Configuration Restoration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        strumEnabled = hi::util::jsonh::readBool(rootJ, "strumEnabled", strumEnabled);
        if (auto* j = json_object_get(rootJ, "strumMode")) strumMode = (int)json_integer_value(j);
        if (auto* j = json_object_get(rootJ, "strumType")) strumType = (int)json_integer_value(j);
        if (auto* j = json_object_get(rootJ, "strumMs")) strumMs = (float)json_number_value(j);
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Randomization Scope Configuration Restoration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        randSlew = hi::util::jsonh::readBool(rootJ, "randSlew", randSlew);              // Include slew rates in randomization
        randOffset = hi::util::jsonh::readBool(rootJ, "randOffset", randOffset);        // Include offsets in randomization
        randShapes = hi::util::jsonh::readBool(rootJ, "randShapes", randShapes);        // Include curve shapes in randomization
        if (auto* j = json_object_get(rootJ, "randMaxPct")) randMaxPct = (float)json_number_value(j);
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Auto-Randomization Configuration Restoration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        rndAutoEnabled = hi::util::jsonh::readBool(rootJ, "rndAutoEnabled", rndAutoEnabled);  // Enable automatic randomization
        rndSyncMode = hi::util::jsonh::readBool(rootJ, "rndSyncMode", rndSyncMode);           // Sync mode (clock ratios vs free time)
        
        // Restore per-mode raw time knob values for dual-mode time parameter
        if (auto* j = json_object_get(rootJ, "rndTimeRawFree")) rndTimeRawFree = (float)json_number_value(j);
        if (auto* j = json_object_get(rootJ, "rndTimeRawSync")) rndTimeRawSync = (float)json_number_value(j);
        if (auto* j = json_object_get(rootJ, "rndTimeRaw")) rndTimeRawLoaded = (float)json_number_value(j); // Legacy single value
        
        // Handle legacy compatibility: if new per-mode values weren't found, seed both from legacy
        if (rndTimeRawFree < 0.f || rndTimeRawFree > 1.f) rndTimeRawFree = rndTimeRawLoaded;
        if (rndTimeRawSync < 0.f || rndTimeRawSync > 1.f) rndTimeRawSync = rndTimeRawLoaded;
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Parameter Value Restoration: Apply Loaded Values to Physical Controls
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Set randomization parameter values to match loaded state
        if (RND_TIME_PARAM < PARAMS_LEN) params[RND_TIME_PARAM].setValue(rndSyncMode ? rndTimeRawSync : rndTimeRawFree);
        if (RND_AMT_PARAM < PARAMS_LEN) params[RND_AMT_PARAM].setValue(randMaxPct);
        if (RND_AUTO_PARAM < PARAMS_LEN) params[RND_AUTO_PARAM].setValue(rndAutoEnabled ? 1.f : 0.f);
        if (RND_SYNC_PARAM < PARAMS_LEN) params[RND_SYNC_PARAM].setValue(rndSyncMode ? 1.f : 0.f);
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Per-Channel Quantization and Octave Shift Settings Restoration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        for (int i = 0; i < 16; ++i) {
            char key[32];
            // Restore quantization enable state for each channel
            std::snprintf(key, sizeof(key), "qzEnabled%d", i + 1);
            qzEnabled[i] = hi::util::jsonh::readBool(rootJ, key, qzEnabled[i]);
            
            // Restore post-quantization octave shift for each channel
            std::snprintf(key, sizeof(key), "postOctShift%d", i + 1);
            if (auto* jv = json_object_get(rootJ, key)) postOctShift[i] = (int)json_integer_value(jv);
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Quantization Core State Restoration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Delegate quantization settings (scales, tuning, etc.) restoration to core DSP module
        {
            hi::dsp::CoreState cs;                                      // Create core state structure with defaults
            hi::dsp::coreFromJson(rootJ, cs);                          // Deserialize core state from JSON
            applyCoreStateToModule(cs, *this);                         // Apply core state to module variables
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Randomization Lock and Allow States Restoration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Per-channel randomization control settings
        for (int i = 0; i < 16; ++i) {
            char key[32];
            // Restore lock states: prevent randomization of specific parameters
            std::snprintf(key, sizeof(key), "lockSlew%d", i + 1);
            lockSlew[i] = hi::util::jsonh::readBool(rootJ, key, lockSlew[i]);      // Lock slew rate from randomization
            std::snprintf(key, sizeof(key), "lockOffset%d", i + 1);
            lockOffset[i] = hi::util::jsonh::readBool(rootJ, key, lockOffset[i]);  // Lock offset from randomization
            
            // Restore allow states: enable randomization of specific parameters
            std::snprintf(key, sizeof(key), "allowSlew%d", i + 1);
            allowSlew[i] = hi::util::jsonh::readBool(rootJ, key, allowSlew[i]);    // Allow slew rate randomization
            std::snprintf(key, sizeof(key), "allowOffset%d", i + 1);
            allowOffset[i] = hi::util::jsonh::readBool(rootJ, key, allowOffset[i]); // Allow offset randomization
        }
        
        // Global curve shape randomization control
        lockRiseShape = hi::util::jsonh::readBool(rootJ, "lockRiseShape", lockRiseShape);     // Lock rise curve shape from randomization
        lockFallShape = hi::util::jsonh::readBool(rootJ, "lockFallShape", lockFallShape);     // Lock fall curve shape from randomization
        allowRiseShape = hi::util::jsonh::readBool(rootJ, "allowRiseShape", allowRiseShape);  // Allow rise curve shape randomization
        allowFallShape = hi::util::jsonh::readBool(rootJ, "allowFallShape", allowFallShape);  // Allow fall curve shape randomization
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Per-Channel Slew Enable State Restoration (Compact Bitmask)
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Restore slew enable states from compact 16-bit bitmask (backward compatible: missing key defaults to all enabled)
        {
            uint16_t slewDisabledMask = 0;
            if (auto* j = json_object_get(rootJ, "slewDisabledMask")) {
                slewDisabledMask = (uint16_t)json_integer_value(j);
            }
            // Restore per-channel enable state from bitmask (bit=1 means disabled)
            for (int i = 0; i < 16; ++i) {
                slewEnabled[i] = !(slewDisabledMask & (1 << i));           // Convert bitmask back to boolean array
            }
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Per-Channel Pre-Range Transform Settings Restoration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Restore per-channel voltage scaling factors from JSON array (defaults already set in constructor)
        if (auto* arr = json_object_get(rootJ, "preScale")) {
            if (json_is_array(arr)) {
                size_t n = json_array_size(arr);
                for (size_t i = 0; i < n && i < 16; ++i) {
                    preScale[i] = (float)json_number_value(json_array_get(arr, i));  // Voltage scaling factor for each channel
                }
            }
        }
        
        // Restore per-channel voltage offset values from JSON array
        if (auto* arr = json_object_get(rootJ, "preOffset")) {
            if (json_is_array(arr)) {
                size_t n = json_array_size(arr);
                for (size_t i = 0; i < n && i < 16; ++i) {
                    preOffset[i] = (float)json_number_value(json_array_get(arr, i)); // Voltage offset for each channel
                }
            }
        }

        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Additional Module Settings Restoration
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Note: rootNote/scaleIndex are restored via CoreState delegation above
        if (auto* j = json_object_get(rootJ, "polyFadeSec")) polyFadeSec = (float)json_number_value(j);
        
        // Restore quantizer position with backward compatibility
        if (auto* jq = json_object_get(rootJ, "quantizerPos")) {
            if (json_is_integer(jq)) quantizerPos = (int)json_integer_value(jq);
        } else {
            quantizerPos = QuantizerPos::Pre;                               // Legacy: preserve old Quantize→Slew chain
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // One-Time Migration Handling
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Placeholder for future migrations (currently no direct param storage across sessions)
        if (!migratedQZ) {
            for (int i = 0; i < 16; ++i) {
                // Future migration logic would go here
                // Currently no action needed as quantization settings are handled via CoreState
            }
            migratedQZ = true;                                              // Mark migration as completed
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════
    // Module Lifecycle: onReset() - Clear Runtime State
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════
    // Clears per-channel step tracking, cached rates, LEDs, and strum delay state. 
    // Does not modify user options or saved parameters.
    void onReset() override {
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Per-Channel State Reset
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        for (int i = 0; i < 16; ++i) {
            stepNorm[i] = 10.f;                                         // Reset step normalization to safe default
            stepSign[i] = 0;                                            // Reset step direction to neutral
            prevRiseRate[i] = -1.f;                                     // Mark rise rate as uninitialized
            prevFallRate[i] = -1.f;                                     // Mark fall rate as uninitialized
            lastOut[i] = 0.f;                                           // Clear last output value
            lights[CH_LIGHT + 2*i + 0].setBrightness(0.f);             // Turn off positive LED
            lights[CH_LIGHT + 2*i + 1].setBrightness(0.f);             // Turn off negative LED
            strumDelayAssigned[i] = 0.f;                                // Clear assigned strum delay
            strumDelayLeft[i] = 0.f;                                    // Clear remaining strum delay
            strumPrevTarget[i] = 0.f;                                   // Reset last processed target snapshot
            strumPrevInit[i] = false;                                   // Mark strum target history as uninitialized
            latchedInit[i] = false;                                     // Reset initialization latch
            preScale[i] = 1.f;                                          // Reset pre-range scaling to unity
            preOffset[i] = 0.f;                                         // Reset pre-range offset to zero
            latchedStep[i] = 0;                                         // Reset step latch counter
            prevYRel[i] = 0.f;                                          // Reset previous relative position
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Auto-Randomization Timing State Reset
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        rndTimerSec = 0.0;                                              // Reset free-running timer with double precision accumulator
        rndClockPeriodSec = -1.0;                                       // Reset detected clock period stored as double
        rndClockLastEdge = -1.0;                                        // Reset last clock edge time (double precision timestamp)
        rndClockReady = false;                                          // Reset clock detection state
        rndAbsTimeSec = 0.0;                                            // Reset absolute time counter maintained in double
        rndNextFireTime = -1.0;                                         // Reset next randomization time (double timestamp)
        
        // Reset clock division/multiplication state
        rndDivCounter = 0;                                              // Reset division counter
        rndCurrentDivide = 1;                                           // Reset current division factor
        rndCurrentMultiply = 1;                                         // Reset current multiplication factor
        rndMulIndex = 0;                                                // Reset multiplication index
        rndMulBaseTime = -1.0;                                          // Reset multiplication base time using double anchor
        rndMulNextTime = -1.0;                                          // Reset next multiplication time with double precision
        rndPrevRatioIdx = -1;                                           // Reset previous ratio index
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════
    // Randomization: doRandomize() - Apply Scoped Parameter Randomization
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════
    // Applies scoped random changes to slews, offsets, and shape knobs. Honors per-control locks 
    // (when scope ON) or allows (when scope OFF). Magnitude is bounded by the Max percentage option.
    void doRandomize() {
        using hi::util::rnd::randSpanClamp;
        float maxPct = clamp(randMaxPct, 0.f, 1.f);                     // Clamp randomization strength to valid range
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Per-Channel Slew and Offset Randomization
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        for (int i = 0; i < 16; ++i) {
            // Determine if slew rate should be randomized based on scope and lock/allow state
            bool doSlew = randSlew ? (!lockSlew[i]) : allowSlew[i];     // Scope ON: respect locks, Scope OFF: respect allows
            if (doSlew) {
                float v = params[SL_PARAM[i]].getValue();               // Get current slew rate [0,1]
                randSpanClamp(v, 0.f, 1.f, maxPct);                    // Apply bounded randomization
                params[SL_PARAM[i]].setValue(v);                       // Update parameter value
            }
            
            // Determine if offset should be randomized based on scope and lock/allow state
            bool doOff = randOffset ? (!lockOffset[i]) : allowOffset[i]; // Scope ON: respect locks, Scope OFF: respect allows
            if (doOff) {
                float v = params[OFF_PARAM[i]].getValue();              // Get current offset [-10,10]
                randSpanClamp(v, -10.f, 10.f, maxPct);                 // Apply bounded randomization
                params[OFF_PARAM[i]].setValue(v);                      // Update parameter value
            }
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Global Curve Shape Randomization (Rise/Fall)
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        {
            // Determine if rise shape should be randomized based on scope and lock/allow state
            bool doRise = randShapes ? (!lockRiseShape) : allowRiseShape; // Scope ON: respect locks, Scope OFF: respect allows
            if (doRise) {
                float v = params[RISE_SHAPE_PARAM].getValue();          // Get current rise shape [-1,1]
                randSpanClamp(v, -1.f, 1.f, maxPct);                   // Apply bounded randomization
                params[RISE_SHAPE_PARAM].setValue(v);                  // Update parameter value
            }
            
            // Determine if fall shape should be randomized based on scope and lock/allow state
            bool doFall = randShapes ? (!lockFallShape) : allowFallShape; // Scope ON: respect locks, Scope OFF: respect allows
            if (doFall) {
                float v = params[FALL_SHAPE_PARAM].getValue();          // Get current fall shape [-1,1]
                randSpanClamp(v, -1.f, 1.f, maxPct);                   // Apply bounded randomization
                params[FALL_SHAPE_PARAM].setValue(v);                  // Update parameter value
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════
    // Audio/DSP: process() - Main Processing Loop
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════
    // 
    // This is the core DSP processing function that handles all real-time audio processing for the 
    // PolyQuanta module. It implements a sophisticated polyphonic slew processor with musical 
    // quantization, strum timing, and advanced fade management for seamless polyphony transitions.
    //
    // ┌─ POLYPHONY MANAGEMENT ─────────────────────────────────────────────────────────────────────────┐
    // │ • Auto-detects or uses forced channel counts (1-16 channels)                                   │
    // │ • Implements pop-free polyphony transitions with configurable fade times                       │
    // │ • Supports mono summing output with optional averaging                                         │
    // │ • Manages fade-out → channel switch → fade-in state machine for smooth transitions             │
    // └────────────────────────────────────────────────────────────────────────────────────────────────┘
    //
    // ┌─ RANDOMIZATION SYSTEM ─────────────────────────────────────────────────────────────────────────┐
    // │ • Manual trigger: immediate randomization via button or external trigger                       │
    // │ • Auto-randomization: free-running timer mode (1ms-10000s logarithmic scale)                   │
    // │ • Clock sync mode: division (÷64..÷2), 1×, or multiplication (×2..×64) ratios                  │
    // │ • Clock measurement with EMA smoothing and subdivision timing for multiplication               │
    // │ • Configurable randomization scope with per-control lock states                                │
    // └────────────────────────────────────────────────────────────────────────────────────────────────┘
    //
    // ┌─ DUAL-MODE GLOBAL CONTROLS ────────────────────────────────────────────────────────────────────┐
    // │ • Slew control: Mode A (slew-add seconds) ↔ Mode B (attenuverter -10V to +10V)                 │
    // │ • Offset control: Mode A (global offset ±10V) ↔ Mode B (range offset ±5V)                      │
    // │ • Bank switching with value persistence and "always on" override options                       │
    // │ • Range offset applied AFTER range processing, BEFORE quantizer                                │
    // │ • Global offset combined with per-channel offsets                                              │
    // └────────────────────────────────────────────────────────────────────────────────────────────────┘
    //
    // ┌─ TWO-PASS PROCESSING ARCHITECTURE ─────────────────────────────────────────────────────────────┐
    // │ Pass 1: Target Calculation & Step Detection                                                    │
    // │ • Input processing with polyphonic handling and global attenuverter                            │
    // │ • Per-channel and global offset combination with optional quantization                         │
    // │ • Per-channel pre-range scaling and offset transforms                                          │
    // │ • Step change detection (direction and magnitude) for strum timing                             │
    // │ • Strum delay assignment: up/down/random patterns with time-stretch or start-delay modes       │
    // │                                                                                                │
    // │ Pass 2: Slew, Range, and Quantization Processing                                               │
    // │ • Shape-aware slew processing with rise/fall curve modulation                                  │
    // │ • Strum timing: time-stretch (extends slew) or start-delay (holds previous output)             │
    // │ • Pre-quantization range limiting (clip/scale around 0V) and range offset application          │
    // │ • Per-channel octave shifting before quantizer                                                 │
    // │ • Dual quantizer positioning: Pre (Q→S legacy) or Post (S→Q pitch-stable) modes                │
    // └────────────────────────────────────────────────────────────────────────────────────────────────┘
    //
    // ┌─ ADVANCED QUANTIZATION ENGINE ─────────────────────────────────────────────────────────────────┐
    // │ • Multi-tuning support: 12/24-EDO, custom EDOs (1-127), and non-octave TET systems             │
    // │ • Scale systems: built-in scales, custom masks (12/24/generic-EDO), MOS pattern detection      │
    // │ • Directional snap with hysteresis: maintains quantization stability during slow changes       │
    // │ • Schmitt latch logic: center-anchored hysteresis prevents oscillation near step boundaries    │
    // │ • Advanced rounding modes: nearest, directional (slope-aware), ceiling, floor                  │
    // │ • Blendable quantization strength (0%=raw, 100%=quantized) with smooth crossfading             │
    // │ • Per-channel quantizer enable/disable and configuration change detection                      │
    // └────────────────────────────────────────────────────────────────────────────────────────────────┘
    //
    // ┌─ SLEW PROCESSING & CURVE SHAPING ──────────────────────────────────────────────────────────────┐
    // │ • Shape-aware slew rates: logarithmic to exponential curve control for rise and fall           │
    // │ • Dynamic rate calculation based on remaining distance and target time                         │
    // │ • Per-channel slew time controls with global slew-add contribution                             │
    // │ • Pitch-safe mode: semitone-aware error calculation for musical slewing                        │
    // │ • Rate change optimization: updates only when rates change significantly (anti-zipper)         │
    // └────────────────────────────────────────────────────────────────────────────────────────────────┘
    //
    // ┌─ OUTPUT PROCESSING & SAFETY ───────────────────────────────────────────────────────────────────┐
    // │ • Final voltage limiting: soft clipping (smooth saturation) or hard limiting                   │
    // │ • Polyphonic output with per-channel fade ramp application                                     │
    // │ • Mono summing mode with optional averaging for multiple channels                              │
    // │ • Per-channel LED management with bipolar voltage indication                                   │
    // │ • Inactive channel cleanup to prevent visual artifacts                                         │
    // └────────────────────────────────────────────────────────────────────────────────────────────────┘
    //
    // The processing order varies by quantizer position:
    // • Pre Mode (Q→S): Input → Offset → PreRange → [Quantize → Blend] → Slew → Output
    // • Post Mode (S→Q): Input → Offset → PreRange → Slew → [Quantize → Blend] → Output
    //
    // This architecture ensures musical accuracy, smooth transitions, and pop-free polyphony changes
    // while maintaining real-time performance for up to 16 simultaneous channels.

        void process(const ProcessArgs& args) override {
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Polyphony Management: Determine Input/Output Channel Counts with Fading
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Detect input connection and determine channel count for processing
        const bool inConn = inputs[IN_INPUT].isConnected();                 // Check if input is connected
        const int inCh = inConn ? inputs[IN_INPUT].getChannels() : 0;       // Get input channel count
            // Calculate desired processing and output channel counts
            int desiredProcN = 0;
            if (forcedChannels > 0)
            desiredProcN = rack::math::clamp(forcedChannels, 1, 16);    // User-forced channel count
            else
            desiredProcN = hi::dsp::poly::processWidth(false, inConn, inCh, 16); // Auto-detect from input
        int desiredOutN = sumToMonoOut ? 1 : desiredProcN;             // Mono sum or match processing width

    // Note: We intentionally use polyTrans.* directly below (no local aliases)
    // to make it obvious these are fields of the shared transition state.

        // Initialize current counts on first process() call
        if (polyTrans.curProcN <= 0 && polyTrans.curOutN <= 0) {
            polyTrans.curProcN = desiredProcN;                          // Set initial processing channel count
            polyTrans.curOutN = desiredOutN;                            // Set initial output channel count
            outputs[OUT_OUTPUT].setChannels(polyTrans.curOutN);         // Configure output port channels
            polyTrans.transPhase = TRANS_STABLE;                        // Mark as stable (no transition)
            polyTrans.polyRamp = 1.f;                                   // Full volume (no fade)
        }

        // Detect a change in desired channel counts and initiate transition
        bool widthChange = (desiredProcN != polyTrans.curProcN) || (desiredOutN != polyTrans.curOutN);
        if (widthChange && polyTrans.transPhase == TRANS_STABLE) {
            polyTrans.pendingProcN = desiredProcN;                      // Store pending processing count
            polyTrans.pendingOutN = desiredOutN;                        // Store pending output count
            
            // Start fade out if time > 0, otherwise immediate switch
            if (polyFadeSec > 0.f) {
                polyTrans.transPhase = TRANS_FADE_OUT;                  // Begin fade-out transition
            } else {
                // Immediate switch (no fade time configured)
                polyTrans.curProcN = polyTrans.pendingProcN;           // Apply new processing count immediately
                polyTrans.curOutN = polyTrans.pendingOutN;             // Apply new output count immediately
                outputs[OUT_OUTPUT].setChannels(polyTrans.curOutN);    // Update output port channels
                polyTrans.initToTargetsOnSwitch = true;                // Flag for target initialization
                polyTrans.transPhase = TRANS_STABLE;                   // Return to stable state
                polyTrans.polyRamp = 1.f;                              // Full volume (no fade needed)
            }
        }

        // Set the output channel count for this block (use current, not desired during transitions)
        outputs[OUT_OUTPUT].setChannels(polyTrans.curOutN);

        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Randomization System: Handle Manual Triggers and Auto-Randomization Timing
        // ───────────────────────────────────────────────────────────────────────────────────────────────
            // Update randomization parameters from front-panel controls
            if (RND_AMT_PARAM < PARAMS_LEN)
            randMaxPct = rack::clamp(params[RND_AMT_PARAM].getValue(), 0.f, 1.f); // Clamp randomization strength
        
        // Cache toggle states for randomization system
            if (RND_AUTO_PARAM < PARAMS_LEN) rndAutoEnabled = params[RND_AUTO_PARAM].getValue() > 0.5f;
        if (RND_SYNC_PARAM < PARAMS_LEN) rndSyncMode = params[RND_SYNC_PARAM].getValue() > 0.5f;
        
        // Handle mode switch: recall per-mode stored raw value & reset schedulers appropriately
        if (rndSyncMode != prevRndSyncMode) {
            if (RND_TIME_PARAM < PARAMS_LEN) params[RND_TIME_PARAM].setValue(rndSyncMode ? rndTimeRawSync : rndTimeRawFree);
            if (rndSyncMode) { rndNextFireTime = -1.0; } else { rndTimerSec = 0.0; } // Reset appropriate scheduler with double timestamps
            prevRndSyncMode = rndSyncMode;                              // Update previous state
        }
        
        // Manual randomization button always fires immediately
        bool manualFire = rndBtnTrig.process(params[RND_PARAM].getValue() > 0.5f);
        
        // External trigger immediate fire only when NOT in sync mode (in sync mode, triggers are used for timing)
        bool extFire = (!rndSyncMode) && rndGateTrig.process(inputs[RND_TRIG_INPUT].getVoltage());
        if (manualFire || extFire) {
            doRandomize();                                              // Execute randomization immediately
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Auto-Randomization Clock Measurement and Timing (Sync Mode)
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Measure external clock period when in sync mode (edges do NOT randomize directly)
        const double dt = args.sampleTime;                              // Get sample time delta using double precision for timing
        rndAbsTimeSec += dt;                                            // Update absolute time counter
        bool edgeThisBlock = false;
        
        if (rndSyncMode) {
            // Measure clock period (single SchmittTrigger.process call per block)
            edgeThisBlock = rndClockTrig.process(inputs[RND_TRIG_INPUT].getVoltage());
            if (edgeThisBlock) {
                if (rndClockLastEdge >= 0.0) {                          // Valid previous edge exists
                    double p = rndAbsTimeSec - rndClockLastEdge;        // Calculate period between edges with double precision
                    if (p > 1e-4) {                                     // Minimum valid period threshold
                        const double alpha = 0.25;                      // EMA smoothing factor (slightly faster than previous 0.2)
                        if (rndClockPeriodSec < 0.0)
                            rndClockPeriodSec = p;                      // Initialize with first measurement
                        else
                            rndClockPeriodSec = (1.0 - alpha) * rndClockPeriodSec + alpha * p; // Smooth with EMA in double precision
                        rndClockReady = true;                           // Mark clock as stable and ready
                    }
                }
                rndClockLastEdge = rndAbsTimeSec;                       // Update last edge timestamp
                rndDivCounter++;                                        // Advance division counter for clock division ratios
            }
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Auto-Randomization Scheduling: Free-Running or Clock-Synchronized
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        if (rndAutoEnabled) {
            float raw = (RND_TIME_PARAM < PARAMS_LEN) ? params[RND_TIME_PARAM].getValue() : 0.5f;
            
            // Lambda for converting raw parameter [0..1] to seconds (logarithmic scale)
            auto rawToSec = [](float r) { 
                const float mn = 0.001f, mx = 10000.f;                 // 1ms to 10000s range
                float lmn = std::log10(mn), lmx = std::log10(mx);      // Log scale bounds
                float lx = lmn + rack::clamp(r, 0.f, 1.f) * (lmx - lmn); // Map to log range
                return std::pow(10.f, lx);                             // Convert back to linear time
            };
            
            // Clock sync ratio mapping: indices 0..125 divides (÷64..÷2), 126 center (1×), 127..251 multiplies (×2..×64)
            const int DIV_MAX = 64;
            const int TOTAL_SYNC_STEPS = (DIV_MAX - 1) + 1 + (DIV_MAX - 1); // 127 positions total
            const int SYNC_LAST_INDEX = TOTAL_SYNC_STEPS - 1;          // Index 126 (last valid position)
            
            if (rndSyncMode) {
                rndTimeRawSync = raw;                                   // Store raw value for sync mode
                if (rndClockReady && rndClockPeriodSec > 0.0) {
                    int idx = (int)std::lround(rack::clamp(raw, 0.f, 1.f) * SYNC_LAST_INDEX); // Map to 0..126
                    if (idx < 0) idx = 0; else if (idx > SYNC_LAST_INDEX) idx = SYNC_LAST_INDEX; // Safety clamp
                    
                    // Calculate division and multiplication factors based on index position
                    int div = 1, mul = 1;
                    if (idx < (DIV_MAX - 1)) {                          // Division region (÷64..÷2)
                        int d = DIV_MAX - idx;                          // Calculate division factor: 64..2
                        div = d; mul = 1;
                    } else if (idx == (DIV_MAX - 1)) {                  // Center position (1×)
                        div = 1; mul = 1;
                    } else {                                            // Multiplication region (×2..×64)
                        int mfac = (idx - (DIV_MAX - 1)) + 1;          // Calculate multiplication factor: 2..64
                        div = 1; mul = mfac;
                    }

                    // Detect significant ratio changes (ignore tiny index flips from knob jitter)
                    bool ratioChanged = (div != rndCurrentDivide) || (mul != rndCurrentMultiply);
                    if (ratioChanged) {
                        // Reset phase/counters when ratio changes; anchor timing is chosen below per case
                        rndMulIndex = 0;                                // Reset multiplication phase counter
                        rndMulNextTime = -1.0;                          // Clear next scheduled time (double precision value)
                        if (div > 1) rndDivCounter = 0;                 // Reset division counter for new ratio
                    }
                    
                    // Store current ratio for UI display and future comparisons
                    rndCurrentDivide = div;
                    rndCurrentMultiply = mul;

                    // ───────────────────────────────────────────────────────────────────────────
                    // Clock Division/Multiplication Logic: Execute Randomization at Proper Times
                    // ───────────────────────────────────────────────────────────────────────────
                    if (div > 1 && mul == 1) {
                        // Pure division: emit randomization only on qualifying edges (every Nth clock)
                        if (edgeThisBlock && (rndDivCounter % div) == 0) doRandomize();
                    } else if (div == 1 && mul == 1) {
                        // 1× ratio: fire randomization on every clock edge
                        if (edgeThisBlock) doRandomize();
                    } else if (mul > 1 && div == 1) {
                        // Clock multiplication: 1 pulse AT the edge, then (mul-1) evenly spaced subdivisions
                        // Re-anchor timing on clock edge (best alignment) or when ratio changes
                        if (edgeThisBlock) {
                            doRandomize();                              // Execute randomization at clock edge
                            rndMulBaseTime = rndClockLastEdge;          // Align exactly to measured edge timestamp
                            rndMulIndex = 0;                            // Reset subdivision counter
                            if (rndClockPeriodSec > 0.0) {
                                double subdiv = rndClockPeriodSec / (double)mul; // Calculate subdivision interval using doubles
                                rndMulNextTime = rndMulBaseTime + subdiv;   // Schedule first subdivision
                            } else {
                                rndMulNextTime = -1.0;                  // Invalid period, disable subdivisions
                            }
                        } else if (ratioChanged) {
                            // If user changed ratio between clock edges, anchor timing to current time
                            rndMulBaseTime = rndAbsTimeSec;             // Use current time as base
                            rndMulIndex = 0;                            // Reset subdivision counter
                            if (rndClockPeriodSec > 0.0) {
                                double subdiv = rndClockPeriodSec / (double)mul; // Calculate subdivision interval using doubles
                                rndMulNextTime = rndMulBaseTime + subdiv;   // Schedule first subdivision
                            } else {
                                rndMulNextTime = -1.0;                      // Invalid period, disable subdivisions
                            }
                        }

                        // Execute interior subdivision pulses when their scheduled times arrive
                        if (rndMulNextTime >= 0.0 && rndClockPeriodSec > 0.0) {
                            double subdiv = rndClockPeriodSec / (double)mul;  // Subdivision interval maintained in double
                            while (rndMulNextTime >= 0.0 && rndMulNextTime <= rndAbsTimeSec + 1e-9) {
                                doRandomize();                              // Execute randomization at subdivision
                                rndMulIndex++;                              // Advance subdivision counter
                                if (rndMulIndex >= mul - 1) {               // All subdivisions completed?
                                    rndMulNextTime = -1.0;                  // Disable further subdivisions until next edge
                                    break;
                                }
                                rndMulNextTime += subdiv;                   // Schedule next subdivision
                            }
                        }
                    }
                }
            } else {
                // ───────────────────────────────────────────────────────────────────────────
                // Free-Running Mode: Logarithmic Time-Based Auto-Randomization
                // ───────────────────────────────────────────────────────────────────────────
                rndTimeRawFree = raw;                                       // Store raw value for free mode
                float intervalSec = rawToSec(raw);                          // Convert to seconds using log scale
                if (intervalSec < 0.001f) intervalSec = 0.001f;            // Enforce minimum interval (1ms)
                rndTimerSec += dt;                                          // Accumulate elapsed time in double precision
                if (rndTimerSec >= intervalSec) {                           // Time to randomize?
                    doRandomize();                                          // Execute randomization
                    while (rndTimerSec >= intervalSec) rndTimerSec -= intervalSec; // Handle multiple intervals per block
                }
            }
        } else {
            // Auto-randomization disabled: prevent timer overflow for long idle periods
            if (rndTimerSec > 60.0) rndTimerSec = std::fmod(rndTimerSec, 60.0); // Wrap timer safely using double constants
        }

            // ═══════════════════════════════════════════════════════════════════════════
            // GLOBAL PARAMETER PREPARATION - Extract and precompute shared values
            // ═══════════════════════════════════════════════════════════════════════════
            
            // Extract global slew curve shape parameters and precompute coefficients
            const float riseShape = params[RISE_SHAPE_PARAM].getValue(); // [-1,1]: log to exp curve
            const float fallShape = params[FALL_SHAPE_PARAM].getValue(); // [-1,1]: log to exp curve
            auto riseParams = hi::dsp::glide::makeShape(riseShape);      // Precomputed curve coefficients
            auto fallParams = hi::dsp::glide::makeShape(fallShape);      // Precomputed curve coefficients

            // Calculate voltage range limit for pre-quantization clipping/scaling
            float clipLimit = currentClipLimit();                       // Get current range limit setting
            
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Dual-Mode Global Control Management: Handle Bank Switching and Value Persistence
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        bool modeSlewNow = params[GLOBAL_SLEW_MODE_PARAM].getValue() > 0.5f;  // Current slew mode toggle
        bool modeOffNow = params[GLOBAL_OFFSET_MODE_PARAM].getValue() > 0.5f; // Current offset mode toggle
        
        // Persist active bank values from current raw knob positions
        if (gSlew.mode) gSlew.b = params[GLOBAL_SLEW_PARAM].getValue(); else gSlew.a = params[GLOBAL_SLEW_PARAM].getValue();
        if (gOffset.mode) gOffset.b = params[GLOBAL_OFFSET_PARAM].getValue(); else gOffset.a = params[GLOBAL_OFFSET_PARAM].getValue();
        
        // Handle mode changes: snap knob to saved value of the new bank
        if (modeSlewNow != gSlew.mode) {
            params[GLOBAL_SLEW_PARAM].setValue(modeSlewNow ? gSlew.b : gSlew.a); // Restore bank value
            gSlew.mode = modeSlewNow;                               // Update mode state
        }
        if (modeOffNow != gOffset.mode) {
            params[GLOBAL_OFFSET_PARAM].setValue(modeOffNow ? gOffset.b : gOffset.a); // Restore bank value
            gOffset.mode = modeOffNow;                          // Update mode state
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Derive Active Control Values from Dual-Mode Banks and "Always On" Overrides
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        float gsecAdd = 0.f;                                    // Additional slew time in seconds
        float gGain = 1.f;                                      // Attenuverter gain multiplier
        
        // Determine which modes are active (considering "always on" overrides)
        bool useSlewAdd = (!gSlew.mode) || slewAddAlwaysOn;     // Use slew-add mode
        bool useAttv = (gSlew.mode) || attenuverterAlwaysOn;    // Use attenuverter mode
        
        if (useSlewAdd) {
            // Use the banked value for slew-add when knob is currently set to attenuverter
            float rawSlew = gSlew.mode ? gSlew.a : params[GLOBAL_SLEW_PARAM].getValue();
            gsecAdd = hi::ui::ExpTimeQuantity::knobToSec(rawSlew); // Convert to seconds
        }
        if (useAttv) {
            float rawAttv = gSlew.mode ? params[GLOBAL_SLEW_PARAM].getValue() : gSlew.b;
            rawAttv = rack::math::clamp(rawAttv, 0.f, 1.f);     // Safety clamp
            gGain = -10.f + 20.f * rawAttv;                     // Map to [-10,+10] gain range
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Offset Control Processing: Global and Range Offsets from Dual-Mode Banks
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        float rangeOffset = 0.f;                               // Applied AFTER range, BEFORE quantizer
        float globalOffset = 0.f;                              // Applied with per-channel offsets
        bool useRangeOff = gOffset.mode || rangeOffsetAlwaysOn; // Use range offset mode
        bool useGlobOff = (!gOffset.mode) || globalOffsetAlwaysOn; // Use global offset mode
        
        if (useRangeOff) {
            float v = gOffset.mode ? params[GLOBAL_OFFSET_PARAM].getValue() : gOffset.b;
            rangeOffset = clamp(v, -5.f, 5.f);                 // Range offset: ±5V limit
        }
        if (useGlobOff) {
            float v = gOffset.mode ? gOffset.a : params[GLOBAL_OFFSET_PARAM].getValue();
            globalOffset = clamp(v, -10.f, 10.f);              // Global offset: ±10V limit
        }
        
        // Pre-range limiter/scaler lambda: operates around 0V only; offset is applied after this
        auto preRange = [&](float v) -> float {
            return hi::dsp::range::apply(v, rangeMode == 0 ? hi::dsp::range::Mode::Clip : hi::dsp::range::Mode::Scale, clipLimit, softClipOut);
        };

        // ═══════════════════════════════════════════════════════════════════════════
        // MAIN DSP PROCESSING - Two-Pass Algorithm for Slew and Quantization
        // ═══════════════════════════════════════════════════════════════════════════
        
        bool modeChanged = (pitchSafeGlide != prevPitchSafeGlide);     // Detect pitch-safe mode changes
        float outVals[16] = {0};                                       // Final output values per channel
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Pass 1: Compute Target Values and Detect Step Changes for Strum Timing
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        float targetArr[16] = {0};                                     // Target values after processing
        float aerrNArr[16] = {0};                                      // Normalized step error for strum
        int signArr[16] = {0};                                         // Step direction for strum ordering
        bool targetChangedArr[16] = {false};                           // Tracks raw target changes for strum detection
        
        // Track whether start-delay is active so we tick once per block
        bool strumTickNeeded = false;

        for (int c = 0; c < polyTrans.curProcN; ++c) {
            if (strumEnabled && strumType == 1 && strumDelayLeft[c] > 0.f) {
                strumTickNeeded = true;
            }

            // ───────────────────────────────────────────────────────────────────────────────────────────
            // Input Processing: Polyphonic Input Handling and Global Attenuverter
            // ───────────────────────────────────────────────────────────────────────────────────────────
            float in = 0.f;
            if (inConn) {
                if (inCh <= 1) 
                    in = inputs[IN_INPUT].getVoltage(0);               // Mono input to all channels
                else if (c < inCh) 
                    in = inputs[IN_INPUT].getVoltage(c);               // Polyphonic input per channel
                // Note: channels beyond input count remain at 0V
            }
            
            // Apply global attenuverter (if enabled) to input before offset/slew processing
            if (useAttv) in *= gGain;                                  // Scale input by attenuverter gain
            
            // ───────────────────────────────────────────────────────────────────────────────────────────
            // Offset Processing: Per-Channel and Global Offset Combination
            // ───────────────────────────────────────────────────────────────────────────────────────────
            float offCh = params[OFF_PARAM[c]].getValue();             // Per-channel offset knob
            float offTot = offCh + globalOffset;                       // Combine with global offset
            
            // ───────────────────────────────────────────────────────────────────────────────────────────
            // Offset Quantization: Snap Offsets to Musical Grid (Optional Per-Channel)
            // ───────────────────────────────────────────────────────────────────────────────────────────
            int qm = snapOffsetModeCh[c];                              // Get offset quantization mode for this channel
            if (qm == 1) {                                             // Semitone (EDO or TET) quantization
                int Nsteps = (tuningMode == 0 ? ((edo <= 0) ? 12 : edo) : (tetSteps > 0 ? tetSteps : 9));
                float period = (tuningMode == 0) ? 1.f : ((tetPeriodOct > 0.f) ? tetPeriodOct : std::log2(3.f/2.f));
                // Steps per octave (volts) = steps per period divided by period size in octaves
                float stepsPerOct = (float)Nsteps / period;            // Calculate steps per volt
                offTot = std::round(offTot * stepsPerOct) / stepsPerOct; // Quantize to nearest semitone
            } else if (qm == 2) {                                      // Cents (1/1200 V) quantization
                offTot = std::round(offTot * 1200.f) / 1200.f;         // Quantize to nearest cent
            }
            // qm == 0: No offset quantization (free voltage)
            
            float target = in + offTot;                                // Calculate initial target value
            targetArr[c] = target;                                     // Store for later processing

            // ───────────────────────────────────────────────────────────────────────────────────────────
            // Per-Channel Pre-Range Transform: Apply Scaling and Offset Before Global Range Processing
            // ───────────────────────────────────────────────────────────────────────────────────────────
            targetArr[c] = targetArr[c] * preScale[c] + preOffset[c];  // Apply per-channel transform
            float targetProcessed = targetArr[c];                      // Cache processed target for reuse

            // Detect actual target changes for strum handling (skip noise-level moves)
            bool targetChanged = false;
            if (strumPrevInit[c]) {
                targetChanged = std::fabs(targetProcessed - strumPrevTarget[c]) > STRUM_TARGET_TOL;
            }
            strumPrevTarget[c] = targetProcessed;                      // Update history for next block
            strumPrevInit[c] = true;                                   // Mark history initialized
            targetChangedArr[c] = targetChanged;                       // Remember change flag for assignment

            // ───────────────────────────────────────────────────────────────────────────────────────────
            // Step Detection: Calculate Error and Direction for Strum Timing
            // ───────────────────────────────────────────────────────────────────────────────────────────
            float yPrev = lastOut[c];                                  // Previous output value
            float err = targetProcessed - yPrev;                       // Raw voltage error based on processed target
            int sign = (err > 0.f) - (err < 0.f);                     // Step direction: +1, 0, or -1
            float aerrV = std::fabs(err);                              // Absolute error in volts
            float aerrN = pitchSafeGlide ? hi::dsp::glide::voltsToSemitones(aerrV) : aerrV; // Normalized error
            signArr[c] = sign;                                         // Store direction for strum ordering
            aerrNArr[c] = aerrN;                                       // Store normalized error for strum
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Strum Delay Assignment: Calculate Per-Channel Timing Offsets for Chord Articulation
        // ───────────────────────────────────────────────────────────────────────────────────────────────
        auto assignDelayFor = [&](int ch) {
            if (!(strumEnabled && strumMs > 0.f && polyTrans.curProcN > 1)) { 
                strumDelayAssigned[ch] = 0.f; 
                strumDelayLeft[ch] = 0.f; 
                return; 
            }
            // Convert strum mode to DSP enum
            hi::dsp::strum::Mode mode = (strumMode == 0 ? hi::dsp::strum::Mode::Up : 
                                        (strumMode == 1 ? hi::dsp::strum::Mode::Down : 
                                         hi::dsp::strum::Mode::Random));
            float tmp[16] = {0};                                       // Temporary delay array
            hi::dsp::strum::assign(strumMs, polyTrans.curProcN, mode, tmp); // Calculate strum delays
            strumDelayAssigned[ch] = tmp[ch];                          // Store assigned delay
            strumDelayLeft[ch] = tmp[ch];                              // Initialize remaining delay
        };
        
        // Trigger strum delay assignment when step changes are detected
        if (strumEnabled && strumMs > 0.f && polyTrans.curProcN > 1) {
            for (int c = 0; c < polyTrans.curProcN; ++c) {
                // Assign new delays when: mode changed, target changed, direction flipped, or step jumped
                if (modeChanged || targetChangedArr[c] || signArr[c] != stepSign[c] || aerrNArr[c] > stepNorm[c])
                    assignDelayFor(c);
            }
        }

        // ───────────────────────────────────────────────────────────────────────────────────────────────
        // Pass 2: Apply Slew, Range Processing, and Quantization with Strum Timing
        // ───────────────────────────────────────────────────────────────────────────────────────────────
    for (int c = 0; c < polyTrans.curProcN; ++c) {
            float target = targetArr[c];                               // Get pre-computed target value
            
            // ───────────────────────────────────────────────────────────────────────────────────────────
            // Step Analysis: Recalculate Error and Direction for Current Channel
            // ───────────────────────────────────────────────────────────────────────────────────────────
            float yPrev = lastOut[c];                                  // Previous output value
            float err = target - yPrev;                                // Current error
            float aerrV = std::fabs(err);                              // Absolute error in volts
            float aerrN = pitchSafeGlide ? hi::dsp::glide::voltsToSemitones(aerrV) : aerrV; // Normalized error
            int sign = (err > 0.f) - (err < 0.f);                     // Step direction

            // ───────────────────────────────────────────────────────────────────────────────────────────
            // Slew Rate Calculation: Combine Per-Channel, Global, and Strum Timing
            // ───────────────────────────────────────────────────────────────────────────────────────────
            float sec = hi::ui::ExpTimeQuantity::knobToSec(params[SL_PARAM[c]].getValue()); // Per-channel slew time
            float gsec = gsecAdd;                                      // Global slew addition from dual-mode
            float assignedDelay = (strumEnabled && polyTrans.curProcN > 1) ? strumDelayAssigned[c] : 0.f;
            
            if (strumEnabled && strumType == 0) {
                // Time-stretch mode: add assigned delay to effective glide time
                sec += gsec + assignedDelay;
            } else {
                // Start-delay mode: no time-stretch addition; delay handled separately below
                sec += gsec;
            }
            
            // ───────────────────────────────────────────────────────────────────────────────────────────
            // Slew Processing Gate: Check Time Threshold and Per-Channel Enable State
            // ───────────────────────────────────────────────────────────────────────────────────────────
            bool chSlewDisabled = !slewEnabled[c];                    // Per-channel slew disable flag
            bool noSlew = (sec <= hconst::MIN_SEC) || chSlewDisabled;  // Skip slew if time too short or disabled

            // ───────────────────────────────────────────────────────────────────────────────────────────
            // Step Change Detection: Update Tracking State When New Steps Are Detected
            // ───────────────────────────────────────────────────────────────────────────────────────────
            if (modeChanged || sign != stepSign[c] || aerrN > stepNorm[c]) {
                stepSign[c] = sign;                                    // Update step direction
                stepNorm[c] = std::max(aerrN, hconst::EPS_ERR);        // Update step magnitude (with minimum)
            }

            // ───────────────────────────────────────────────────────────────────────────────────────────
            // Start Delay Processing: Handle Strum Timing in Start-Delay Mode
            // ───────────────────────────────────────────────────────────────────────────────────────────
            float yRaw = target;                                       // Initialize with target value
            bool inStartDelay = (strumEnabled && strumType == 1 && strumDelayLeft[c] > 0.f);
            if (!inStartDelay && !noSlew && quantizerPos == QuantizerPos::Post) {
                // Slew BEFORE the quantizer only when the voice is not being held by start-delay
                float remainingV = aerrV;                              // Distance remaining to target
                float totalJumpV = remainingV;                         // Total jump distance
                float baseRateV = totalJumpV / sec;                    // Base rate: equal-time distance over seconds
                float u = clamp(remainingV / std::max(totalJumpV, hconst::EPS_ERR), 0.f, 1.f); // Progress ratio

                // Apply curve shaping to rise and fall rates
                float rateRise = baseRateV * hi::dsp::glide::shapeMul(u, riseParams, hconst::EPS_ERR);
                float rateFall = baseRateV * hi::dsp::glide::shapeMul(u, fallParams, hconst::EPS_ERR);

                // Update slew processor only when rates change significantly (avoid zipper noise)
                if (std::fabs(rateRise - prevRiseRate[c]) > hconst::RATE_EPS ||
                    std::fabs(rateFall - prevFallRate[c]) > hconst::RATE_EPS) {
                    slews[c].setRiseFall(rateRise, rateFall);          // Configure slew processor
                    prevRiseRate[c] = rateRise;                        // Cache for comparison
                    prevFallRate[c] = rateFall;                        // Cache for comparison
                }
                yRaw = slews[c].process(args.sampleTime, target);      // Apply slew processing
            }

            // ───────────────────────────────────────────────────────────────────────────────────────────
            // Pre-Quantization Processing: Range Limiting and Octave Shifting
            // ───────────────────────────────────────────────────────────────────────────────────────────
            float yPre = preRange(yRaw);                               // Apply pre-quantization range limiting
            // Apply range offset and octave shift before quantizer to shift whole quantization window
            float yBasePre = yPre + rangeOffset + (float)postOctShift[c] * 1.f;

            // ───────────────────────────────────────────────────────────────────────────────────────────
            // Quantizer Position Logic: Pre (Q→S) vs Post (S→Q) Processing Order
            // ───────────────────────────────────────────────────────────────────────────────────────────
            float yFinal = 0.f;
            if (quantizerPos == QuantizerPos::Pre) {
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Pre-Quantization Mode: Legacy Q→S Order (Quantize → Mix Strength → Slew)
                // ───────────────────────────────────────────────────────────────────────────────────────
                float yPreForQ = yBasePre;                             // Input includes range offset & octave shift
                float yRel = yPreForQ - rangeOffset;                   // Remove range offset for quantizer
                float yQRel = yRel;                                    // Will hold quantized relative volts
                
                if (qzEnabled[c]) {
                    // ───────────────────────────────────────────────────────────────────────────────────
                    // Core Quantizer Configuration: Setup Scale and Tuning Parameters
                    // ───────────────────────────────────────────────────────────────────────────────────
                    hi::dsp::QuantConfig qc;
                    if (tuningMode == 0) { 
                        qc.edo = (edo <= 0) ? 12 : edo; 
                        qc.periodOct = 1.f; 
                    } else { 
                        qc.edo = tetSteps > 0 ? tetSteps : 9; 
                        qc.periodOct = (tetPeriodOct > 0.f) ? tetPeriodOct : std::log2(3.f/2.f); 
                    }
                    qc.root = rootNote; 
                    qc.useCustom = useCustomScale; 
                    qc.scaleIndex = scaleIndex; 
                    if (qc.useCustom) { 
                        if ((int)customMaskGeneric.size() == qc.edo) { 
                            qc.customMaskGeneric = customMaskGeneric.data(); 
                            qc.customMaskLen = (int)customMaskGeneric.size(); 
                        } 
                    }
                    
                    // Detect quantizer configuration changes and reset latched state
                    bool cfgChanged = (prevRootNote != rootNote || prevScaleIndex != scaleIndex || 
                                      prevEdo != qc.edo || prevTetSteps != tetSteps || 
                                      prevTetPeriodOct != qc.periodOct || prevTuningMode != tuningMode || 
                                      prevUseCustomScale != useCustomScale); 
                    if (cfgChanged) { 
                        for (int k = 0; k < 16; ++k) latchedInit[k] = false; // Reset all channels
                        prevRootNote = rootNote; prevScaleIndex = scaleIndex; prevEdo = qc.edo; 
                        prevTetSteps = tetSteps; prevTetPeriodOct = qc.periodOct; prevTuningMode = tuningMode; 
                    }
                    
                    // ───────────────────────────────────────────────────────────────────────────────────
                    // Step Calculation and Latched State Initialization
                    // ───────────────────────────────────────────────────────────────────────────────────
                    int N = qc.edo; 
                    float period = qc.periodOct;
                    double fs = (double)yRel * (double)N / (double)period; // Convert voltage to fractional steps
                    
                    if (!latchedInit[c]) {
                        latchedStep[c] = hi::dsp::nearestAllowedStep((int)std::round(fs), (float)fs, qc);
                        lastFs[c] = fs;                                // Seed direction state with current fs
                        lastDir[c] = 0;                                // Start neutral so peaks don't mis-set direction
                        latchedInit[c] = true;
                    }
                    
                    // ───────────────────────────────────────────────────────────────────────────────────
                    // Directional Snap: Hysteresis-Based Quantization with Direction Memory
                    // ───────────────────────────────────────────────────────────────────────────────────
                    int baseStep = (int)std::round(fs);                // Default for non-directional modes
                    int dir = 0;                                       // Direction state for Directional Snap
                    
                    if (quantRoundMode == 0) {                         // Directional Snap mode
                        float Hc = rack::clamp(stickinessCents, 0.f, 20.f); // Hysteresis in cents
                        const float maxAllowed = 0.4f * 1200.f * (period / (float)N); // Maximum reasonable hysteresis
                        if (Hc > maxAllowed) Hc = maxAllowed;          // Clamp to reasonable range
                        float Hs = (Hc * (float)N) / 1200.0f;         // Convert cents to steps
                        float Hd = std::max(0.75f * Hs, 0.02f);       // Widen direction hysteresis slightly
                        double d = fs - lastFs[c];                     // Calculate step delta
                        dir = lastDir[c];                              // Get previous direction
                        if (d > +Hd) dir = +1;                        // Moving up beyond hysteresis
                        else if (d < -Hd) dir = -1;                   // Moving down beyond hysteresis
                        // else: stay in current direction (hysteresis)
                        
                        if (dir > 0)      baseStep = (int)std::ceil(fs);  // Round up when moving up
                        else if (dir < 0) baseStep = (int)std::floor(fs); // Round down when moving down
                        else              baseStep = latchedStep[c];       // Hold candidate at peak/valley
                        lastDir[c] = dir;                              // Update direction state
                        lastFs[c] = fs;                                // Update position state
                    }
                    
                    // Ensure latched step is valid in current scale
                    if (!hi::dsp::isAllowedStep(latchedStep[c], qc)) { 
                        latchedStep[c] = hi::dsp::nearestAllowedStep(latchedStep[c], (float)fs, qc); 
                    }
                    
                    // ───────────────────────────────────────────────────────────────────────────────────
                    // Target Step Selection: Directional vs Standard Quantization
                    // ───────────────────────────────────────────────────────────────────────────────────
                    int targetStep;
                    if (quantRoundMode == 0) {                         // Directional Snap mode
                        int candidate = latchedStep[c];                // Start with current latched step
                        if (dir > 0)      
                            candidate = hi::dsp::nextAllowedStep(latchedStep[c], +1, qc); // Move to next higher allowed step
                        else if (dir < 0) 
                            candidate = hi::dsp::nextAllowedStep(latchedStep[c], -1, qc); // Move to next lower allowed step
                        // dir == 0: hold candidate at current latched step
                        targetStep = candidate;
                    } else {
                        // Standard quantization modes: nearest, up, down
                        targetStep = hi::dsp::nearestAllowedStep(baseStep, (float)fs, qc);
                    }
                    // ───────────────────────────────────────────────────────────────────────────────────
                    // Schmitt Latch Logic: Center-Anchored Hysteresis for Stable Quantization
                    // ───────────────────────────────────────────────────────────────────────────────────
                    float Hc = rack::clamp(stickinessCents, 0.f, 20.f);    // Hysteresis in cents
                    float stepCents = 1200.f * (period / (float)N);        // Cents per step in current tuning
                    const float maxAllowed = 0.4f * stepCents;             // Maximum reasonable hysteresis (40% of step)
                    if (Hc > maxAllowed) Hc = maxAllowed;                  // Clamp to prevent excessive hysteresis
                    float Hs = (Hc * (float)N) / 1200.0f;                 // Convert cents to steps
                    float d = (float)(fs - (double)latchedStep[c]);        // Distance from latched center (in steps)
                    float upThresh = +0.5f + Hs;                           // Upper switching threshold
                    float downThresh = -0.5f - Hs;                         // Lower switching threshold
                    
                    // Switch latched step only when crossing thresholds and only by ±1 step
                    if (targetStep > latchedStep[c] && d > upThresh) 
                        latchedStep[c] = latchedStep[c] + 1;               // Move up one step
                    else if (targetStep < latchedStep[c] && d < downThresh) 
                        latchedStep[c] = latchedStep[c] - 1;               // Move down one step
                    // else: hold at latchedStep[c] (within hysteresis zone)
                    
                    // Convert final latched step back to voltage with scale snapping
                    yQRel = hi::dsp::snapEDO((latchedStep[c] / (float)N) * period, qc, 10.f, false, 0);
                    
                    // ───────────────────────────────────────────────────────────────────────────────────
                    // Advanced Rounding Mode Processing: Directional Nudging and Scale-Aware Selection
                    // ───────────────────────────────────────────────────────────────────────────────────
                    if (quantRoundMode != 1) {
                        // Step-aware rounding: derive the active tuning's volts-per-step so nudges follow the scale grid
                        const float rawStepVolts = (N > 0 && period > 0.f)
                                                     ? (period / static_cast<float>(N))
                                                     : 0.f;
                        const float voltsPerStep = (rawStepVolts > 0.f) ? rawStepVolts : (1.f / 12.f);
                        const float nudgeVolts = voltsPerStep * 0.51f;          // Match legacy 51% bias, but scale by tuning
                        const float stepTolVolts = std::max(1e-5f, voltsPerStep * 1e-3f); // ~0.1% of a step for hysteresis
                        const float stepTolSteps = stepTolVolts / voltsPerStep;
                        const float diffVolts = yRel - yQRel;
                        const float diffSteps = diffVolts / voltsPerStep;
                        float prev = prevYRel[c];
                        float dir = (yRel > prev + 1e-6f) ? 1.f : (yRel < prev - 1e-6f ? -1.f : 0.f);
                        int slopeDir = (dir > 0.f) ? +1 : (dir < 0.f ? -1 : 0);

                        // Map quantization mode to DSP rounding mode
                        hi::dsp::RoundMode rm = (quantRoundMode == 0 ? hi::dsp::RoundMode::Directional :
                                                (quantRoundMode == 2 ? hi::dsp::RoundMode::Ceil :
                                                (quantRoundMode == 3 ? hi::dsp::RoundMode::Floor : hi::dsp::RoundMode::Nearest)));
                        hi::dsp::RoundPolicy rp{rm};
                        (void)hi::dsp::pickRoundingTarget(0, diffSteps, (int)slopeDir, rp);

                        // Apply directional nudging based on rounding mode (step thresholds follow tuning scale)
                        if (rm == hi::dsp::RoundMode::Directional) {
                            if (slopeDir > 0 && diffSteps > 0.f) {
                                float nudged = quantizeToScale(yQRel + nudgeVolts, 0, clipLimit, true);
                                if (nudged > yQRel + stepTolVolts) yQRel = nudged;
                            } else if (slopeDir < 0 && diffSteps < 0.f) {
                                float nudged = quantizeToScale(yQRel - nudgeVolts, 0, clipLimit, true);
                                if (nudged < yQRel - stepTolVolts) yQRel = nudged;
                            }
                        } else if (rm == hi::dsp::RoundMode::Ceil) {
                            if (diffSteps > stepTolSteps) {
                                float nudged = quantizeToScale(yQRel + nudgeVolts, 0, clipLimit, true);
                                if (nudged > yQRel + stepTolVolts) yQRel = nudged;
                            }
                        } else if (rm == hi::dsp::RoundMode::Floor) {
                            if (diffSteps < -stepTolSteps) {
                                float nudged = quantizeToScale(yQRel - nudgeVolts, 0, clipLimit, true);
                                if (nudged < yQRel - stepTolVolts) yQRel = nudged;
                            }
                        }
                        prevYRel[c] = yRel;                                // Update previous value for direction tracking
                    } else { 
                        prevYRel[c] = yRel;                                // Update previous value even when not processing
                    }
                } else { 
                    prevYRel[c] = yRel;                                    // Update previous value when quantizer disabled
                }
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Quantization Strength Blending: Mix Raw and Quantized Signals
                // ───────────────────────────────────────────────────────────────────────────────────────
                float yQAbs = yQRel + rangeOffset;                         // Add range offset back to quantized signal
                float t = clamp(quantStrength, 0.f, 1.f);                  // Quantization strength (0=raw, 1=quantized)
                float yMix = yPreForQ + (yQAbs - yPreForQ) * t;            // Blend raw (yPreForQ) and quantized signals
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Post-Quantization Slew Processing (Pre Mode): Apply Slew AFTER Quantization
                // ───────────────────────────────────────────────────────────────────────────────────────
                float yPost = yMix;                                        // Initialize with blended value
                if (!noSlew && !inStartDelay) {
                    // Calculate shape-aware slew rates targeting the quantized blend
                    float remainingV = std::fabs(yMix - lastOut[c]);       // Distance to blended target
                    float totalJumpV = remainingV;                         // Total jump distance
                    float baseRateV = totalJumpV / sec;                    // Base rate: distance over time
                    float u = clamp(remainingV / std::max(totalJumpV, hconst::EPS_ERR), 0.f, 1.f); // Progress ratio
                    
                    // Apply curve shaping to rise and fall rates
                    float rateRise = baseRateV * hi::dsp::glide::shapeMul(u, riseParams, hconst::EPS_ERR);
                    float rateFall = baseRateV * hi::dsp::glide::shapeMul(u, fallParams, hconst::EPS_ERR);
                    
                    // Update slew processor only when rates change significantly
                    if (std::fabs(rateRise - prevRiseRate[c]) > hconst::RATE_EPS || 
                        std::fabs(rateFall - prevFallRate[c]) > hconst::RATE_EPS) { 
                        slews[c].setRiseFall(rateRise, rateFall); 
                        prevRiseRate[c] = rateRise; 
                        prevFallRate[c] = rateFall; 
                    }
                    yPost = slews[c].process(args.sampleTime, yMix);   // Apply slew to blended signal
                }
                yFinal = yPost;                                            // Set final output for Pre mode
            } else {
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Post-Quantization Mode: S→Q Order (Slew → Quantize for Pitch Stability)
                // ───────────────────────────────────────────────────────────────────────────────────────
                float ySlewed = yPre + rangeOffset + (float)postOctShift[c] * 1.f; // Apply range offset and octave shift
                float yRel = (ySlewed - rangeOffset);                      // Remove range offset for quantizer input
                float yOutQuant = ySlewed;                                 // Will hold final blended result
                
                if (qzEnabled[c]) {
                    // ───────────────────────────────────────────────────────────────────────────────────
                    // Post-Mode Quantizer Logic: Operating on Already-Slewed Signal
                    // ───────────────────────────────────────────────────────────────────────────────────
                    hi::dsp::QuantConfig qc;
                    if (tuningMode == 0) { 
                        qc.edo = (edo <= 0) ? 12 : edo; 
                        qc.periodOct = 1.f; 
                    } else { 
                        qc.edo = tetSteps > 0 ? tetSteps : 9; 
                        qc.periodOct = (tetPeriodOct > 0.f) ? tetPeriodOct : std::log2(3.f/2.f); 
                    }
                    qc.root = rootNote; 
                    qc.useCustom = useCustomScale; 
                    qc.customFollowsRoot = true;                           // Always follow root in Post mode
                    qc.scaleIndex = scaleIndex;
                    if (qc.useCustom) {
                        if ((int)customMaskGeneric.size() == qc.edo) { 
                            qc.customMaskGeneric = customMaskGeneric.data(); 
                            qc.customMaskLen = (int)customMaskGeneric.size(); 
                        }
                    }
                    
                    // Configuration change detection and state management
                    int N = qc.edo; 
                    float period = qc.periodOct; 
                    bool cfgChanged = (prevRootNote != rootNote || prevScaleIndex != scaleIndex || 
                                      prevEdo != qc.edo || prevTetSteps != tetSteps || 
                                      prevTetPeriodOct != qc.periodOct || prevTuningMode != tuningMode || 
                                      prevUseCustomScale != useCustomScale); 
                    if (cfgChanged) { 
                        for (int k = 0; k < 16; ++k) latchedInit[k] = false; // Reset all channels
                        prevRootNote = rootNote; prevScaleIndex = scaleIndex; prevEdo = qc.edo; 
                        prevTetSteps = tetSteps; prevTetPeriodOct = qc.periodOct; prevTuningMode = tuningMode; 
                    }
                    
                    // Step calculation and latched state initialization
                    float fs = yRel * (float)N / period;                   // Convert voltage to fractional steps
                    if (!latchedInit[c]) { 
                        latchedStep[c] = hi::dsp::nearestAllowedStep((int)std::round(fs), fs, qc); 
                        latchedInit[c] = true; 
                    } 
                    if (!hi::dsp::isAllowedStep(latchedStep[c], qc)) { 
                        latchedStep[c] = hi::dsp::nearestAllowedStep(latchedStep[c], fs, qc); 
                    }
                    // Hysteresis-based Schmitt latch for stable quantization
                    float dV = period / (float)N;                          // Voltage per step
                    float stepCents = 1200.f * dV;                         // Cents per step
                    float Hc = rack::clamp(stickinessCents, 0.f, 20.f);    // Clamp hysteresis to reasonable range
                    float maxAllowed = 0.4f * stepCents;                   // Maximum hysteresis (40% of step size)
                    if (Hc > maxAllowed) Hc = maxAllowed;                  // Prevent excessive hysteresis
                    float H_V = Hc / 1200.f;                               // Convert cents to voltage
                    
                    // Calculate adjacent allowed steps for hysteresis boundaries
                    int upStep = hi::dsp::nextAllowedStep(latchedStep[c], +1, qc);
                    int dnStep = hi::dsp::nextAllowedStep(latchedStep[c], -1, qc);
                    float center = (latchedStep[c] / (float)N) * period;   // Current step voltage
                    float vUp = (upStep / (float)N) * period;              // Next step up voltage
                    
                    // Compute hysteresis thresholds around current step
                    hi::dsp::HystSpec hs{ (vUp - center) * 2.f, H_V };
                    auto th = hi::dsp::computeHysteresis(center, hs);
                    float T_up = th.up;                                    // Upper threshold
                    float T_down = th.down;                                // Lower threshold
                    
                    // Apply Schmitt latch logic for step transitions
                    if (yRel >= T_up && upStep != latchedStep[c]) 
                        latchedStep[c] = upStep;
                    else if (yRel <= T_down && dnStep != latchedStep[c]) 
                        latchedStep[c] = dnStep;
                    
                    // Snap to exact quantized voltage for current latched step
                    float yqRel = hi::dsp::snapEDO((latchedStep[c] / (float)N) * period, qc, 10.f, false, 0);
                    // Advanced rounding modes for fine-tuned quantization behavior
                    if (quantRoundMode != 1) { 
                        float rawSemi = yRel * 12.f;                           // Raw signal in semitones
                        float snappedSemi = yqRel * 12.f;                      // Quantized signal in semitones
                        float diff = rawSemi - snappedSemi;                    // Difference for rounding decisions
                        float prev = prevYRel[c];                              // Previous voltage for direction detection
                        float dir = (yRel > prev + 1e-6f) ? 1.f : (yRel < prev - 1e-6f ? -1.f : 0.f);
                        int slopeDir = (dir > 0.f) ? +1 : (dir < 0.f ? -1 : 0); // Direction: +1=up, -1=down, 0=static
                        
                        // Map quantRoundMode to DSP rounding mode
                        hi::dsp::RoundMode rm = (quantRoundMode == 0 ? hi::dsp::RoundMode::Directional : 
                                               (quantRoundMode == 2 ? hi::dsp::RoundMode::Ceil : 
                                               (quantRoundMode == 3 ? hi::dsp::RoundMode::Floor : 
                                                                     hi::dsp::RoundMode::Nearest)));
                        hi::dsp::RoundPolicy rp{rm};
                        float posWithin = diff;
                        (void)hi::dsp::pickRoundingTarget(0, posWithin, slopeDir, rp); 
                        
                        // Scale-aware directional selection (replaces chromatic nudging)
                        if (rm == hi::dsp::RoundMode::Directional && std::fabs(diff) > 1e-5f) { 
                            int targetStep = (slopeDir > 0) ? hi::dsp::nextAllowedStep(latchedStep[c], +1, qc) : 
                                                             hi::dsp::nextAllowedStep(latchedStep[c], -1, qc); 
                            if (targetStep != latchedStep[c]) { 
                                float targetV = (targetStep / (float)N) * period; 
                                yqRel = targetV; 
                            } 
                        } else if (rm == hi::dsp::RoundMode::Ceil && diff > 1e-5f) { 
                            int targetStep = hi::dsp::nextAllowedStep(latchedStep[c], +1, qc); 
                            if (targetStep != latchedStep[c]) { 
                                float targetV = (targetStep / (float)N) * period; 
                                yqRel = targetV; 
                            } 
                        } else if (rm == hi::dsp::RoundMode::Floor && diff < -1e-5f) { 
                            int targetStep = hi::dsp::nextAllowedStep(latchedStep[c], -1, qc); 
                            if (targetStep != latchedStep[c]) { 
                                float targetV = (targetStep / (float)N) * period; 
                                yqRel = targetV; 
                            } 
                        }
                        prevYRel[c] = (ySlewed - rangeOffset);                 // Track pre-quant slew for direction
                    } else {
                        prevYRel[c] = (ySlewed - rangeOffset);                 // Standard nearest mode tracking
                    }
                    // Quantization strength blending (Post mode)
                    float yq = yqRel + rangeOffset;                        // Add range offset back to quantized signal
                    float t = clamp(quantStrength, 0.f, 1.f);              // Clamp blend factor to valid range
                    yOutQuant = ySlewed + (yq - ySlewed) * t;              // Blend: raw slewed + (quantized - raw) * strength
                    // Note: In Post mode, raw signal is ySlewed (already processed through slew)
                } else {
                    prevYRel[c] = (ySlewed - rangeOffset);                 // Track voltage for next frame
                }
                float yPost = yOutQuant;                               // Final quantized output (already slewed)
                yFinal = yPost;                                        // Set as final channel output
            }

            // ───────────────────────────────────────────────────────────────────────────────────────
            // Final Output Processing and Channel State Updates
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Note: Strength crossfade raw source varies by mode:
            // - Pre mode uses yPre (yPreForQ) as raw signal for blending
            // - Post mode uses ySlewed as raw signal for blending
            
            // Apply output voltage limiting with soft or hard clipping
            // Hold the previously latched output while start-delay counts down so the quantizer
            // can still track the incoming gesture (prevents Directional Snap from chasing late).
            float yOut = inStartDelay ? yPrev : yFinal;

            if (softClipOut)
                yOut = hi::dsp::clip::soft(yOut, hconst::MAX_VOLT_CLAMP);  // Smooth saturation curve
            else
                yOut = clamp(yOut, -hconst::MAX_VOLT_CLAMP, hconst::MAX_VOLT_CLAMP); // Hard limiting

            outVals[c] = yOut;                                         // Store final output for this channel
            lastOut[c] = yOut;                                         // Update last output for next frame
            hi::ui::led::setBipolar(lights[CH_LIGHT + 2*c + 0], lights[CH_LIGHT + 2*c + 1], yOut, args.sampleTime);
        }

        // Advance strum countdowns after processing when any voice is still delaying
        if (strumTickNeeded) {
            hi::dsp::strum::tickStartDelays((float)args.sampleTime, polyTrans.curProcN, strumDelayLeft);
        }

    // ───────────────────────────────────────────────────────────────────────────────────────
    // Polyphonic Output Processing with Fade Management
    // ───────────────────────────────────────────────────────────────────────────────────────
    // Apply polyphony transition ramp for pop-free fade during channel count changes
    float ramp = clamp(polyTrans.polyRamp, 0.f, 1.f);                 // Get current fade ramp (0=silent, 1=full)
    
    if (sumToMonoOut) {
        // Sum all active channels to single mono output
        float sum = 0.f;
        for (int c = 0; c < polyTrans.curProcN; ++c) 
            sum += outVals[c];                                         // Accumulate all channel outputs
        if (avgWhenSumming && polyTrans.curProcN > 0) 
            sum /= (float)polyTrans.curProcN;                         // Average instead of sum if enabled
        outputs[OUT_OUTPUT].setVoltage(clamp(sum * ramp, -hconst::MAX_VOLT_CLAMP, hconst::MAX_VOLT_CLAMP), 0);
    } else {
        // Standard polyphonic output - each channel gets its own output
        for (int c = 0; c < polyTrans.curProcN; ++c) 
            outputs[OUT_OUTPUT].setVoltage(outVals[c] * ramp, c);     // Apply fade ramp to each channel
    }

    // ───────────────────────────────────────────────────────────────────────────────────────
    // LED Management and Cleanup
    // ───────────────────────────────────────────────────────────────────────────────────────
    // Clear LEDs for inactive channels to prevent visual artifacts
    for (int c = polyTrans.curProcN; c < 16; ++c) {
        lights[CH_LIGHT + 2*c + 0].setBrightness(0.f);               // Clear positive LED
        lights[CH_LIGHT + 2*c + 1].setBrightness(0.f);               // Clear negative LED
    }

    // ───────────────────────────────────────────────────────────────────────────────────────
    // Polyphony Transition State Machine
    // ───────────────────────────────────────────────────────────────────────────────────────
    // Handle fade phase progression at the end of processing block
    if (polyTrans.transPhase == TRANS_FADE_OUT) {
        if (polyFadeSec <= 0.f) {                                     // Instant fade (no time specified)
            polyTrans.polyRamp = 0.f;                                 // Set ramp to silent immediately
        } else {
            // Gradual fade-out over specified time period
            polyTrans.polyRamp = std::max(0.f, polyTrans.polyRamp - (float)args.sampleTime / polyFadeSec);
        }
        if (polyTrans.polyRamp <= 0.f + 1e-6f) {
            // Fade-out complete - switch to new channel configuration while silent
            polyTrans.curProcN = polyTrans.pendingProcN;              // Update processing channel count
            polyTrans.curOutN  = polyTrans.pendingOutN;               // Update output channel count
            outputs[OUT_OUTPUT].setChannels(polyTrans.curOutN);       // Set VCV Rack output channels
            polyTrans.initToTargetsOnSwitch = true;                   // Flag for target reinitialization
            polyTrans.transPhase = TRANS_FADE_IN;                     // Begin fade-in phase
            // Prepare to fade in with new channel configuration
        }
    } else if (polyTrans.transPhase == TRANS_FADE_IN) {
        // Fade-in phase: reinitialize targets and gradually increase volume
        if (polyTrans.initToTargetsOnSwitch) {
            // Recompute target voltages with current settings to avoid output jumps
            for (int c = 0; c < polyTrans.curProcN; ++c) {
                // Recreate minimal target calculation (input + offsets), matching main processing
                float in = 0.f;
                    // Get input voltage for this channel
                if (inConn) {
                    if (inCh <= 1) 
                        in = inputs[IN_INPUT].getVoltage(0);              // Mono input
                    else if (c < inCh) 
                        in = inputs[IN_INPUT].getVoltage(c);              // Polyphonic input
                }
                
                // Apply global attenuverter/gain if enabled
                if ((params[GLOBAL_SLEW_MODE_PARAM].getValue() > 0.5f) || attenuverterAlwaysOn) {
                    float rawAttv = gSlew.mode ? params[GLOBAL_SLEW_PARAM].getValue() : gSlew.b;
                    rawAttv = rack::math::clamp(rawAttv, 0.f, 1.f);       // Clamp to valid range
                    float gGain2 = -10.f + 20.f * rawAttv;               // Map to -10V to +10V gain
                    in *= gGain2;                                         // Apply gain to input
                }
                
                // Calculate total offset (per-channel + global)
                float offCh = params[OFF_PARAM[c]].getValue();            // Per-channel offset
                float offTot = offCh + (gOffset.mode ? gOffset.a : params[GLOBAL_OFFSET_PARAM].getValue());
                
                // Apply offset quantization if enabled
                int qm = snapOffsetModeCh[c];                             // Offset snap mode for this channel
                if (qm == 1) {                                            // Snap to scale steps
                    int Nsteps = (tuningMode == 0 ? ((edo <= 0) ? 12 : edo) : (tetSteps > 0 ? tetSteps : 9));
                    float period = (tuningMode == 0) ? 1.f : ((tetPeriodOct > 0.f) ? tetPeriodOct : std::log2(3.f/2.f));
                    float stepsPerOct = (float)Nsteps / period;           // Steps per octave
                    offTot = std::round(offTot * stepsPerOct) / stepsPerOct; // Quantize to scale steps
                } else if (qm == 2) {                                     // Snap to cents
                    offTot = std::round(offTot * 1200.f) / 1200.f;        // Quantize to cent boundaries
                }
                
                float target = in + offTot;                               // Calculate target voltage

                // ────────────────────────────────────────────────────────────────────────────────
                // Per-Channel Pre-Range Transform Application
                // ────────────────────────────────────────────────────────────────────────────────
                // Apply per-channel scaling and offset during fade reinitialization
                target = target * preScale[c] + preOffset[c];             // Scale and offset transform

                // Initialize slew processors and output states to current targets
                lastOut[c] = target;                                      // Set last output to target
                slews[c].reset();                                         // Reset slew processor state
                strumPrevTarget[c] = target;                              // Seed strum change detector with current target
                strumPrevInit[c] = true;                                  // Mark detector initialized after poly switch
            }
            polyTrans.initToTargetsOnSwitch = false;                      // Clear reinitialization flag
            polyTrans.polyRamp = 0.f;                                    // Start from silence
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Fade-In Progression
        // ───────────────────────────────────────────────────────────────────────────────────────
        if (polyFadeSec <= 0.f) {                                        // Instant fade-in
            polyTrans.polyRamp = 1.f;                                    // Set ramp to full volume
        } else {
            // Gradual fade-in over specified time period
            polyTrans.polyRamp = std::min(1.f, polyTrans.polyRamp + (float)args.sampleTime / polyFadeSec);
        }
        if (polyTrans.polyRamp >= 1.f - 1e-6f) {
            polyTrans.polyRamp = 1.f;                                     // Ensure exact 1.0 when complete
            polyTrans.transPhase = TRANS_STABLE;                          // Transition to stable state
        }
    } else {
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Stable State - No Polyphony Transition Active
        // ───────────────────────────────────────────────────────────────────────────────────────
        polyTrans.polyRamp = 1.f;                                        // Keep ramp at full volume
    }

    // ───────────────────────────────────────────────────────────────────────────────────────
    // Process Method State Preservation
    // ───────────────────────────────────────────────────────────────────────────────────────
    prevPitchSafeGlide = pitchSafeGlide;                                 // Remember mode for next frame
    }
};

// ═════════════════════════════════════════════════════════════════════════════════════════
// CoreState Glue Helper Definitions
// ═════════════════════════════════════════════════════════════════════════════════════════
// These functions provide centralized serialization/deserialization logic for the DSP core
// state, mirroring the previous JSON read/write logic exactly with no behavior changes.
// This abstraction allows the core DSP state to be saved/restored independently of the
// full module state, enabling features like presets and state snapshots.
// ═════════════════════════════════════════════════════════════════════════════════════════

/**
 * @brief Copies quantization and tuning state from module to CoreState structure
 * @param m Source PolyQuanta module instance (read-only)
 * @param cs Destination CoreState structure to populate
 * @note This function extracts only the core DSP parameters, not UI or randomization state
 */
static void fillCoreStateFromModule(const PolyQuanta& m, hi::dsp::CoreState& cs) noexcept {
    // ───────────────────────────────────────────────────────────────────────────────────────
    // Quantization Parameters
    // ───────────────────────────────────────────────────────────────────────────────────────
    cs.quantStrength = m.quantStrength;                               // Blend factor (0=bypass, 1=full)
    cs.quantRoundMode = m.quantRoundMode;                             // Rounding mode (nearest/directional/etc)
    cs.stickinessCents = m.stickinessCents;                           // Hysteresis amount in cents
    
    // ───────────────────────────────────────────────────────────────────────────────────────
    // Tuning System Configuration
    // ───────────────────────────────────────────────────────────────────────────────────────
    cs.edo = m.edo;                                                   // Equal Division of Octave steps
    cs.tuningMode = m.tuningMode;                                     // 0=EDO, 1=TET (Tetrachordal)
    cs.tetSteps = m.tetSteps;                                         // Steps in tetrachordal period
    cs.tetPeriodOct = m.tetPeriodOct;                                 // Tetrachordal period in octaves
    
    // ───────────────────────────────────────────────────────────────────────────────────────
    // Custom Scale Configuration
    // ───────────────────────────────────────────────────────────────────────────────────────
    cs.useCustomScale = m.useCustomScale;                            // Enable custom scale mode
    cs.customMaskGeneric = m.customMaskGeneric;                      // Custom scale mask for all EDOs
    
    // ───────────────────────────────────────────────────────────────────────────────────────
    // Per-Channel Quantization Settings
    // ───────────────────────────────────────────────────────────────────────────────────────
    for (int i = 0; i < 16; ++i) { 
        cs.qzEnabled[i] = m.qzEnabled[i];                             // Per-channel quantize enable
        cs.postOctShift[i] = m.postOctShift[i];                      // Per-channel octave shift
    }
    
    // ───────────────────────────────────────────────────────────────────────────────────────
    // Scale and Root Note Selection
    // ───────────────────────────────────────────────────────────────────────────────────────
    cs.rootNote = m.rootNote;                                        // Root note (0-11, C-B)
    cs.scaleIndex = m.scaleIndex;                                     // Preset scale index
}
/**
 * @brief Applies CoreState configuration to module instance
 * @param cs Source CoreState structure (read-only)
 * @param m Destination PolyQuanta module to configure
 * @note This function restores core DSP parameters from saved state
 */
static void applyCoreStateToModule(const hi::dsp::CoreState& cs, PolyQuanta& m) noexcept {
    // ───────────────────────────────────────────────────────────────────────────────────────
    // Quantization Parameters Restoration
    // ───────────────────────────────────────────────────────────────────────────────────────
    m.quantStrength = cs.quantStrength;                               // Restore blend factor
    m.quantRoundMode = cs.quantRoundMode;                             // Restore rounding mode
    m.stickinessCents = cs.stickinessCents;                           // Restore hysteresis amount
    
    // ───────────────────────────────────────────────────────────────────────────────────────
    // Tuning System Configuration Restoration
    // ───────────────────────────────────────────────────────────────────────────────────────
    m.edo = cs.edo;                                                   // Restore EDO steps
    m.tuningMode = cs.tuningMode;                                     // Restore tuning mode
    m.tetSteps = cs.tetSteps;                                         // Restore tetrachordal steps
    m.tetPeriodOct = cs.tetPeriodOct;                                 // Restore tetrachordal period
    
    // ───────────────────────────────────────────────────────────────────────────────────────
    // Custom Scale Configuration Restoration
    // ───────────────────────────────────────────────────────────────────────────────────────
    m.useCustomScale = cs.useCustomScale;                            // Restore custom scale mode
    m.customMaskGeneric = cs.customMaskGeneric;                      // Restore custom scale mask (vector copy)
    
    // ───────────────────────────────────────────────────────────────────────────────────────
    // Per-Channel Settings Restoration
    // ───────────────────────────────────────────────────────────────────────────────────────
    for (int i = 0; i < 16; ++i) { 
        m.qzEnabled[i] = cs.qzEnabled[i];                             // Restore per-channel quantize enable
        m.postOctShift[i] = cs.postOctShift[i];                      // Restore per-channel octave shift
    }
    
    // ───────────────────────────────────────────────────────────────────────────────────────
    // Scale and Root Note Restoration
    // ───────────────────────────────────────────────────────────────────────────────────────
    m.rootNote = cs.rootNote;                                        // Restore root note
    m.scaleIndex = cs.scaleIndex;                                     // Restore preset scale index
}

// ═════════════════════════════════════════════════════════════════════════════════════════
// Parameter Index Mapping Arrays
// ═════════════════════════════════════════════════════════════════════════════════════════
// These arrays provide convenient mapping from compact indices (0-15) to the generated
// parameter enum IDs, enabling loop-based access to per-channel controls without hardcoding
// individual parameter names throughout the codebase.
// ═════════════════════════════════════════════════════════════════════════════════════════

/**
 * @brief Maps channel indices (0-15) to slew parameter enum IDs
 * @note Required as out-of-class definition for C++11 compliance
 */
const int PolyQuanta::SL_PARAM[16] = {
    PolyQuanta::SL1_PARAM,  PolyQuanta::SL2_PARAM,                   // Channels 1-2
    PolyQuanta::SL3_PARAM,  PolyQuanta::SL4_PARAM,                   // Channels 3-4
    PolyQuanta::SL5_PARAM,  PolyQuanta::SL6_PARAM,                   // Channels 5-6
    PolyQuanta::SL7_PARAM,  PolyQuanta::SL8_PARAM,                   // Channels 7-8
    PolyQuanta::SL9_PARAM,  PolyQuanta::SL10_PARAM,                  // Channels 9-10
    PolyQuanta::SL11_PARAM, PolyQuanta::SL12_PARAM,                  // Channels 11-12
    PolyQuanta::SL13_PARAM, PolyQuanta::SL14_PARAM,                  // Channels 13-14
    PolyQuanta::SL15_PARAM, PolyQuanta::SL16_PARAM                   // Channels 15-16
};

/**
 * @brief Maps channel indices (0-15) to offset parameter enum IDs
 * @note Required as out-of-class definition for C++11 compliance
 */
const int PolyQuanta::OFF_PARAM[16] = {
    PolyQuanta::OFF1_PARAM,  PolyQuanta::OFF2_PARAM,                 // Channels 1-2
    PolyQuanta::OFF3_PARAM,  PolyQuanta::OFF4_PARAM,                 // Channels 3-4
    PolyQuanta::OFF5_PARAM,  PolyQuanta::OFF6_PARAM,                 // Channels 5-6
    PolyQuanta::OFF7_PARAM,  PolyQuanta::OFF8_PARAM,                 // Channels 7-8
    PolyQuanta::OFF9_PARAM,  PolyQuanta::OFF10_PARAM,                // Channels 9-10
    PolyQuanta::OFF11_PARAM, PolyQuanta::OFF12_PARAM,                // Channels 11-12
    PolyQuanta::OFF13_PARAM, PolyQuanta::OFF14_PARAM,                // Channels 13-14
    PolyQuanta::OFF15_PARAM, PolyQuanta::OFF16_PARAM                 // Channels 15-16
};

/**
 * @brief Maps channel indices (0-15) to legacy quantize parameter enum IDs
 * @note Required as out-of-class definition for C++11 compliance
 * @deprecated These parameters are maintained for backward compatibility only
 */
const int PolyQuanta::QZ_PARAM[16] = {
    PolyQuanta::QZ1_PARAM,  PolyQuanta::QZ2_PARAM,                   // Channels 1-2 (legacy)
    PolyQuanta::QZ3_PARAM,  PolyQuanta::QZ4_PARAM,                   // Channels 3-4 (legacy)
    PolyQuanta::QZ5_PARAM,  PolyQuanta::QZ6_PARAM,                   // Channels 5-6 (legacy)
    PolyQuanta::QZ7_PARAM,  PolyQuanta::QZ8_PARAM,                   // Channels 7-8 (legacy)
    PolyQuanta::QZ9_PARAM,  PolyQuanta::QZ10_PARAM,                  // Channels 9-10 (legacy)
    PolyQuanta::QZ11_PARAM, PolyQuanta::QZ12_PARAM,                  // Channels 11-12 (legacy)
    PolyQuanta::QZ13_PARAM, PolyQuanta::QZ14_PARAM,                  // Channels 13-14 (legacy)
    PolyQuanta::QZ15_PARAM, PolyQuanta::QZ16_PARAM                   // Channels 15-16 (legacy)
};

// ═════════════════════════════════════════════════════════════════════════════════════════
// MOS (Moment of Symmetry) Helper Functions
// ═════════════════════════════════════════════════════════════════════════════════════════
// These functions implement algorithms for detecting and generating Moment of Symmetry scales,
// which are mathematical structures that create musically coherent scales from cyclic patterns.
// MOS scales are generated by taking every g-th step in an N-step cycle, where g and N are
// coprime, creating scales with optimal harmonic properties.
// ═════════════════════════════════════════════════════════════════════════════════════════

namespace hi { namespace music { namespace mos {
    /**
     * @brief Builds a custom scale mask from a pitch class cycle
     * @param mod Target PolyQuanta module to configure
     * @param N Number of steps in the tuning system (EDO)
     * @param pcs Vector of pitch classes to include in the scale
     * @note Updates the appropriate custom mask (12-EDO, 24-EDO, or generic) based on N.
     */
    void buildMaskFromCycle(PolyQuanta* mod, int N, const std::vector<int>& pcs){
        if(!mod || N <= 0) return;                                    // Validate inputs
        
        // Initialize mask for all EDO sizes using unified vector system
        mod->customMaskGeneric.assign(N, 0);                          // Clear mask array
        
        // Set bits for each pitch class in the scale
        for(int p : pcs){
            if(p < 0 || p >= N) continue;                             // Skip invalid pitch classes
            int bit = p;                                              // Root-relative pitch class
            // Set the bit in the unified vector mask
            if((int)mod->customMaskGeneric.size() != N) 
                mod->customMaskGeneric.assign(N, 0);                      // Ensure correct size
            mod->customMaskGeneric[(size_t)bit] = 1;                      // Set bit in mask
        }
    }
    
    /**
     * @brief Detects if the current custom scale is a MOS (Moment of Symmetry) pattern
     * @param mod Source PolyQuanta module to analyze
     * @param mOut Output parameter for number of scale steps (if MOS detected)
     * @param gOut Output parameter for generator step size (if MOS detected)
     * @return true if current scale is a valid MOS pattern, false otherwise
     * @note Uses cached results when possible to avoid expensive recalculation
     */
    bool detectCurrentMOS(PolyQuanta* mod, int& mOut, int& gOut){
        if(!mod) return false;                                            // Validate input
        // Determine tuning system parameters
        int N = (mod->tuningMode == 0 ? mod->edo : mod->tetSteps);        // Get division count
        if(N < 2 || N > 24) { 
            mod->mosCache.valid = false; 
            return false;                                                 // Invalid range for MOS detection
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Cache Management for Performance Optimization
        // ───────────────────────────────────────────────────────────────────────────────────────
        uint64_t h = mod->hashMask(N);                                   // Generate hash of current mask
        bool keyMatch = mod->mosCache.valid &&
            mod->mosCache.N == N &&
            mod->mosCache.tuningMode == mod->tuningMode &&
            mod->mosCache.edo == mod->edo &&
            mod->mosCache.tetSteps == mod->tetSteps &&
            mod->mosCache.rootNote == mod->rootNote &&
            mod->mosCache.useCustom == mod->useCustomScale &&
            mod->mosCache.maskHash == h;
        
        if(keyMatch){
            // Return cached result if parameters haven't changed
            if(mod->mosCache.found){ 
                mOut = mod->mosCache.m; 
                gOut = mod->mosCache.g; 
            }
            return mod->mosCache.found;
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Cache Update - Store Current Parameters
        // ───────────────────────────────────────────────────────────────────────────────────────
        mod->mosCache.valid = true;                                      // Mark cache as valid
        mod->mosCache.N = N;                                             // Store division count
        mod->mosCache.tuningMode = mod->tuningMode;                      // Store tuning mode
        mod->mosCache.edo = mod->edo;                                    // Store EDO value
        mod->mosCache.tetSteps = mod->tetSteps;                          // Store TET steps
        mod->mosCache.rootNote = mod->rootNote;                          // Store root note
        mod->mosCache.useCustom = mod->useCustomScale;                   // Store custom scale flag
        mod->mosCache.maskHash = h;                                      // Store mask hash
        mod->mosCache.found = false;                                     // Initialize as not found
        mod->mosCache.m = 0; 
        mod->mosCache.g = 0;                                             // Initialize MOS parameters
        if(!mod->useCustomScale){ return false; }                       // Only analyze custom scales
        
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Pitch Class Collection and Validation
        // ───────────────────────────────────────────────────────────────────────────────────────
        std::vector<int> pcs;                                            // Pitch class collection
        pcs.reserve(32);                                                 // Reserve space for efficiency
        
        // Extract pitch classes from appropriate mask based on tuning system
        // Extract pitch classes from unified vector mask
        if((int)mod->customMaskGeneric.size() != N) return false;        // Validate mask size
        for(int i = 0; i < N; ++i) 
            if(mod->customMaskGeneric[(size_t)i]) pcs.push_back(i);      // Extract from mask
        
        // Validate pitch class count (must be reasonable for MOS detection)
        if(pcs.size() < 2 || pcs.size() > 24) return false;
        
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Scale Normalization for MOS Analysis
        // ───────────────────────────────────────────────────────────────────────────────────────
        int rotateBy = 0;                                                // Root-relative scales need no rotation

        // Apply rotation (no-op) and clean up duplicates
        for(int& p : pcs){ p = (p + rotateBy) % N; }                    // Rotate each pitch class

        std::sort(pcs.begin(), pcs.end());                              // Sort for analysis
        pcs.erase(std::unique(pcs.begin(), pcs.end()), pcs.end());     // Remove duplicates
        if(pcs.size() < 2 || pcs.size() > 24) return false;            // Revalidate after cleanup
        
        // ───────────────────────────────────────────────────────────────────────────────────────
        // MOS Pattern Detection Algorithm
        // ───────────────────────────────────────────────────────────────────────────────────────
        int m = (int)pcs.size();                                         // Number of scale steps
        
        // Test all possible coprime generators for MOS pattern
        for(int g = 1; g < N; ++g){
            if(gcdInt(g, N) != 1) continue;                              // Skip non-coprime generators
            auto cyc = generateCycle(N, g, m);                           // Generate cycle with this generator
            if((int)cyc.size() != m) continue;                           // Skip if wrong size
            if(cyc == pcs){
                // Found matching MOS pattern - cache and return results
                mod->mosCache.found = true; 
                mod->mosCache.m = m; 
                mod->mosCache.g = g;
                mOut = m; 
                gOut = g; 
                return true;
            }
        }
        // No MOS pattern found - return false (cache already updated)
        return false;
    }
} } }

// Scale detection function implementations
namespace hi { namespace music { namespace scale {
    /**
     * @brief Check if two scale masks are equivalent (accounting for root note rotation)
     * @param mask1 First scale mask to compare
     * @param mask2 Second scale mask to compare
     * @return true if masks represent the same scale (possibly rotated), false otherwise
     */
    bool masksEqual(const std::vector<uint8_t>& mask1, const std::vector<uint8_t>& mask2) {
        if (mask1.size() != mask2.size()) return false;
        if (mask1.empty()) return true;
        
        int N = (int)mask1.size();
        
        // Normalize both masks to canonical form (root at position 0)
        auto normalizeMask = [N](const std::vector<uint8_t>& mask) -> std::vector<uint8_t> {
            std::vector<uint8_t> normalized(N, 0);
            
            // Find the first active note (root) in the mask
            int rootOffset = 0;
            for (int i = 0; i < N; ++i) {
                if (mask[i] != 0) {
                    rootOffset = i;
                    break;
                }
            }
            
            // Rotate the mask so that the root is at position 0
            for (int i = 0; i < N; ++i) {
                int sourceIndex = (i + rootOffset) % N;
                normalized[i] = mask[sourceIndex];
            }
            
            return normalized;
        };
        
        std::vector<uint8_t> norm1 = normalizeMask(mask1);
        std::vector<uint8_t> norm2 = normalizeMask(mask2);
        
        // Compare normalized masks
        for (int i = 0; i < N; ++i) {
            if (norm1[i] != norm2[i]) {
                return false;
            }
        }
        
        return true;
    }
    
    /**
     * @brief Detect if a custom scale matches any predefined scale for the given EDO
     * @param customMask The custom scale mask to check
     * @param edo The EDO system (1-120)
     * @return Pointer to matching scale, or nullptr if no match found
     */
    const Scale* detectMatchingScale(const std::vector<uint8_t>& customMask, int edo) {
        if (customMask.empty() || edo < 1 || edo > 120) return nullptr;
        
        // Get the predefined scales for this EDO
        const Scale* scales = scalesEDO(edo);
        int numScales = numScalesEDO(edo);
        
        if (!scales || numScales <= 0) return nullptr;
        
        // Check each predefined scale for a match
        for (int i = 0; i < numScales; ++i) {
            if (masksEqual(customMask, scales[i].mask)) {
                return &scales[i];
            }
        }
        
        return nullptr;
    }
} } }


// ═════════════════════════════════════════════════════════════════════════════════════════
// ModuleWidget - User Interface Layout and Interaction
// ═════════════════════════════════════════════════════════════════════════════════════════
// This section implements the complete visual interface for PolyQuanta, including:
//
// PANEL LAYOUT SYSTEM:
// - Millimeter-based coordinate system with automatic HP scaling
// - Panel center anchoring for consistent layout across different widths
// - Corner screw mounting hardware
//
// GLOBAL CONTROLS (TOP SECTION):
// - Rise/Fall shape controls for slew curve modification
// - Global Slew and Global Offset knobs with dual-mode toggle switches
// - Symmetrical positioning from panel center for visual balance
//
// CUSTOM WIDGET CLASSES:
// - LockableTrimpot: Enhanced Trimpot with comprehensive context menus including:
//   * Global parameter controls (bulk operations, dual-mode settings)
//   * Per-channel controls (scale/offset sliders, snap modes, quantization)
//   * Randomization settings (scope-based lock/allow toggles)
//   * Batch operations (all/odd/even enable/disable patterns)
// - CentsDisplay: Real-time voltage readout in cents relative to 0V = middle C
//
// PER-CHANNEL CONTROL GRID (8×2 LAYOUT):
// - 16 channels arranged in 8 rows × 2 columns for optimal density
// - Left side: Odd channels (1,3,5,7,9,11,13,15) with slew controls and LED
// - Right side: Even channels (2,4,6,8,10,12,14,16) with offset controls and LED
// - Bipolar LED indicators (green/red for positive/negative voltage)
// - Real-time cents displays showing output voltage as musical intervals
// - Symmetric layout: [Slew][Slew][Cents][LED] | [LED][Cents][Offset][Offset]
//
// BOTTOM SECTION I/O:
// - Main polyphonic input/output jacks (±24V range)
// - Manual randomization button and trigger input (center position)
// - Auto-randomization controls (time/amount knobs, auto/sync switches)
// - Module-conditional rendering (auto-randomization only when not in browser)
//
// CONTEXT MENU INTEGRATION:
// - Comprehensive per-parameter menus with parameter-specific options
// - Global vs per-channel control distinction
// - Randomization scope and lock/allow system
// - Batch operations for efficient multi-channel configuration
// ═════════════════════════════════════════════════════════════════════════════════════════

/**
 * @brief Main UI widget class for PolyQuanta module
 * @note Creates and positions all visual components including knobs, LEDs, jacks, and buttons
 */
struct PolyQuantaWidget : ModuleWidget {
    /**
     * @brief Constructor - builds the complete user interface layout
     * @param module Pointer to the PolyQuanta module instance (can be null for library browser)
     */
	PolyQuantaWidget(PolyQuanta* module) {
		setModule(module);                                               // Associate with module instance
		setPanel(createPanel(asset::plugin(pluginInstance, "res/PolyQuanta.svg"))); // Load panel graphics

        // ───────────────────────────────────────────────────────────────────────────────────────
        // Panel Hardware - Corner Screws
        // ───────────────────────────────────────────────────────────────────────────────────────
        addChild(createWidget<ScrewBlack>(Vec(0, 0)));                                      // Top-left
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));           // Top-right
        addChild(createWidget<ScrewBlack>(Vec(0, box.size.y - RACK_GRID_WIDTH)));           // Bottom-left
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, box.size.y - RACK_GRID_WIDTH))); // Bottom-right

        // ───────────────────────────────────────────────────────────────────────────────────────
        // UI Layout Coordinate System and Placement Constants
        // ───────────────────────────────────────────────────────────────────────────────────────
        // All component positions are defined in millimeters for consistency with panel SVGs.
        // The coordinate system is centered horizontally (cxMM) and scales automatically with
        // panel width changes, requiring no code edits when changing HP size.
        // ───────────────────────────────────────────────────────────────────────────────────────
        
        // Coordinate system conversion (1 HP = 5.08 mm in Eurorack standard)
        const float pxPerMM = RACK_GRID_WIDTH / 5.08f;                          // Pixels per millimeter conversion
        const float cxMM = (box.size.x * 0.5f) / pxPerMM;                       // Panel center X in millimeters
        
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Global Control Rows - Top Section Layout
        // ───────────────────────────────────────────────────────────────────────────────────────
        const float yShapeMM = 16.0f;                                           // Rise/Fall shape controls Y position
        const float yGlobalMM = 27.8f;                                          // Global Slew/Offset controls Y position
        
        // Horizontal positioning for global controls (separated for independent adjustment)
        const float dxColShapesMM = 20.2f;                                      // Rise/Fall shape column offset from center
        const float dxColGlobalsMM = 20.2f;                                     // Global Slew/Offset column offset from center
        const float dxToggleMM = 9.0f;                                          // Mode toggle switch inset from global knobs
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Per-Channel Control Grid - 8×2 Layout (16 channels total)
        // ───────────────────────────────────────────────────────────────────────────────────────
        const float yRow0MM = 41.308f;                                          // First row Y position
        const float rowDyMM = 8.252f;                                           // Vertical spacing between rows
        const float ledDxMM = 1.2f;                                             // LED horizontal offset from row center
        const float knobDx1MM = 19.0f;                                          // Inner knob offset from LED center
        const float knobDx2MM = 28.0f;                                          // Outer knob offset from LED center
        
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Per-Channel Text Readouts - Cents Display Positioning
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Horizontal offsets are measured from panel center (cxMM) and include LED inset plus
        // additional spacing to clear the LED graphics. Vertical offset is relative to row centerline.
        const float dxCentsLeftMM = -(ledDxMM + 8.5f);                         // Left column cents text X offset
        const float dxCentsRightMM = (ledDxMM + 8.5f);                         // Right column cents text X offset
        const float dyCentsMM = 0.0f;                                           // Cents text Y offset from row center
        
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Bottom Section - I/O Jacks and Control Buttons
        // ───────────────────────────────────────────────────────────────────────────────────────
        const float yInOutMM = 122.000f;                                        // Main IN/OUT jacks Y position
        const float yTrigMM = 122.000f;                                         // Randomize trigger jack Y position
        const float yBtnMM = 106.000f;                                          // Randomize button Y position
        const float dxPortsMM = 25.000f;                                        // Horizontal offset for IN/OUT jacks
        
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Auto-Randomization Controls - Clustered Around Main Randomize Button
        // ───────────────────────────────────────────────────────────────────────────────────────
        const float yRndKnobMM = 114.0f;                                        // Time & Amount knobs Y position
        const float dxRndKnobMM = 10.0f;                                        // Horizontal offset for randomize knobs
        const float yRndSwMM = 122.0f;                                          // Auto/Sync switches Y position
        const float dxRndSwMM = 10.0f;                                          // Horizontal offset for randomize switches

        // ───────────────────────────────────────────────────────────────────────────────────────
        // Custom Widget Classes - Enhanced UI Components
        // ───────────────────────────────────────────────────────────────────────────────────────
        
        /**
         * @brief Enhanced Trimpot with per-control randomization settings in context menu
         * @note Extends standard Trimpot to add randomization lock/unlock and per-channel features
         */
        struct LockableTrimpot : Trimpot {
            /**
             * @brief Extends context menu with randomization and channel-specific controls
             * @param menu Pointer to the context menu being built
             */
            void appendContextMenu(Menu* menu) override {
                Trimpot::appendContextMenu(menu);                               // Add standard menu items first
                
                // Validate parameter and module references
                auto* pq = getParamQuantity();
                if (!pq || !pq->module) return;                                 // No parameter or module
                auto* m = dynamic_cast<PolyQuanta*>(pq->module);
                if (!m) return;                                                 // Wrong module type
                
                int pid = pq->paramId;                                          // Get parameter ID for identification
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Parameter Classification and Context Menu State Variables
                // ───────────────────────────────────────────────────────────────────────────────────────
                bool* lockPtr = nullptr;                                        // Pointer to randomization lock flag
                bool* allowPtr = nullptr;                                       // Pointer to randomization allow flag
                bool isSlew = false, isOffset = false, isRise = false, isFall = false; // Parameter type flags
                int chIndex = -1;                                               // Channel index for per-channel features
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Global Slew Parameter Context Menu
                // ───────────────────────────────────────────────────────────────────────────────────────
                if (pid == PolyQuanta::GLOBAL_SLEW_PARAM) {
                    menu->addChild(new MenuSeparator);
                    menu->addChild(rack::createMenuLabel("Controls"));
                    
                    // Bulk reset all per-channel slew values to zero
                    menu->addChild(rack::createMenuItem("Set all slews to 0", "", [m]{
                        for (int i = 0; i < 16; ++i) 
                            m->params[PolyQuanta::SL_PARAM[i]].setValue(0.f);
                    }));
                    
                    menu->addChild(new MenuSeparator);
                    menu->addChild(rack::createMenuLabel("Dual-mode: Global Slew"));
                    
                    // Global slew behavior toggles
                    menu->addChild(rack::createBoolMenuItem("Attenuverter always on", "", 
                        [m]{ return m->attenuverterAlwaysOn; }, 
                        [m](bool v){ m->attenuverterAlwaysOn = v; }));
                    menu->addChild(rack::createBoolMenuItem("Global slew always on", "", 
                        [m]{ return m->slewAddAlwaysOn; }, 
                        [m](bool v){ m->slewAddAlwaysOn = v; }));
                    
                    // Batch operations for per-channel slew enable/disable
                    menu->addChild(new MenuSeparator);
                    menu->addChild(rack::createSubmenuItem("Batch: Slew enable/disable", "", [m](rack::ui::Menu* sm){
                        // Enable operations
                        sm->addChild(rack::createMenuItem("All ON", "", 
                            [m]{ for(int i = 0; i < 16; ++i) m->slewEnabled[i] = true; }));
                        sm->addChild(rack::createMenuItem("Odd ON", "", 
                            [m]{ for(int i = 0; i < 16; i += 2) m->slewEnabled[i] = true; }));
                        sm->addChild(rack::createMenuItem("Even ON", "", 
                            [m]{ for(int i = 1; i < 16; i += 2) m->slewEnabled[i] = true; }));
                        sm->addChild(new MenuSeparator);
                        // Disable operations
                        sm->addChild(rack::createMenuItem("All OFF", "", 
                            [m]{ for(int i = 0; i < 16; ++i) m->slewEnabled[i] = false; }));
                        sm->addChild(rack::createMenuItem("Odd OFF", "", 
                            [m]{ for(int i = 0; i < 16; i += 2) m->slewEnabled[i] = false; }));
                        sm->addChild(rack::createMenuItem("Even OFF", "", 
                            [m]{ for(int i = 1; i < 16; i += 2) m->slewEnabled[i] = false; }));
                    }));
                }
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Global Offset Parameter Context Menu
                // ───────────────────────────────────────────────────────────────────────────────────────
                if (pid == PolyQuanta::GLOBAL_OFFSET_PARAM) {
                    menu->addChild(new MenuSeparator);
                    menu->addChild(rack::createMenuLabel("Controls"));
                    
                    // Batch apply offset quantization mode to all channels
                    menu->addChild(rack::createSubmenuItem("Batch: Offset knob snap mode", "", [m](rack::ui::Menu* sm){
                        auto applyAll = [m](int mode){ 
                            m->snapOffsetMode = mode; 
                            for(int i = 0; i < 16; ++i) m->snapOffsetModeCh[i] = mode; 
                        };
                        sm->addChild(rack::createCheckMenuItem("Voltage (±10 V)", "", 
                            [m]{ return m->snapOffsetMode == 0; }, 
                            [applyAll]{ applyAll(0); }));
                        sm->addChild(rack::createCheckMenuItem("Semitones (EDO/TET accurate)", "", 
                            [m]{ return m->snapOffsetMode == 1; }, 
                            [applyAll]{ applyAll(1); }));
                        sm->addChild(rack::createCheckMenuItem("Cents (1/1200 V)", "", 
                            [m]{ return m->snapOffsetMode == 2; }, 
                            [applyAll]{ applyAll(2); }));
                    }));
                    
                    // Bulk reset all per-channel offset values to zero
                    menu->addChild(rack::createMenuItem("Set all offsets to 0", "", [m]{
                        for (int i = 0; i < 16; ++i) 
                            m->params[PolyQuanta::OFF_PARAM[i]].setValue(0.f);
                    }));
                    menu->addChild(new MenuSeparator);
                    menu->addChild(rack::createMenuLabel("Quantization"));
                    
                    // Batch operations for per-channel quantization enable/disable
                    menu->addChild(rack::createSubmenuItem("Batch: Quantize to scale", "", [m](rack::ui::Menu* sm){
                        sm->addChild(rack::createMenuItem("All ON", "", 
                            [m]{ for (int i = 0; i < 16; ++i) m->qzEnabled[i] = true; }));
                        sm->addChild(rack::createMenuItem("Even ON", "", 
                            [m]{ for (int i = 0; i < 16; ++i) if ((i % 2) == 1) m->qzEnabled[i] = true; }));
                        sm->addChild(rack::createMenuItem("Odd ON", "", 
                            [m]{ for (int i = 0; i < 16; ++i) if ((i % 2) == 0) m->qzEnabled[i] = true; }));
                        sm->addChild(new MenuSeparator);
                        sm->addChild(rack::createMenuItem("All OFF", "", 
                            [m]{ for (int i = 0; i < 16; ++i) m->qzEnabled[i] = false; }));
                        sm->addChild(rack::createMenuItem("Even OFF", "", 
                            [m]{ for (int i = 0; i < 16; ++i) if ((i % 2) == 1) m->qzEnabled[i] = false; }));
                        sm->addChild(rack::createMenuItem("Odd OFF", "", 
                            [m]{ for (int i = 0; i < 16; ++i) if ((i % 2) == 0) m->qzEnabled[i] = false; }));
                    }));
                    
                    // Bulk reset all per-channel octave shifts
                    menu->addChild(rack::createMenuItem("Reset all oct shifts", "", 
                        [m]{ for (int i = 0; i < 16; ++i) m->postOctShift[i] = 0; }));
                    
                    menu->addChild(new MenuSeparator);
                    menu->addChild(rack::createMenuLabel("Dual-mode: Global Offset"));
                    
                    // Global offset behavior toggles
                    menu->addChild(rack::createBoolMenuItem("Global offset always on", "", 
                        [m]{ return m->globalOffsetAlwaysOn; }, 
                        [m](bool v){ m->globalOffsetAlwaysOn = v; }));
                    menu->addChild(rack::createBoolMenuItem("Range offset always on", "", 
                        [m]{ return m->rangeOffsetAlwaysOn; }, 
                        [m](bool v){ m->rangeOffsetAlwaysOn = v; }));
                }
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Per-Channel Parameter Detection and Menu Setup
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Identify which per-channel parameter this is and set up appropriate menu options
                for (int i = 0; i < 16; ++i) {
                    if (pid == PolyQuanta::SL_PARAM[i]) { 
                        lockPtr = &m->lockSlew[i]; 
                        allowPtr = &m->allowSlew[i]; 
                        isSlew = true; 
                        chIndex = i; 
                        break; 
                    }
                    if (pid == PolyQuanta::OFF_PARAM[i]) { 
                        lockPtr = &m->lockOffset[i]; 
                        allowPtr = &m->allowOffset[i]; 
                        isOffset = true; 
                        chIndex = i; 
                        break; 
                    }
                }
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Per-Channel Slew Parameter Context Menu
                // ───────────────────────────────────────────────────────────────────────────────────────
                if (isSlew && chIndex >= 0) {
                    menu->addChild(new MenuSeparator);
                    menu->addChild(rack::createMenuLabel("Slew Processing"));
                    menu->addChild(rack::createCheckMenuItem(
                        "Slew processing enabled",
                        "",
                        [m, chIndex]{ return m->slewEnabled[chIndex]; },
                        [m, chIndex]{ m->slewEnabled[chIndex] = !m->slewEnabled[chIndex]; }
                    ));
                }
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Global Shape Parameter Detection
                // ───────────────────────────────────────────────────────────────────────────────────────
                if (pid == PolyQuanta::RISE_SHAPE_PARAM) { 
                    lockPtr = &m->lockRiseShape; 
                    allowPtr = &m->allowRiseShape; 
                    isRise = true; 
                }
                if (pid == PolyQuanta::FALL_SHAPE_PARAM) { 
                    lockPtr = &m->lockFallShape; 
                    allowPtr = &m->allowFallShape; 
                    isFall = true; 
                }
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Per-Channel Offset Parameter Context Menu
                // ───────────────────────────────────────────────────────────────────────────────────────
                if (isOffset && chIndex >= 0) {
                    menu->addChild(new MenuSeparator);
                    menu->addChild(rack::createMenuLabel("Controls"));

                    // ───────────────────────────────────────────────────────────────────────────────────────
                    // Per-Channel Range Controls - Direct Float Sliders in Context Menu
                    // ───────────────────────────────────────────────────────────────────────────────────────
                    // These sliders bind directly to module float variables via FloatMenuQuantity
                    {
                        auto* q = new FloatMenuQuantity(&m->preScale[chIndex],
                            -10.0f, 10.0f, 1.0f, "Scale (attenuverter)", " ×", 2);
                        auto* s = new rack::ui::Slider();
                        s->quantity = q; 
                        s->box.size.x = 220.f;                                   // Set slider width
                        menu->addChild(s);
                    }
                    {
                        auto* q = new FloatMenuQuantity(&m->preOffset[chIndex],
                            -10.0f, 10.0f, 0.0f, "Offset (post-scale)", " V", 2);
                        auto* s = new rack::ui::Slider();
                        s->quantity = q; 
                        s->box.size.x = 220.f;                                   // Set slider width
                        menu->addChild(s);
                    }

                    // ───────────────────────────────────────────────────────────────────────────────────────
                    // Per-Channel Offset Knob Behavior Settings
                    // ───────────────────────────────────────────────────────────────────────────────────────
                    menu->addChild(rack::createSubmenuItem("Offset knob snap mode", "", [m, chIndex](rack::ui::Menu* sm){
                        sm->addChild(rack::createCheckMenuItem("Voltages (±10 V)", "", 
                            [m, chIndex]{ return m->snapOffsetModeCh[chIndex] == 0; }, 
                            [m, chIndex]{ m->snapOffsetModeCh[chIndex] = 0; }));
                        sm->addChild(rack::createCheckMenuItem("Semitones (EDO/TET accurate)", "", 
                            [m, chIndex]{ return m->snapOffsetModeCh[chIndex] == 1; }, 
                            [m, chIndex]{ m->snapOffsetModeCh[chIndex] = 1; }));
                        sm->addChild(rack::createCheckMenuItem("Cents (1/1200 V)", "", 
                            [m, chIndex]{ return m->snapOffsetModeCh[chIndex] == 2; }, 
                            [m, chIndex]{ m->snapOffsetModeCh[chIndex] = 2; }));
                    }));
                    
                    menu->addChild(new MenuSeparator);
                    menu->addChild(rack::createMenuLabel("Quantization"));
                    
                    // ───────────────────────────────────────────────────────────────────────────────────────
                    // Per-Channel Quantization Controls
                    // ───────────────────────────────────────────────────────────────────────────────────────
                    // Toggle quantization enable/disable for this specific channel
                    menu->addChild(rack::createCheckMenuItem(
                        "Quantize to scale",
                        "",
                        [m, chIndex]{ return m->qzEnabled[chIndex]; },
                        [m, chIndex]{ m->qzEnabled[chIndex] = !m->qzEnabled[chIndex]; }
                    ));
                    
                    // Quick reset for this channel's octave shift
                    menu->addChild(rack::createMenuItem("Reset this channel's oct shift", "", 
                        [m, chIndex]{ m->postOctShift[chIndex] = 0; }));
                    
                    // Per-channel octave shift submenu (pre-quantization)
                    menu->addChild(rack::createSubmenuItem(std::string("Octave shift (pre") + "-quant)", "", [m, chIndex](rack::ui::Menu* sm){
                        for (int o = -5; o <= 5; ++o) {
                            std::string lbl = (o == 0) ? std::string("0 (default)") : rack::string::f("%+d oct", o);
                            sm->addChild(rack::createCheckMenuItem(
                                lbl,
                                "",
                                [m, chIndex, o]{ return m->postOctShift[chIndex] == o; },
                                [m, chIndex, o]{ m->postOctShift[chIndex] = o; }
                            ));
                        }
                    }));
                }
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Randomization Control Menu Section
                // ───────────────────────────────────────────────────────────────────────────────────────
                if (!lockPtr && !allowPtr) return;                             // No randomization controls for this parameter
                
                menu->addChild(new MenuSeparator);
                menu->addChild(rack::createMenuLabel("Randomize"));
                
                // Determine randomization menu options based on global scope settings
                bool scopeOn = false;
                if (isSlew) scopeOn = m->randSlew;                              // Check slew randomization scope
                else if (isOffset) scopeOn = m->randOffset;                     // Check offset randomization scope
                else if (isRise || isFall) scopeOn = m->randShapes;             // Check shape randomization scope
                
                if (scopeOn) {
                    // Scope is ON: offer per-control lock (exclude from randomization)
                    if (lockPtr)
                        menu->addChild(rack::createBoolPtrMenuItem("Don't randomize me :(", "", lockPtr));
                } else {
                    // Scope is OFF: offer per-control opt-in (include in randomization)
                    if (allowPtr)
                        menu->addChild(rack::createBoolPtrMenuItem("Please randomize me :)", "", allowPtr));
                }
            }
        };

        // ───────────────────────────────────────────────────────────────────────────────────────
        // Global Shape Controls - Rise/Fall Curve Parameters
        // ───────────────────────────────────────────────────────────────────────────────────────
        // These controls affect the slew curve shape for all channels, positioned symmetrically
        // from panel center for visual balance
        {
            addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM - dxColShapesMM, yShapeMM)), module, PolyQuanta::RISE_SHAPE_PARAM));  // Left: Rise shape
            addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM + dxColShapesMM, yShapeMM)), module, PolyQuanta::FALL_SHAPE_PARAM)); // Right: Fall shape
        }
        
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Global Control Parameters - Slew and Offset with Dual-Mode Toggles
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Main global controls positioned below the shape controls, with mode toggle switches
        // inset to provide dual-mode functionality (knob vs CV control)
        {
            addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM - dxColGlobalsMM, yGlobalMM)), module, PolyQuanta::GLOBAL_SLEW_PARAM));   // Left: Global slew
            addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM + dxColGlobalsMM, yGlobalMM)), module, PolyQuanta::GLOBAL_OFFSET_PARAM)); // Right: Global offset
            
            // Dual-mode toggle switches (inset from main knobs)
            addParam(createParamCentered<CKSS>(mm2px(Vec(cxMM - dxColGlobalsMM - dxToggleMM, yGlobalMM)), module, PolyQuanta::GLOBAL_SLEW_MODE_PARAM));   // Slew mode toggle
            addParam(createParamCentered<CKSS>(mm2px(Vec(cxMM + dxColGlobalsMM + dxToggleMM, yGlobalMM)), module, PolyQuanta::GLOBAL_OFFSET_MODE_PARAM)); // Offset mode toggle
        }

        // ───────────────────────────────────────────────────────────────────────────────────────
        // CentsDisplay Widget - Per-Channel Voltage Readout in Cents
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Custom widget that displays the current output voltage of each channel as cents
        // relative to 0V = middle C. Shows "—" when channel is inactive.
        /**
         * @brief Custom widget for displaying per-channel output voltage in cents
         * @note Displays relative to 0V = middle C, with 1V = 1200 cents
         */
        struct CentsDisplay : TransparentWidget {
            PolyQuanta* mod = nullptr;                                   // Module reference
            int ch = 0;                                                  // Channel index
            std::shared_ptr<Font> font;                                  // Font for text rendering
            
            /**
             * @brief Constructor - sets up display widget positioning and module reference
             * @param centerPx Center position in pixels
             * @param sizePx Widget size in pixels
             * @param m Module pointer
             * @param channel Channel index (0-15)
             */
            CentsDisplay(Vec centerPx, Vec sizePx, PolyQuanta* m, int channel) {
                box.size = sizePx; 
                box.pos = centerPx.minus(sizePx.div(2));                 // Center the widget
                mod = m; 
                ch = channel;
            }
            
            /**
             * @brief Renders the cents value text on the foreground layer
             * @param args Drawing arguments with NanoVG context
             * @param layer Rendering layer (only draws on layer 1)
             */
            void drawLayer(const DrawArgs& args, int layer) override {
                if (layer != 1) return;                                  // Only draw on foreground layer
                
                // Load monospace font for consistent number display
                if (!font) font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
                if (!font) return;
                
                // Set up text rendering properties
                nvgFontFaceId(args.vg, font->handle);
                nvgFontSize(args.vg, 9.f);                               // Small font size for compact display
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                NVGcolor col = nvgRGB(220, 220, 220);                    // Light gray text
                nvgFillColor(args.vg, col);
                
                std::string txt = "—";                                   // Default inactive display
                if (mod) {
                    // Only show cents value when channel is active
                    int activeN = std::max(0, mod->polyTrans.curProcN);
                    if (ch < activeN) {
                        float v = mod->lastOut[ch];                      // Get channel output voltage
                        float cents = v * 1200.f;                       // Convert to cents (1V = 1200 cents)
                        
                        // Round to 2 decimal places and clamp to practical bounds
                        cents = std::round(cents * 100.f) / 100.f;
                        if (cents > 12000.f) cents = 12000.f;           // Clamp to ±10V equivalent
                        else if (cents < -12000.f) cents = -12000.f;
                        txt = rack::string::f("%+.2f¢", cents);         // Format with 2 decimals and sign
                    }
                }
                // Render the text centered in the widget
                nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, txt.c_str(), nullptr);
            }
        };

        // ───────────────────────────────────────────────────────────────────────────────────────
        // Per-Channel Control Grid - 8×2 Layout Implementation
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Creates 8 rows with 2 channels each (16 total), arranged as:
        // Left side: [Odd Slew][Even Slew][Odd Cents][Odd LED] | Right side: [Even LED][Even Cents][Odd Offset][Even Offset]
        // This layout provides visual symmetry and logical grouping of controls (and it looks nice :) very important for the user experience)
        {
            for (int row = 0; row < 8; ++row) {
                int chL = row * 2 + 0;                                   // Left channel (odd: 1,3,5,7,9,11,13,15)
                int chR = row * 2 + 1;                                   // Right channel (even: 2,4,6,8,10,12,14,16)
                float y = yRow0MM + row * rowDyMM;                       // Calculate Y position for this row
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Left Side Components - Two Slew Controls, Cents Display, and LED Indicator
                // ───────────────────────────────────────────────────────────────────────────────────────
                // LED indicator for odd channels (green/red for positive/negative)
                addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(cxMM - ledDxMM, y)), module, PolyQuanta::CH_LIGHT + 2*chL));
                // Slew controls: far-left knob for odd channels, inner-left knob for even channels
                addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM - ledDxMM - knobDx2MM, y)), module, PolyQuanta::SL_PARAM[chL])); // Far-left slew
                addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM - ledDxMM - knobDx1MM, y)), module, PolyQuanta::SL_PARAM[chR])); // Inner-left slew
                // Cents display for odd channels output
                addChild(new CentsDisplay(mm2px(Vec(cxMM + dxCentsLeftMM, y + dyCentsMM)), Vec(28.f, 12.f), module, chL));

                // ───────────────────────────────────────────────────────────────────────────────────────
                // Right Side Components - LED Indicator, Cents Display, and Two Offset Controls
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Offset controls: inner-right knob for odd channels, far-right knob for even channels
                addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM + ledDxMM + knobDx1MM, y)), module, PolyQuanta::OFF_PARAM[chL])); // Inner-right offset
                addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM + ledDxMM + knobDx2MM, y)), module, PolyQuanta::OFF_PARAM[chR])); // Far-right offset
                // LED indicator for even channels (green/red for positive/negative)
                addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(cxMM + ledDxMM, y)), module, PolyQuanta::CH_LIGHT + 2*chR));
                // Cents display for even channels output
                addChild(new CentsDisplay(mm2px(Vec(cxMM + dxCentsRightMM, y + dyCentsMM)), Vec(28.f, 12.f), module, chR));
            }
        }
        // ───────────────────────────────────────────────────────────────────────────────────────
        // Bottom Section - I/O Ports and Randomization Controls
        // ───────────────────────────────────────────────────────────────────────────────────────
        // All components anchored to panel center for automatic scaling with HP changes
        {
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Main I/O Ports - Polyphonic Input and Output
            // ───────────────────────────────────────────────────────────────────────────────────────
            addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(cxMM - dxPortsMM, yInOutMM)), module, PolyQuanta::IN_INPUT));   // Polyphonic input
            addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(cxMM + dxPortsMM, yInOutMM)), module, PolyQuanta::OUT_OUTPUT)); // Polyphonic output
            
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Manual Randomization Controls - Center Position
            // ───────────────────────────────────────────────────────────────────────────────────────
            addParam(createParamCentered<VCVButton>(mm2px(Vec(cxMM, yBtnMM)), module, PolyQuanta::RND_PARAM));              // Manual randomize button
            addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(cxMM, yTrigMM)), module, PolyQuanta::RND_TRIG_INPUT)); // Randomize trigger input
            
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Auto-Randomization Controls - Only Added When Module Exists
            // ───────────────────────────────────────────────────────────────────────────────────────
            // These controls are only created when not in library browser mode
            if (module) {
                // Randomization timing and amount knobs
                addParam(createParamCentered<Trimpot>(mm2px(Vec(cxMM - dxRndKnobMM, yRndKnobMM)), module, PolyQuanta::RND_TIME_PARAM)); // Time interval
                addParam(createParamCentered<Trimpot>(mm2px(Vec(cxMM + dxRndKnobMM, yRndKnobMM)), module, PolyQuanta::RND_AMT_PARAM));  // Amount/intensity
                
                // Randomization behavior switches
                addParam(createParamCentered<CKSS>(mm2px(Vec(cxMM - dxRndSwMM, yRndSwMM)), module, PolyQuanta::RND_AUTO_PARAM)); // Auto-randomize enable
                addParam(createParamCentered<CKSS>(mm2px(Vec(cxMM + dxRndSwMM, yRndSwMM)), module, PolyQuanta::RND_SYNC_PARAM)); // Sync to external clock
            }
        }
	}

    // ═════════════════════════════════════════════════════════════════════════════════════════
    // Main Module Context Menu Implementation
    // ═════════════════════════════════════════════════════════════════════════════════════════
    // This comprehensive context menu provides organized access to all module configuration
    // options, grouped into logical sections:
    //
    // OUTPUT SECTION:
    // - Range status display (Clip/Scale mode, voltage range)
    // - Channel count configuration (Auto/1-16 channels with fade timing)
    // - Output processing (sum to mono, averaging, soft clipping)
    // - Range level selection (20V to 1V peak-to-peak)
    // - Range mode selection (Clip vs Scale pre-quantization)
    //
    // CONTROLS SECTION:
    // - Panel layout SVG export utility
    // - Randomization scope controls (slews, offsets, shapes)
    //
    // QUANTIZATION SECTION:
    // - Signal chain order (Quantize→Slew vs Slew→Quantize)
    // - Comprehensive status display (tuning system, root, scale, MOS detection, 
    //   strength, rounding mode, stickiness with limits)
    // - Tuning system selection (EDO octave vs TET non-octave)
    // - EDO selection with curated quick picks (5-120 EDO) plus full range navigator
    // - TET configuration with period controls and Carlos family presets
    // - Root note selection with intelligent 12-EDO pitch-class labeling
    // - Scale selection including preset scales, MOS generators, 12-EDO seeding,
    //   and comprehensive custom scale editor with individual degree toggles
    // - Quantization parameters: strength slider, rounding modes, stickiness presets
    // - Strum effects with timing, behavior, direction, and spread controls
    // ═════════════════════════════════════════════════════════════════════════════════════════

        /**
         * @brief Builds the main module context menu with organized sections
         * @param menu Pointer to the menu being constructed
         * @note Provides comprehensive access to all module configuration options
         */
        void appendContextMenu(Menu* menu) override {
            auto* m = dynamic_cast<PolyQuanta*>(module);
            
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Output Section - Range Management and Channel Configuration
            // ───────────────────────────────────────────────────────────────────────────────────────
            hi::ui::menu::addSection(menu, "Output");
            
            // Display current range status (Clip/Scale mode and voltage range)
            {
                const char* rngMode = (m->rangeMode == 0) ? "Clip" : "Scale";
                float vpp = 2.f * m->currentClipLimit();                        // Peak-to-peak voltage
                menu->addChild(rack::createMenuLabel(rack::string::f("Range: %s %.0f Vpp", rngMode, vpp)));
            }
            
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Channel Count Configuration Submenu
            // ───────────────────────────────────────────────────────────────────────────────────────
            menu->addChild(rack::createSubmenuItem("Channels", "", [m](rack::ui::Menu* sm){
                // Auto mode: match input polyphony
                sm->addChild(rack::createCheckMenuItem("Auto (match input)", "", 
                    [m]{ return m->forcedChannels == 0; }, 
                    [m]{ m->forcedChannels = 0; }));
                sm->addChild(new MenuSeparator);
                
                // Fixed channel count options (1-16)
                for (int n = 1; n <= 16; ++n) {
                    sm->addChild(rack::createCheckMenuItem(rack::string::f("%d", n), "", 
                        [m, n]{ return m->forcedChannels == n; }, 
                        [m, n]{ m->forcedChannels = n; }));
                }
                sm->addChild(new MenuSeparator);
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Channel Switch Fade Time Configuration
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Controls fade-in/out timing when polyphony changes to prevent audio pops
                sm->addChild(rack::createSubmenuItem("Channel switch fade time", "", [m](rack::ui::Menu* sm2){
                    struct Opt { const char* label; float ms; };
                    const Opt opts[] = {
                        {"0 ms", 0.f}, {"5 ms", 5.f}, {"10 ms", 10.f}, {"20 ms", 20.f},
                        {"50 ms", 50.f}, {"100 ms (default)", 100.f}, {"200 ms", 200.f},
                        {"500 ms", 500.f}, {"1000 ms", 1000.f}
                    };
                    for (auto& o : opts) {
                        const char* label = o.label;
                        const float msVal = o.ms;
                        sm2->addChild(rack::createCheckMenuItem(
                            label,
                            "",
                            [m, msVal]{ return std::fabs(m->polyFadeSec - (msVal * 0.001f)) < 1e-6f; },
                            [m, msVal]{ m->polyFadeSec = msVal * 0.001f; }
                        ));
                    }
                }));
            }));
            
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Output Processing Options
            // ───────────────────────────────────────────────────────────────────────────────────────
            hi::ui::menu::addBoolPtr(menu, "Sum to mono (post‑slew)", &m->sumToMonoOut);
            hi::ui::menu::addBoolPtr(menu, "Average when summing", &m->avgWhenSumming, [m]{ return m->sumToMonoOut; });
            hi::ui::menu::addBoolPtr(menu, "Soft clip (range + final)", &m->softClipOut);
            
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Range Level Configuration (Peak-to-Peak Voltage)
            // ───────────────────────────────────────────────────────────────────────────────────────
            menu->addChild(rack::createSubmenuItem("Range (Vpp)", "", [m](rack::ui::Menu* sm){
                struct Opt { const char* label; int idx; };
                const Opt opts[] = {
                    {"20 V", 0}, {"15 V", 1}, {"10 V", 2}, {"5 V", 3}, {"2 V", 4}, {"1 V", 5}
                };
                for (auto& o : opts) {
                    sm->addChild(rack::createCheckMenuItem(o.label, "", 
                        [m, o]{ return m->clipVppIndex == o.idx; }, 
                        [m, o]{ m->clipVppIndex = o.idx; }));
                }
            }));
            
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Range Mode Configuration (Pre-Quantization)
            // ───────────────────────────────────────────────────────────────────────────────────────
            menu->addChild(rack::createSubmenuItem("Range mode (pre‑quant)", "", [m](rack::ui::Menu* sm){
                sm->addChild(rack::createCheckMenuItem("Clip", "", 
                    [m]{ return m->rangeMode == 0; }, 
                    [m]{ m->rangeMode = 0; }));
                sm->addChild(rack::createCheckMenuItem("Scale", "", 
                    [m]{ return m->rangeMode == 1; }, 
                    [m]{ m->rangeMode = 1; }));
            }));
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Controls Section - Module Configuration and Utility Functions
            // ───────────────────────────────────────────────────────────────────────────────────────
            hi::ui::menu::addSection(menu, "Controls");
            
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Panel Layout Export Utility
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Exports a rich SVG snapshot including panel artwork and component approximations
            menu->addChild(rack::createMenuItem("Export layout SVG (user folder)", "", [this]{
                // Delegated to extracted implementation for maintainability
                PanelExport::exportPanelSnapshot(this, "PolyQuanta", "res/PolyQuanta.svg");
            }));
            
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Randomization Scope Controls
            // ───────────────────────────────────────────────────────────────────────────────────────
            // These toggles determine which parameter types are affected by randomization
            menu->addChild(rack::createSubmenuItem("Randomize Scope", "", [m](rack::ui::Menu* sm){
                sm->addChild(rack::createBoolPtrMenuItem("Slews", "", &m->randSlew));
                sm->addChild(rack::createBoolPtrMenuItem("Offsets", "", &m->randOffset));
                sm->addChild(rack::createBoolPtrMenuItem("Shapes", "", &m->randShapes));
            }));

            // ───────────────────────────────────────────────────────────────────────────────────────
            // Quantization Section - Musical Scale Processing Configuration
            // ───────────────────────────────────────────────────────────────────────────────────────
            hi::ui::menu::addSection(menu, "Quantization");
            
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Signal Chain Order Configuration
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Controls whether quantization happens before or after slew processing
            menu->addChild(rack::createSubmenuItem("Signal chain →", "", [m](rack::ui::Menu* sm){
                // Pre-slew quantization: allows pitch-bend effects through slewing
                sm->addChild(rack::createCheckMenuItem("Pitch-bend: Quantize → Slew (Q→S)", "", 
                    [m]{ return m->quantizerPos == PolyQuanta::QuantizerPos::Pre; }, 
                    [m]{ m->quantizerPos = PolyQuanta::QuantizerPos::Pre; }));
                // Post-slew quantization: ensures pitch-accurate final output
                sm->addChild(rack::createCheckMenuItem("Pitch-accurate: Slew → Quantize (S→Q)", "", 
                    [m]{ return m->quantizerPos == PolyQuanta::QuantizerPos::Post; }, 
                    [m]{ m->quantizerPos = PolyQuanta::QuantizerPos::Post; }));
            }));
            
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Quantization Status Display
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Shows current quantization settings in a comprehensive status line
            {
                int steps = (m->tuningMode == 0) ? m->edo : m->tetSteps;
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Root Note Label Generation
                // ───────────────────────────────────────────────────────────────────────────────────────
                std::string rootStr;
                if (m->tuningMode == 0 && steps == 12) {
                    // 12-EDO: use standard note names
                    static const char* noteNames[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                    int rn = (m->rootNote % 12 + 12) % 12;
                    rootStr = noteNames[rn];
                } else {
                    // Other tuning systems: use numeric step notation with 12-EDO approximations when close
                    static const char* noteNames[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                    int root = (m->rootNote % std::max(1, steps) + std::max(1, steps)) % std::max(1, steps);
                    
                    // Calculate 12-EDO pitch class approximation (same logic as root menu)
                    float period = (m->tuningMode == 0) ? 1.f : 
                        ((m->tetPeriodOct > 0.f) ? m->tetPeriodOct : std::log2(3.f/2.f));
                    float semis = (float)root * 12.f * period / (float)steps;
                    int nearestPc = (int)std::round(semis);
                    float delta = semis - (float)nearestPc;
                    float err = std::fabs(delta);
                    
                    // Check for exact match (EDO systems divisible by 12)
                    bool exact = false;
                    if (m->tuningMode == 0 && (steps % 12) == 0) {
                        int stepPerSemi = steps / 12;
                        exact = (root % stepPerSemi) == 0;
                    } else {
                        exact = err <= 1e-6f;
                    }
                    
                    // Generate label based on pitch-class proximity (5-cent tolerance)
                    int pc12 = ((nearestPc % 12) + 12) % 12;
                    if (exact) {
                        // Exact match: show note name only
                        rootStr = noteNames[pc12];
                    } else if (err <= 0.05f) {
                        // Close approximation (within 5 cents): show note name with cents
                        int cents = (int)std::round(delta * 100.f);
                        if (cents != 0) 
                            rootStr = rack::string::f("%s %+d¢", noteNames[pc12], cents);
                        else 
                            rootStr = noteNames[pc12];
                    } else {
                        // Distant approximation: show only step number
                        rootStr = rack::string::f("%d", root);
                    }
                }
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Scale Name Label Generation
                // ───────────────────────────────────────────────────────────────────────────────────────
                std::string scaleStr;
                if (m->tuningMode == 0 && steps == 12 && !m->useCustomScale) {
                    // 12-EDO preset scales
                    int idx = (m->scaleIndex >= 0 && m->scaleIndex < hi::music::NUM_SCALES_12EDO) ? m->scaleIndex : 0;
                    scaleStr = hi::music::scales12EDO()[idx].name;
                } else if (m->tuningMode == 0 && steps == 24 && !m->useCustomScale) {
                    // 24-EDO preset scales
                    int idx = (m->scaleIndex >= 0 && m->scaleIndex < hi::music::NUM_SCALES_24EDO) ? m->scaleIndex : 0;
                    scaleStr = hi::music::scales24EDO()[idx].name;
                } else if (m->useCustomScale) {
                    // Custom scale - try to detect if it matches a predefined scale
                    const hi::music::Scale* matchingScale = hi::music::scale::detectMatchingScale(m->customMaskGeneric, steps);
                    if (matchingScale) {
                        scaleStr = matchingScale->name;
                    } else {
                        scaleStr = "Custom";
                    }
                } else {
                    // Non-standard tuning without custom scale
                    scaleStr = "Custom";
                }
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Quantization Parameters Calculation
                // ───────────────────────────────────────────────────────────────────────────────────────
                int pct = (int)std::round(clamp(m->quantStrength, 0.f, 1.f) * 100.f);  // Strength percentage
                float period = (m->tuningMode == 0) ? 1.f : m->tetPeriodOct;           // Octave period
                int N = std::max(1, steps);                                             // Valid step count
                float dV = period / (float)N;                                           // Voltage per step
                float stepCents = 1200.f * dV;                                          // Cents per step
                float maxStick = std::floor(0.4f * stepCents);                          // Maximum sticky range
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Quantization Mode Label Generation
                // ───────────────────────────────────────────────────────────────────────────────────────
                const char* roundStr = "Directional Snap";                             // Default mode name
                switch (m->quantRoundMode) {
                    case 0: roundStr = "Directional Snap"; break;
                    case 1: roundStr = "Nearest"; break;
                    case 2: roundStr = "Up"; break;
                    case 3: roundStr = "Down"; break;
                }
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // MOS (Moment of Symmetry) Detection for Status Annotation
                // ───────────────────────────────────────────────────────────────────────────────────────
                int mosM = 0, mosG = 0; 
                bool mosOk = hi::music::mos::detectCurrentMOS(m, mosM, mosG);
                std::string mosStr = mosOk ? rack::string::f(", MOS %d/gen %d", mosM, mosG) : "";
                
                // Display comprehensive quantization status
                menu->addChild(rack::createMenuLabel(rack::string::f(
                    "Status: %s %d, Root %s, Scale %s%s, Strength %d%%, Round %s, Stickiness %.1f¢ (max %.0f¢)",
                    (m->tuningMode == 0 ? "EDO" : "TET"), steps, rootStr.c_str(), scaleStr.c_str(), mosStr.c_str(), pct,
                    roundStr, m->stickinessCents, maxStick)));
            }
            
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Tuning System Configuration
            // ───────────────────────────────────────────────────────────────────────────────────────
            menu->addChild(rack::createSubmenuItem("Tuning system", "", [m](rack::ui::Menu* sm){
                sm->addChild(rack::createCheckMenuItem("EDO (octave)", "", 
                    [m]{ return m->tuningMode == 0; }, 
                    [m]{ m->tuningMode = 0; m->invalidateMOSCache(); }));
                sm->addChild(rack::createCheckMenuItem("TET (non-octave)", "", 
                    [m]{ return m->tuningMode == 1; }, 
                    [m]{ m->tuningMode = 1; m->invalidateMOSCache(); }));
            }));
            // ───────────────────────────────────────────────────────────────────────────────────────
            // EDO (Equal Division of Octave) Selection Menu
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Provides curated quick picks with descriptions plus full range (1-120 EDO) navigator (labels show cents/step)
            menu->addChild(rack::createSubmenuItem("EDO", "", [m](rack::ui::Menu* sm){
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Helper Functions - Custom Scale Mask Preservation
                // ───────────────────────────────────────────────────────────────────────────────────────
                // These functions preserve the current custom scale mask when changing EDO divisions
                auto readMask = [m](int Nsrc) {
                    std::vector<uint8_t> out;
                    // All EDOs now use the generic vector-based mask system
                    out = m->customMaskGeneric;
                    if ((int)out.size() != Nsrc) {
                        out.assign((size_t)Nsrc, 0);
                    }
                    return out;
                };
                
                auto writeMask = [m](int Ndst, const std::vector<uint8_t>& in) {
                    // All EDOs now use the generic vector-based mask system
                    m->customMaskGeneric = in;
                    m->customMaskGeneric.resize((size_t)Ndst, 0);
                };
                auto resampleMask = [](const std::vector<uint8_t>& src, int Ndst) {
                    std::vector<uint8_t> dst((size_t)Ndst, 0);
                    int Nsrc = (int)src.size();
                    if (Nsrc <= 0 || Ndst <= 0) return dst;
                    // Resample custom scale mask from source EDO to destination EDO
                    for (int i = 0; i < Nsrc; ++i) if (src[(size_t)i]) {
                        int j = (int)std::round((double)i * (double)Ndst / (double)Nsrc);
                        if (j >= Ndst) j = Ndst - 1;
                        dst[(size_t)j] = 1;
                    }
                    return dst;
                };

                // ───────────────────────────────────────────────────────────────────────────────────────
                // Curated EDO Quick Selection List
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Provides musically meaningful EDO divisions with descriptive explanations
                struct Quick { int edo; const char* desc; };
                static const Quick quicks[] = {
                    {5,  "Equal pentatonic; spacious, open."},
                    {6,  "Whole-tone planing; dreamy."},
                    {7,  "“Neutral diatonic” with singable modes."},
                    {8,  "Symmetric; tritone center."},
                    {9,  "Neutral chain; edgy fifths."},
                    {10, "Wide-step palette; broad fifth analogue."},
                    {11, "Neutral 2nds/3rds; distinctive color."},
                    {12, "Baseline tonal workflow & compatibility."},
                    {13, "Blackwood flavor; coherent alien triads."},
                    {14, "Slightly finer than 12; softened fifths."},
                    {16, "Binary grid; symmetric flows."},
                    {17, "Alt to 12; bright minor/soft major."},
                    {18, "Third-tone palette ideal for expressive slides."},
                    {19, "Meantone-like diatonicism; strong fifths."},
                    {20, "Hybrid diatonic/symmetric design."},
                    {22, "Porcupine temperament; crunchy chords."},
                    {24, "Quarter-tone system; nests with 12."},
                    {25, "Mid-resolution microtonal; pairs with 50."},
                    {26, "Neutral thirds; distinctive harmonic color."},
                    {27, "Tripled 9-EDO; third-tone precision."},
                    {29, "Superpyth temperament; wide fifths."},
                    {31, "Meantone champion; near-JI accuracy."},
                    {34, "Superpyth double; enhanced precision."},
                    {36, "Sixth-tone system; dense inflection."},
                    {38, "Double-19; added precision."},
                    {41, "Strong 7-limit approximations."},
                    {43, "Partch-like color while equal."},
                    {44, "Double-22; finer porcupine grain."},
                    {48, "Eighth-tone; nests with 12/24."},
                    {50, "Fine control; halves of 25-EDO."},
                    {52, "Double-26; smoother stepwork."},
                    {53, "Near-JI accuracy; 5/7-limit hero."},
                    {55, "Divisible by 5 & 11; limit color sets."},
                    {60, "Five per semitone; consistent micro-bends."},
                    {62, "Double-31; precise meantone family."},
                    {64, "Power-of-two grid; precise experiments."},
                    {72, "Twelfth-tone system; embeds 12/24/36."},
                    {96, "Sixteenth-tone; detailed retuning."},
                    {120, "Ultra-fine granularity; easy rescaling."}
                };
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Quick Picks Menu Section
                // ───────────────────────────────────────────────────────────────────────────────────────
                sm->addChild(rack::createMenuLabel("Quick picks"));
                for (const auto& q : quicks) {
                    int e = q.edo;
                    float cents = 1200.f / (float)e;                            // Calculate cents per step
                    sm->addChild(rack::createCheckMenuItem(
                        rack::string::f("%d-EDO (%.2f¢)", e, cents), q.desc,
                        [m, e]{ return m->tuningMode == 0 && m->edo == e; },
                        [m, e, readMask, writeMask, resampleMask]{
                            // ───────────────────────────────────────────────────────────────────────────────────────
                            // EDO Change Handler - Preserves Root Note and Custom Scale
                            // ───────────────────────────────────────────────────────────────────────────────────────
                            // Preserve root note by pitch (proportional scaling)
                            int oldEDO  = std::max(1, m->edo);
                            int oldRoot = (m->rootNote % oldEDO + oldEDO) % oldEDO;
                            
                            // Always preserve custom scale mask when changing EDO
                            std::vector<uint8_t> src;
                            if (m->useCustomScale) src = readMask(oldEDO);
                            
                            // Apply EDO change
                            m->tuningMode = 0;
                            m->edo = e;
                            
                            // Scale root note proportionally to new EDO
                            int newRoot = (int)std::round((double)oldRoot * (double)e / (double)oldEDO);
                            m->rootNote = (newRoot % e + e) % e;
                            
                            // Resample and restore custom scale mask if needed
                            if (!src.empty()) {
                                auto dst = resampleMask(src, e);
                                writeMask(e, dst);
                            }
                            m->invalidateMOSCache();
                        }
                    ));
                }
                
                sm->addChild(new MenuSeparator);
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Full Range (1-120 EDO) Navigator - Complete N-EDO Selection
                // ───────────────────────────────────────────────────────────────────────────────────────
                sm->addChild(rack::createMenuLabel("N-EDO"));
                auto addRange = [m, readMask, writeMask, resampleMask](rack::ui::Menu* dst, int a, int b){
                    for (int e = a; e <= b; ++e) {
                        float cents = 1200.f / (float)e;                        // Calculate cents per step
                        dst->addChild(rack::createCheckMenuItem(
                            rack::string::f("%d-EDO (%.2f¢)", e, cents), "",
                            [m, e]{ return m->tuningMode == 0 && m->edo == e; },
                            [m, e, readMask, writeMask, resampleMask]{
                                // Same preservation logic as quick picks
                                int oldEDO  = std::max(1, m->edo);
                                int oldRoot = (m->rootNote % oldEDO + oldEDO) % oldEDO;
                                std::vector<uint8_t> src;
                                if (m->useCustomScale) src = readMask(oldEDO);
                                m->tuningMode = 0;
                                m->edo = e;
                                int newRoot = (int)std::round((double)oldRoot * (double)e / (double)oldEDO);
                                m->rootNote = (newRoot % e + e) % e;
                                if (!src.empty()) {
                                    auto dst = resampleMask(src, e);
                                    writeMask(e, dst);
                                }
                                m->invalidateMOSCache();
                            }
                        ));
                    }
                };
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Range-Based EDO Selection Submenus
                // ───────────────────────────────────────────────────────────────────────────────────────
                sm->addChild(rack::createSubmenuItem("1-30",  "", [addRange](rack::ui::Menu* sm2){ addRange(sm2, 1, 30); }));
                sm->addChild(rack::createSubmenuItem("31-60", "", [addRange](rack::ui::Menu* sm2){ addRange(sm2, 31, 60); }));
                sm->addChild(rack::createSubmenuItem("61-90", "", [addRange](rack::ui::Menu* sm2){ addRange(sm2, 61, 90); }));
                sm->addChild(rack::createSubmenuItem("91-120","", [addRange](rack::ui::Menu* sm2){ addRange(sm2, 91, 120); }));
            }));
            
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Root Note Configuration Menu
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Labels entries by their 12-EDO pitch-class mapping from 1 V/oct (0 -> C)
            menu->addChild(rack::createSubmenuItem("Root", "", [m](rack::ui::Menu* sm) {
                int N = (m->tuningMode == 0 ? m->edo : m->tetSteps);
                if (N <= 0) N = 12;                                             // Default to 12 if invalid
                float period = (m->tuningMode == 0) ? 1.f : 
                    ((m->tetPeriodOct > 0.f) ? m->tetPeriodOct : std::log2(3.f/2.f));
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Root Note Range Helper Function
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Capture N and period by value so submenu population works after lambda returns
                auto addRange = [m, N, period](rack::ui::Menu* menuDest, int start, int end){
                    static const char* noteNames[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                    for (int n = start; n <= end && n < N; ++n) {
                        // ───────────────────────────────────────────────────────────────────────────────────────
                        // Pitch-Class Mapping and Label Generation
                        // ───────────────────────────────────────────────────────────────────────────────────────
                        float semis = (float)n * 12.f * period / (float)N;         // Convert to semitones
                        int nearestPc = (int)std::round(semis);                     // Nearest 12-EDO pitch class
                        float delta = semis - (float)nearestPc;                     // Deviation from 12-EDO
                        float err = std::fabs(delta);                               // Absolute error
                        
                        // Determine if this step exactly matches a 12-EDO pitch class
                        bool exact = false;
                        if (m->tuningMode == 0 && (N % 12) == 0) {
                            // For EDO systems divisible by 12, check exact alignment
                            int stepPerSemi = N / 12;
                            exact = (n % stepPerSemi) == 0;
                        } else {
                            // For other systems, use floating-point tolerance
                            exact = err <= 1e-6f;
                        }
                        
                        // Generate appropriate label based on pitch-class proximity
                        int pc12 = ((nearestPc % 12) + 12) % 12;
                        std::string label;
                        if (exact) {
                            // Exact match: show step number and note name
                            label = rack::string::f("%d (%s)", n, noteNames[pc12]);
                        } else if (err <= 0.05f) {
                            // Close approximation: show cents deviation
                            int cents = (int)std::round(delta * 100.f);
                            if (cents != 0) 
                                label = rack::string::f("%d (≈%s %+d¢)", n, noteNames[pc12], cents);
                            else 
                                label = rack::string::f("%d (≈%s)", n, noteNames[pc12]);
                        } else {
                            // Distant approximation: show only step number
                            label = rack::string::f("%d", n);
                        }
                        
                        // Create menu item for this root note option
                        menuDest->addChild(rack::createCheckMenuItem(label, "", 
                            [m, n]{ return m->rootNote == n; }, 
                            [m, n]{ m->rootNote = n; m->invalidateMOSCache(); }));
                    }
                };
                
                // ───────────────────────────────────────────────────────────────────────────────────────
                // Root Note Range Organization
                // ───────────────────────────────────────────────────────────────────────────────────────
                if (N > 72) {
                    // For large tuning systems, split into thirds with remainder distribution
                    int base = N / 3;
                    int rem = N % 3;
                    int size1 = base + (rem > 0 ? 1 : 0);
                    int size2 = base + (rem > 1 ? 1 : 0);
                    // Third size is implied by remainder; no variable needed
                    int s1 = 0;            int e1 = size1 - 1;
                    int s2 = e1 + 1;       int e2 = s2 + size2 - 1;
                    int s3 = e2 + 1;       int e3 = N - 1;
                    sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", s1, e1), "", 
                        [addRange, s1, e1](rack::ui::Menu* sm2){ addRange(sm2, s1, e1); }));
                    sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", s2, e2), "", 
                        [addRange, s2, e2](rack::ui::Menu* sm2){ addRange(sm2, s2, e2); }));
                    sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", s3, e3), "", 
                        [addRange, s3, e3](rack::ui::Menu* sm2){ addRange(sm2, s3, e3); }));
                } else if (N > 36) {
                    // For medium systems, split into halves (lower gets extra when odd)
                    int halfLo = (N % 2 == 1) ? ((N + 1) / 2) : (N / 2);
                    int loStart = 0;           int loEnd = halfLo - 1;
                    int hiStart = halfLo;      int hiEnd = N - 1;
                    sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", loStart, loEnd), "", 
                        [addRange, loStart, loEnd](rack::ui::Menu* sm2){ addRange(sm2, loStart, loEnd); }));
                    sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", hiStart, hiEnd), "", 
                        [addRange, hiStart, hiEnd](rack::ui::Menu* sm2){ addRange(sm2, hiStart, hiEnd); }));
                } else {
                    // For small systems, show all root notes directly
                    addRange(sm, 0, N-1);
                }
            }));
            
            // ───────────────────────────────────────────────────────────────────────────────────────
            // Scale Selection and Custom Degrees Editor
            // ───────────────────────────────────────────────────────────────────────────────────────
            menu->addChild(rack::createSubmenuItem("Scale", "", [m](rack::ui::Menu* sm) {
                
                // Helper function to select a preset scale and enable custom mode
                auto selectPresetScale = [m](const hi::music::Scale* scales, int scaleIndex, int scaleCount) {
                    if (scaleIndex >= 0 && scaleIndex < scaleCount) {
                        // Set the scale index
                        m->scaleIndex = scaleIndex;
                        
                        // Enable custom mode and seed with the selected scale
                        m->useCustomScale = true;
                        
                        // Seed custom scale from selected preset
                                int N = (m->tuningMode == 0 ? m->edo : m->tetSteps);
                                if (N <= 0) N = 12;
                        
                        // Use vector mask directly for all EDO sizes
                        // The core quantization logic handles root note transformation
                        m->customMaskGeneric = scales[scaleIndex].mask;
                        if ((int)m->customMaskGeneric.size() != N) {
                            m->customMaskGeneric.resize((size_t)N, 0);
                        }
                        m->invalidateMOSCache();
                    }
                };
                
                // Helper function to add scale submenu with checkmarks for matching scales
                auto addScaleSubmenu = [&](const char* category, const hi::music::Scale* scales, int count) {
                    sm->addChild(rack::createSubmenuItem(category, "", [m, scales, count, selectPresetScale](rack::ui::Menu* smScales) {
                        for (int i = 0; i < count; ++i) {
                            // Check if this scale matches the current custom scale
                            bool isMatching = false;
                            if (m->useCustomScale) {
                                const hi::music::Scale* matchingScale = hi::music::scale::detectMatchingScale(m->customMaskGeneric, m->tuningMode == 0 ? m->edo : m->tetSteps);
                                isMatching = (matchingScale == &scales[i]);
                            }
                            
                            smScales->addChild(rack::createCheckMenuItem(scales[i].name, "", 
                                [isMatching]{ return isMatching; },
                                [scales, i, count, selectPresetScale]{
                                    selectPresetScale(scales, i, count);
                                }));
                        }
                    }));
                };
                
                // Show appropriate scales based on current tuning system
                if (m->tuningMode == 0) {
                    // EDO tuning systems
                    int edo = m->edo;
                    
                    if (edo >= 1 && edo <= 120) {
                        // All other EDOs 1-120 get chromatic scales
                        std::string label = rack::string::f("%d-EDO Chords & Scales", edo);
                        addScaleSubmenu(label.c_str(), hi::music::scalesEDO(edo), hi::music::numScalesEDO(edo));
                    }
                } else {
                    // TET tuning systems
                    int steps = m->tetSteps;
                    float period = m->tetPeriodOct;
                    
                    if (std::fabs(period - 1.0f) < 1e-6f) {
                        // Octave-based systems
                        if (steps == 12) {
                            addScaleSubmenu("12-EDO Scales", hi::music::scales12EDO(), hi::music::NUM_SCALES_12EDO);
                        } else if (steps == 24) {
                            addScaleSubmenu("24-EDO Scales", hi::music::scales24EDO(), hi::music::NUM_SCALES_24EDO);
                        } else if (steps == 7) {
                            addScaleSubmenu("Just Intonation", hi::music::scales7EDO(), hi::music::NUM_SCALES_7EDO);
                        } else if (steps == 22 || steps == 17 || steps == 5) {
                            addScaleSubmenu("World Music", hi::music::scales5EDO(), hi::music::NUM_SCALES_5EDO);
                        } else if (steps == 13 || steps == 16 || steps == 32) {
                            addScaleSubmenu("Experimental", hi::music::scalesEDO(steps), hi::music::numScalesEDO(steps));
                        }
                    } else if (std::fabs(period - std::log2(3.f)) < 1e-6f) {
                        // Bohlen-Pierce (tritave-based)
                        addScaleSubmenu("Bohlen-Pierce", hi::music::scalesEDO(steps), hi::music::numScalesEDO(steps));
                    } else if (std::fabs(period - std::log2(1.618f)) < 1e-6f) {
                        // Golden Ratio based systems
                        if (steps == 12) {
                            addScaleSubmenu("Golden Ratio Scales", hi::music::scales12EDO(), hi::music::NUM_SCALES_12EDO);
                        } else if (steps == 5 || steps == 8) {
                            addScaleSubmenu("Fibonacci Scales", hi::music::scales5EDO(), hi::music::NUM_SCALES_5EDO);
                } else {
                            addScaleSubmenu("Golden Ratio Scales", hi::music::scalesEDO(steps), hi::music::numScalesEDO(steps));
                        }
                    } else {
                        // Other TET systems
                        addScaleSubmenu("Experimental", hi::music::scalesEDO(steps), hi::music::numScalesEDO(steps));
                    }
                }
                
                // MOS (Moment of Symmetry) Presets
                    sm->addChild(rack::createSubmenuItem("MOS presets (current EDO)", "", [m](rack::ui::Menu* smMos){
                    int N = (m->tuningMode == 0 ? m->edo : m->tetSteps);
                    if (N <= 0) N = 12;
                    
                    // Get MOS generators for current EDO
                        auto it = hi::music::mos::curated.find(N);
                    if (it != hi::music::mos::curated.end()) {
                        const auto& gens = it->second;
                        for (int g : gens) {
                            // Create submenu for each generator
                            std::string genLabel = rack::string::f("Generator %d", g);
                            smMos->addChild(rack::createSubmenuItem(genLabel, "", [m, N, g](rack::ui::Menu* smGen){
                                // Find the best generator for 7-note scales as reference
                                int bestGen = hi::music::mos::findBestGenerator(N, 7);
                                
                                // Try different mode sizes (5-9 notes)
                                for (int modeSize = 5; modeSize <= 9; modeSize++) {
                                    auto cyc = hi::music::mos::generateCycle(N, g, modeSize);
                                    if (cyc.size() >= 2 && hi::music::mos::isMOS(cyc, N)) {
                                        std::string pattern = hi::music::mos::patternLS(cyc, N);
                                        bool isBest = (g == bestGen && modeSize == 7);
                                        std::string label = rack::string::f("%d-note (%s)%s", 
                                            modeSize, pattern.c_str(), isBest ? " ★" : "");
                                        
                                        smGen->addChild(rack::createMenuItem(label, "", [m, N, g, modeSize]{
                                            // Generate MOS scale with this generator and mode size
                                            auto cyc = hi::music::mos::generateCycle(N, g, modeSize);
                                            if (cyc.size() >= 2) {
                                                // Convert to custom mask (stored in root-relative space)
                                                m->customMaskGeneric.assign((size_t)N, 0);
                                                
                                                // The generateCycle creates a pattern starting from 0, which is perfect
                                                // for root-relative storage. The core quantization logic will handle
                                                // the root note transformation during quantization.
                                                for (int pc : cyc) {
                                                    if (pc >= 0 && pc < N) {
                                                        m->customMaskGeneric[pc] = 1;
                                                    }
                                                }
                                                m->useCustomScale = true;
                                                m->invalidateMOSCache();
                                            }
                                        }));
                                    }
                                }
                                
                                if (smGen->children.empty()) {
                                    smGen->addChild(rack::createMenuLabel("No valid MOS scales found"));
                                }
                            }));
                        }
                    } else {
                        smMos->addChild(rack::createMenuLabel("No MOS generators available"));
                        }
                    }));

                // Helper function to get current custom mask
                auto getCurrentMask = [m]() -> std::vector<uint8_t> {
                    int N = (m->tuningMode == 0 ? m->edo : m->tetSteps);
                    if (N <= 0) N = 12;
                    
                    // Use unified vector mask for all EDO sizes
                    if ((int)m->customMaskGeneric.size() != N) {
                        m->customMaskGeneric.assign((size_t)N, 0);
                    }
                    return m->customMaskGeneric;
                };
                
                // Helper function to set custom mask
                auto setCurrentMask = [m](const std::vector<uint8_t>& mask) {
                    int N = (m->tuningMode == 0 ? m->edo : m->tetSteps);
                    if (N <= 0) N = 12;
                    
                    // Use vector mask directly for all EDO sizes
                    m->customMaskGeneric = mask;
                    if ((int)m->customMaskGeneric.size() != N) {
                        m->customMaskGeneric.resize((size_t)N, 0);
                    }
                    m->useCustomScale = true;
                        m->invalidateMOSCache();
                };
                
                // Custom Scale Editor - Direct menu items
                sm->addChild(rack::createMenuItem("Select All Notes", "", [m, setCurrentMask]{
                    int N = (m->tuningMode == 0 ? m->edo : m->tetSteps);
                    if (N <= 0) N = 12;
                    std::vector<uint8_t> mask((size_t)N, 1);
                    setCurrentMask(mask);
                }));
                
                sm->addChild(rack::createMenuItem("Clear All Notes", "", [m, setCurrentMask]{
                    int N = (m->tuningMode == 0 ? m->edo : m->tetSteps);
                    if (N <= 0) N = 12;
                    std::vector<uint8_t> mask((size_t)N, 0);
                    setCurrentMask(mask);
                }));
                
                sm->addChild(rack::createMenuItem("Invert Selection", "", [getCurrentMask, setCurrentMask]{
                    auto mask = getCurrentMask();
                    for (auto& bit : mask) {
                        bit = 1 - bit;
                    }
                    setCurrentMask(mask);
                }));
                
                    // 12-EDO Alignment Helper (EDO Mode Only)
                    if (m->tuningMode == 0) {
                    sm->addChild(rack::createMenuItem("Select aligned 12-EDO notes", "", [m]{
                            int N = std::max(1, m->edo);
                            
                            // Helper function to set degree state in root-relative mask
                            auto setDeg = [&](int degAbs, bool on){
                            int bit = (((degAbs - m->rootNote) % N + N) % N);
                                // Use unified vector mask for all EDO sizes
                                if ((int)m->customMaskGeneric.size() != N) m->customMaskGeneric.assign((size_t)N, 0);
                                m->customMaskGeneric[(size_t)bit] = on ? 1 : 0;
                            };
                            
                            // Apply alignment logic based on EDO type
                            if (N % 12 == 0) {
                                int stepPerSemi = N / 12;
                                for (int d = 0; d < N; ++d) {
                                    int n = ((m->rootNote + d) % N + N) % N;
                                    bool aligned = (d % stepPerSemi) == 0;
                                    setDeg(n, aligned);
                                }
                            } else {
                                for (int d = 0; d < N; ++d) {
                                    int n = ((m->rootNote + d) % N + N) % N;
                                float cents = 1200.0f * (float)d / (float)N;
                                float semitoneCents = 100.0f * std::round(cents / 100.0f);
                                bool aligned = std::fabs(cents - semitoneCents) < 5.0f;
                                    setDeg(n, aligned);
                                }
                            }
                        m->useCustomScale = true;
                            m->invalidateMOSCache();
                        }));
                    }
                    
                // Individual Degree Editor with proper division
                sm->addChild(rack::createSubmenuItem("Degrees", "", [m, getCurrentMask, setCurrentMask](rack::ui::Menu* smDeg){
                    int N = (m->tuningMode == 0 ? m->edo : m->tetSteps);
                    if (N <= 0) N = 12;
                    
                    auto mask = getCurrentMask();
                    if ((int)mask.size() != N) {
                        mask.assign((size_t)N, 0);
                        setCurrentMask(mask);
                    }
                    
                    // Helper function to get note name for degree
                    auto getNoteName = [](int deg, int root, int N, int displayIndex) -> std::string {
                        const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                        // Calculate the actual pitch class relative to root
                        int actualPC = (deg + root) % N;
                        // Convert to 12-EDO equivalent for display
                        float note12Float = (actualPC * 12.0f) / N;
                        int note12 = (int)std::round(note12Float);
                        if (note12 < 0) note12 += 12;
                        note12 = note12 % 12;
                        
                        // Calculate cents deviation from 12-EDO
                        float cents = (note12Float - note12) * 100.0f;
                        if (cents > 50.0f) cents -= 100.0f;
                        if (cents < -50.0f) cents += 100.0f;
                        
                        // Only show notes within ±5 cents of 12-EDO
                        if (std::abs(cents) <= 5.0f) {
                            if (std::abs(cents) < 0.1f) {
                                return rack::string::f("%d (%s)", displayIndex + 1, noteNames[note12]);
                            } else {
                                return rack::string::f("%d (%s %+.0f¢)", displayIndex + 1, noteNames[note12], cents);
                            }
                        } else {
                            // Show as microtonal degree without note name
                            return rack::string::f("%d", displayIndex + 1);
                        }
                    };
                    
                    // Apply proper degree division logic
                    if (N > 72) {
                        // For large tuning systems, split into thirds with remainder distribution
                        int base = N / 3;
                        int rem = N % 3;
                        int size1 = base + (rem > 0 ? 1 : 0);
                        int size2 = base + (rem > 1 ? 1 : 0);
                        int s1 = 0;            int e1 = size1 - 1;
                        int s2 = e1 + 1;       int e2 = s2 + size2 - 1;
                        int s3 = e2 + 1;       int e3 = N - 1;
                        
                        smDeg->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", s1, e1), "", 
                            [m, N, getCurrentMask, setCurrentMask, getNoteName, s1, e1](rack::ui::Menu* sm2){
                                auto mask = getCurrentMask();
                                for (int i = s1; i <= e1; ++i) {
                                    int deg = (i - m->rootNote + N) % N; // Rotate degrees so root appears first
                                    int displayIndex = i - s1; // Display index relative to this submenu
                                    std::string label = getNoteName(deg, 0, N, displayIndex); // Use 0 as root for display since we already rotated
                                    bool enabled = (i < (int)mask.size()) ? mask[i] : false;
                                    
                                    sm2->addChild(rack::createCheckMenuItem(label, "", 
                                        [enabled]{ return enabled; }, 
                                        [i, getCurrentMask, setCurrentMask]{
                                            auto mask = getCurrentMask();
                                            if (i < (int)mask.size()) {
                                                mask[i] = 1 - mask[i]; // toggle
                                                setCurrentMask(mask);
                                            }
                                        }));
                                }
                            }));
                        smDeg->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", s2, e2), "", 
                            [m, N, getCurrentMask, setCurrentMask, getNoteName, s2, e2](rack::ui::Menu* sm2){
                                auto mask = getCurrentMask();
                                for (int i = s2; i <= e2; ++i) {
                                    int deg = (i - m->rootNote + N) % N; // Rotate degrees so root appears first
                                    int displayIndex = i - s2; // Display index relative to this submenu
                                    std::string label = getNoteName(deg, 0, N, displayIndex); // Use 0 as root for display since we already rotated
                                    bool enabled = (i < (int)mask.size()) ? mask[i] : false;
                                    
                                    sm2->addChild(rack::createCheckMenuItem(label, "", 
                                        [enabled]{ return enabled; }, 
                                        [i, getCurrentMask, setCurrentMask]{
                                            auto mask = getCurrentMask();
                                            if (i < (int)mask.size()) {
                                                mask[i] = 1 - mask[i]; // toggle
                                                setCurrentMask(mask);
                                            }
                                        }));
                                }
                            }));
                        smDeg->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", s3, e3), "", 
                            [m, N, getCurrentMask, setCurrentMask, getNoteName, s3, e3](rack::ui::Menu* sm2){
                                auto mask = getCurrentMask();
                                for (int i = s3; i <= e3; ++i) {
                                    int deg = (i - m->rootNote + N) % N; // Rotate degrees so root appears first
                                    int displayIndex = i - s3; // Display index relative to this submenu
                                    std::string label = getNoteName(deg, 0, N, displayIndex); // Use 0 as root for display since we already rotated
                                    bool enabled = (i < (int)mask.size()) ? mask[i] : false;
                                    
                                    sm2->addChild(rack::createCheckMenuItem(label, "", 
                                        [enabled]{ return enabled; }, 
                                        [i, getCurrentMask, setCurrentMask]{
                                            auto mask = getCurrentMask();
                                            if (i < (int)mask.size()) {
                                                mask[i] = 1 - mask[i]; // toggle
                                                setCurrentMask(mask);
                                            }
                                        }));
                                }
                            }));
                    } else if (N > 36) {
                        // For medium systems, split into halves (lower gets extra when odd)
                        int halfLo = (N % 2 == 1) ? ((N + 1) / 2) : (N / 2);
                        int loStart = 0;           int loEnd = halfLo - 1;
                        int hiStart = halfLo;      int hiEnd = N - 1;
                        
                        smDeg->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", loStart, loEnd), "", 
                            [m, N, getCurrentMask, setCurrentMask, getNoteName, loStart, loEnd](rack::ui::Menu* sm2){
                                auto mask = getCurrentMask();
                                for (int i = loStart; i <= loEnd; ++i) {
                                    int deg = (i - m->rootNote + N) % N; // Rotate degrees so root appears first
                                    int displayIndex = i - loStart; // Display index relative to this submenu
                                    std::string label = getNoteName(deg, 0, N, displayIndex); // Use 0 as root for display since we already rotated
                                    bool enabled = (i < (int)mask.size()) ? mask[i] : false;
                                    
                                    sm2->addChild(rack::createCheckMenuItem(label, "", 
                                        [enabled]{ return enabled; }, 
                                        [i, getCurrentMask, setCurrentMask]{
                                            auto mask = getCurrentMask();
                                            if (i < (int)mask.size()) {
                                                mask[i] = 1 - mask[i]; // toggle
                                                setCurrentMask(mask);
                                            }
                                        }));
                                }
                            }));
                        smDeg->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", hiStart, hiEnd), "", 
                            [m, N, getCurrentMask, setCurrentMask, getNoteName, hiStart, hiEnd](rack::ui::Menu* sm2){
                                auto mask = getCurrentMask();
                                for (int i = hiStart; i <= hiEnd; ++i) {
                                    int deg = (i - m->rootNote + N) % N; // Rotate degrees so root appears first
                                    int displayIndex = i - hiStart; // Display index relative to this submenu
                                    std::string label = getNoteName(deg, 0, N, displayIndex); // Use 0 as root for display since we already rotated
                                    bool enabled = (i < (int)mask.size()) ? mask[i] : false;
                                    
                                    sm2->addChild(rack::createCheckMenuItem(label, "", 
                                        [enabled]{ return enabled; }, 
                                        [i, getCurrentMask, setCurrentMask]{
                                            auto mask = getCurrentMask();
                                            if (i < (int)mask.size()) {
                                                mask[i] = 1 - mask[i]; // toggle
                                                setCurrentMask(mask);
                                            }
                                        }));
                                }
                            }));
                    } else {
                        // For small systems, show all degrees directly
                        for (int i = 0; i < N; ++i) {
                            int deg = (i - m->rootNote + N) % N; // Rotate degrees so root appears first
                            int displayIndex = i; // Display index is just the loop index for small systems
                            std::string label = getNoteName(deg, 0, N, displayIndex); // Use 0 as root for display since we already rotated
                            bool enabled = (i < (int)mask.size()) ? mask[i] : false;
                            
                            smDeg->addChild(rack::createCheckMenuItem(label, "", 
                                [enabled]{ return enabled; }, 
                                [i, getCurrentMask, setCurrentMask]{
                                    auto mask = getCurrentMask();
                                    if (i < (int)mask.size()) {
                                        mask[i] = 1 - mask[i]; // toggle
                                        setCurrentMask(mask);
                                    }
                            }));
                    }
                }
                }));
            }));
            
            // ═══════════════════════════════════════════════════════════════════════════════════════
            // TET PRESETS (NON-OCTAVE) - COMPREHENSIVE TUNING SYSTEMS
            // ═══════════════════════════════════════════════════════════════════════════════════════
            menu->addChild(rack::createSubmenuItem("TET presets (non-octave)", "", [m](rack::ui::Menu* sm){
                // Helper function to add TET preset menu items
                auto add = [&](const hi::music::tets::Tet& t){
                    float cents = 1200.f * t.periodOct / (float)t.steps;
                    std::string label = rack::string::f("%s — %d steps / period, %.1f cents/step", t.name, t.steps, cents);
                    sm->addChild(rack::createCheckMenuItem(label, "", 
                        [m, &t]{ 
                            return m->tuningMode == 1 && m->tetSteps == t.steps && 
                                   std::fabs(m->tetPeriodOct - t.periodOct) < 1e-6f; 
                        }, 
                        [m, &t]{ 
                            m->tuningMode = 1; 
                            m->tetSteps = t.steps; 
                            m->tetPeriodOct = t.periodOct; 
                            m->rootNote %= std::max(1, m->tetSteps); 
                        }));
                };
                
                // Carlos family of non-octave temperaments
                sm->addChild(rack::createMenuLabel("Carlos"));
                for (const auto& t : hi::music::tets::carlos()) add(t);
                
                // Experimental/Modern non-octave tunings
                sm->addChild(rack::createMenuLabel("Experimental"));
                for (const auto& t : hi::music::tets::tetExperimental()) add(t);
            }));
            
            // ═══════════════════════════════════════════════════════════════════════════════════════
            // QUANTIZE STRENGTH SLIDER
            // ═══════════════════════════════════════════════════════════════════════════════════════
            // Slider (0..100%) bound directly to m->quantStrength (0..1)
            {
                // Display as percent so users see "Quantize strength: 75 %"
                auto* q = new PercentMenuQuantity(&m->quantStrength, "Quantize strength", /*default*/100.f, /*precision*/3); // precision=3 needed to show 0-100%
                auto* s = new rack::ui::Slider();
                s->quantity = q;
                s->box.size.x = 220.f;                                          // Match other sliders' width for consistent look
                menu->addChild(s);
            }
            
            // ═══════════════════════════════════════════════════════════════════════════════════════
            // QUANTIZE ROUNDING MODE
            // ═══════════════════════════════════════════════════════════════════════════════════════
            // Show current mode in submenu title
            menu->addChild(rack::createSubmenuItem("Round", "", [m](rack::ui::Menu* sm){
                struct Opt { const char* label; int mode; const char* desc; };
                static const Opt opts[] = {
                    {"Directional Snap (default)", 0, "Ceil when rising, floor when falling"},
                    {"Nearest", 1, "Standard nearest note"},
                    {"Up", 2, "Always ceil to next note"},
                    {"Down", 3, "Always floor to previous note"}
                };
                for (const auto& o : opts) {
                    int mode = o.mode; const char* label = o.label; const char* desc = o.desc;
                    sm->addChild(rack::createCheckMenuItem(label, desc, 
                        [m, mode]{ return m->quantRoundMode == mode; }, 
                        [m, mode]{ m->quantRoundMode = mode; }));
                }
            }));
            
            // ═══════════════════════════════════════════════════════════════════════════════════════
            // STICKINESS (CENTS) HYSTERESIS
            // ═══════════════════════════════════════════════════════════════════════════════════════
            menu->addChild(rack::createSubmenuItem("Stickiness (¢)", "", [m](rack::ui::Menu* sm){
                const float presets[] = {0.f, 2.f, 5.f, 7.f, 10.f, 15.f, 20.f};
                for (float v : presets) {
                    sm->addChild(rack::createCheckMenuItem(rack::string::f("%.0f¢", v), "", 
                        [m, v]{ return std::fabs(m->stickinessCents - v) < 1e-3f; }, 
                        [m, v]{ m->stickinessCents = v; }));
                }
                // Info label showing current value; presets above handle selection
                sm->addChild(rack::createMenuLabel(rack::string::f("Current: %.2f¢", m->stickinessCents)));
            }));
            
            // ═══════════════════════════════════════════════════════════════════════════════════════
            // STRUM SETTINGS
            // ═══════════════════════════════════════════════════════════════════════════════════════
            menu->addChild(rack::createSubmenuItem("Strum", "", [m](rack::ui::Menu* sm){
                // Enabled toggle (default OFF). When enabling, auto-set spread to 100ms if unset
                sm->addChild(rack::createCheckMenuItem("Enabled (default off)", "", 
                    [m]{ return m->strumEnabled; }, 
                    [m]{
                        m->strumEnabled = !m->strumEnabled;
                        if (m->strumEnabled) {
                            if (m->strumMs <= 0.f) m->strumMs = 100.f;           // Sensible default so enabling has audible effect
                        } else {
                            m->strumMs = 0.f;
                        }
                    }));
                    
                sm->addChild(new MenuSeparator);
                
                // Strum behavior selection
                sm->addChild(rack::createSubmenuItem("Behavior", "", [m](rack::ui::Menu* sm2){
                    sm2->addChild(rack::createCheckMenuItem("Time-stretch", "", 
                        [m]{ return m->strumType == 0; }, 
                        [m]{ m->strumType = 0; }));
                    sm2->addChild(rack::createCheckMenuItem("Start-delay",  "", 
                        [m]{ return m->strumType == 1; }, 
                        [m]{ m->strumType = 1; }));
                }));
                
                // Strum direction selection
                sm->addChild(rack::createSubmenuItem("Direction", "", [m](rack::ui::Menu* sm2){
                    sm2->addChild(rack::createCheckMenuItem("Up", "",   
                        [m]{ return m->strumMode == 0; }, 
                        [m]{ m->strumMode = 0; }));
                    sm2->addChild(rack::createCheckMenuItem("Down", "", 
                        [m]{ return m->strumMode == 1; }, 
                        [m]{ m->strumMode = 1; }));
                    sm2->addChild(rack::createCheckMenuItem("Random", "", 
                        [m]{ return m->strumMode == 2; }, 
                        [m]{ m->strumMode = 2; }));
                }));
                
                // Strum spread timing presets
                sm->addChild(rack::createSubmenuItem("Spread (ms)", "", [m](rack::ui::Menu* sm2){
                    const float vals[] = {5.f, 10.f, 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f};
                    for (int i = 0; i < 8; ++i) {
                        float v = vals[i];
                        sm2->addChild(rack::createCheckMenuItem(rack::string::f("%.0f", v), "", 
                            [m, v]{ return m->strumEnabled && std::fabs(m->strumMs - v) < 1e-3f; }, 
                            [m, v]{ m->strumEnabled = true; m->strumMs = v; }));
                    }
                }));
            }));
    }
};


Model* modelPolyQuanta = createModel<PolyQuanta, PolyQuantaWidget>("PolyQuanta");