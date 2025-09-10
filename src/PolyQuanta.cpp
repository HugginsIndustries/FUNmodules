/*
***********************************************************************
* PolyQuanta — Module overview
*
* This module provides a 16‑channel, polyphonic slew/glide processor with
* musician‑friendly offset, quantization, and range management. The DSP
* pipeline is:
*   1) Input optionally attenuverted by a global dual‑mode control.
*   2) Per‑channel offset plus optional global offset.
*   3) Pre‑quant range handling around 0 V (Clip or Scale, user‑selectable),
*      with an independent Range Offset to slide the window.
*   4) Musical quantizer (12‑/24‑EDO or custom degrees), with blendable
*      quantize strength for soft snapping and per‑channel enable.
*   5) Slew/glide using shape‑aware rates, with optional pitch‑safe mode,
*      synchronized glides, and strum timing patterns.
*   6) Final safety limiter/clip to ±10 V (hard or soft clip).
*
* Key features
* - Dual‑mode global controls with per‑mode value banks and "always on" flags
*   so Slew‑add and Attenuverter (and Global vs Range offset) can coexist.
* - Musical quantizer: 12/24‑EDO scales, custom masks (optionally root‑follow),
*   global root and scale, and per‑channel octave shift. Quantize strength is
*   blendable from 0%..100%.
* - Robust polyphony: automatic width or forced channel count, per‑channel LED
*   activity, optional mono sum/average.
* - Strum: up/down/random order with time‑stretch or start‑delay behavior.
* - Randomization: scope toggles (slew/offset/shapes), per‑control locks/opt‑in,
*   and a global trigger input/button to randomize.
* - Full JSON persistence for all states and options; sensible defaults.
*
* UI/UX
* - Per‑knob context menus expose randomize locks, per‑channel quantize enable,
*   and octave shift. The module menu groups Output/Controls/Quantization.
* - Typed entry is friendly: volts or semitones where appropriate; attenuverter
*   accepts values like "1" meaning 1x. Double‑click defaults are mode‑aware.
*
* The following comment blocks separate the major sections of the code for
* easier navigation and maintenance. No executable code is altered.
***********************************************************************
*/
#include "plugin.hpp"
#include <cstdio>
#include <cctype>
#include <limits>
#include <fstream>
#include <unordered_set>
#include <set>
#include <map>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>

// Forward declare module class for MOS helpers (incomplete type ok for prototypes)
class PolyQuanta; // defined later in this file
// MOS helpers relocated to core/PolyQuantaCore.* (namespace hi::music::mos)
namespace hi { namespace music { namespace mos {
    // Prototypes requiring full PolyQuanta (definitions later in file unchanged)
    void buildMaskFromCycle(PolyQuanta* mod, int N, const std::vector<int>& pcs, bool followsRoot); // relocated impl uses core helpers
    bool detectCurrentMOS(PolyQuanta* mod, int& mOut, int& gOut); // relocated impl uses core helpers
} } }



// -----------------------------------------------------------------------------
// Inlined helpers
// -----------------------------------------------------------------------------

// (Pure DSP helpers moved to core/PolyQuantaCore.*: hi::consts, hi::dsp::clip, hi::dsp::glide, hi::dsp::range)
#include "core/PolyQuantaCore.hpp"
#include "core/ScaleDefs.hpp" // centralized 12-EDO / 24-EDO scale tables

// (Scale defs moved to core/ScaleDefs.* — single source of truth)

// (Curated EDO/TET preset groups moved to core/EdoTetPresets.*)
#include "core/EdoTetPresets.hpp"

// (Range helpers moved to core)

// (Relocated verbatim) strum helpers now in core/Strum.* (hi::dsp::strum)
#include "core/Strum.hpp"

// poly::processWidth relocated to core/PolyQuantaCore.* (hi::dsp::poly::processWidth)

// (Relocated verbatim) ExpTimeQuantity now in core/ui/Quantities.* (hi::ui)
#include "core/ui/Quantities.hpp"

// (Relocated verbatim) menu helpers now in core/ui/MenuHelpers.hpp
#include "core/ui/MenuHelpers.hpp"

// (Relocated verbatim) ShapeQuantity in core/ui/Quantities.*


// (Relocated verbatim) SemitoneVoltQuantity in core/ui/Quantities.*

namespace hi { namespace ui { namespace led {
static inline void setBipolar(rack::engine::Light& g, rack::engine::Light& r, float val, float dt) {
    float gs = rack::clamp( val / hi::consts::LED_SCALE_V, 0.f, 1.f);
    float rs = rack::clamp(-val / hi::consts::LED_SCALE_V, 0.f, 1.f);
    g.setBrightnessSmooth(gs, dt); r.setBrightnessSmooth(rs, dt);
}
}}} // namespace hi::ui::led

// Overlay exporter moved to core/PanelExport.* (a backwards-compatible inline
// forwarder for hi::ui::overlay::exportOverlay now lives in PanelExport.hpp).
#include "core/PanelExport.hpp" // panel snapshot + overlay exporters

namespace hi { namespace ui { namespace dual {
template<typename T>
struct DualBank { T a{}; T b{}; bool mode = false; void syncOnToggle(float& knobVal) const { knobVal = mode ? (float)b : (float)a; } };
struct AttenuverterMap { static inline float rawToGain(float raw) { raw = rack::clamp(raw, 0.f, 1.f); return -10.f + 20.f * raw; } static inline float gainToRaw(float g) { float r = (g + 10.f) / 20.f; return rack::clamp(r, 0.f, 1.f); } };
}}} // namespace hi::ui::dual

namespace hi { namespace util { namespace rnd {
static inline float delta(float width) { return (2.f * rack::random::uniform() - 1.f) * width; }
static inline void randSpanClamp(float& v, float lo, float hi, float maxPct) { float span = hi - lo; if (span <= 0.f) return; float dv = delta(maxPct * span); v = rack::clamp(v + dv, lo, hi); }
}}} // namespace hi::util::rnd

namespace hi { namespace util { namespace jsonh {
static inline void writeBool(::json_t* root, const char* key, bool value) { json_object_set_new(root, key, json_boolean(value)); }
static inline bool readBool(::json_t* root, const char* key, bool def) { if (!root) return def; if (auto* j = json_object_get(root, key)) { return json_boolean_value(j); } return def; }
}}} // namespace hi::util::jsonh

// (QuantConfig + snapEDO moved to core/PolyQuantaCore.*)

namespace hi { namespace dsp { namespace polytrans {
enum Phase { TRANS_STABLE = 0, TRANS_FADE_OUT, TRANS_FADE_IN };
struct State { int curProcN = 0; int curOutN = 0; int pendingProcN = 0; int pendingOutN = 0; float polyRamp = 1.f; Phase transPhase = TRANS_STABLE; bool initToTargetsOnSwitch = false; };
}}} // namespace hi::dsp::polytrans
using namespace rack;
using namespace rack::componentlibrary;
namespace hconst = hi::consts;
// Import transition phase enum values for brevity in this file
using namespace hi::dsp::polytrans;

// Phase 3D forward declarations for CoreState glue (definitions placed after
// full PolyQuanta type to avoid incomplete-type member access errors).
static void fillCoreStateFromModule(const struct PolyQuanta& m, hi::dsp::CoreState& cs) noexcept;
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
 - Declares Rack params, inputs, outputs, and lights.
 - Holds runtime state for per‑channel slews, last outputs, randomization,
     quantization settings, dual‑mode banks/flags, strum data, and options.
 - Provides utility methods (quantizeToScale, currentClipLimit) used by DSP
     and menu logic.
-----------------------------------------------------------------------
*/
struct PolyQuanta : Module {
    // Enums including a 32-slot light bank for bipolar LEDs
    enum ParamId {
        SL1_PARAM, SL2_PARAM, OFF1_PARAM, OFF2_PARAM,
        SL3_PARAM, SL4_PARAM, OFF3_PARAM, OFF4_PARAM,
        SL5_PARAM, SL6_PARAM, OFF5_PARAM, OFF6_PARAM,
        SL7_PARAM, SL8_PARAM, OFF7_PARAM, OFF8_PARAM,
        SL9_PARAM, SL10_PARAM, OFF9_PARAM, OFF10_PARAM,
        SL11_PARAM, SL12_PARAM, OFF11_PARAM, OFF12_PARAM,
        SL13_PARAM, SL14_PARAM, OFF13_PARAM, OFF14_PARAM,
        SL15_PARAM, SL16_PARAM, OFF15_PARAM, OFF16_PARAM,
        // Per-channel output quantize toggles (shared by two buttons per channel)
        QZ1_PARAM, QZ2_PARAM, QZ3_PARAM, QZ4_PARAM,
        QZ5_PARAM, QZ6_PARAM, QZ7_PARAM, QZ8_PARAM,
        QZ9_PARAM, QZ10_PARAM, QZ11_PARAM, QZ12_PARAM,
        QZ13_PARAM, QZ14_PARAM, QZ15_PARAM, QZ16_PARAM,
    RISE_SHAPE_PARAM,
        FALL_SHAPE_PARAM,
    RND_PARAM,
    // Auto-randomize controls
    RND_TIME_PARAM,
    RND_AMT_PARAM,
    RND_AUTO_PARAM,
    RND_SYNC_PARAM,
    GLOBAL_SLEW_PARAM,
    GLOBAL_SLEW_MODE_PARAM,   // 0=Slew add (time), 1=Attenuverter (gain)
    GLOBAL_OFFSET_PARAM,
    GLOBAL_OFFSET_MODE_PARAM, // 0=Global offset, 1=Range offset
        PARAMS_LEN
    };
    enum InputId { IN_INPUT, RND_TRIG_INPUT, INPUTS_LEN  };
    enum OutputId { OUT_OUTPUT, OUTPUTS_LEN };

    // 2 light channels per voice: + (green) and – (red)
    enum LightId { ENUMS(CH_LIGHT, 32), LIGHTS_LEN };

    // Per-voice slew units 
    dsp::SlewLimiter slews[16];
    // Per-channel normalization for shape & timing
    float stepNorm[16] = {10.f};  // current step magnitude (V), defaults to 10 V
    int   stepSign[16] = {0};     // sign of current error (+1 / −1)
    dsp::BooleanTrigger rndBtnTrig;
    dsp::SchmittTrigger rndGateTrig;

    // Cache last rates to avoid redundant setRiseFall() calls
    float prevRiseRate[16] = {0};
    float prevFallRate[16] = {0};
    float lastOut[16]      = {0};

    // Options
    // Output channels selection: 0 = Auto (match input), otherwise force N channels (1..16)
    int forcedChannels = 0;
    bool sumToMonoOut = false;     // when true, sum post-slew/offset to mono
    bool avgWhenSumming = false;   // when true, average instead of plain sum
    bool pitchSafeGlide = false;   // normalize step/time in semitones (1 V/oct)
    // --- Added: Glide normalization enum/state (A.1) ---
    enum class GlideNorm : int { VoltsLinear = 0, CentLinear = 1, StepSafe = 2 };
    int  glideNorm = static_cast<int>(GlideNorm::VoltsLinear); // legacy default for patches without a key
    bool glideNormEnabled = false; // Master toggle: OFF = equal-time legacy (distance ignored for duration)
    // Baseline tracking for equal-time vs normalized modes
    float normUnitAtStep[16] = {0};   // cached unit size (V) at step start (for StepSafe/CentLinear)
    float baseJumpV[16]      = {0};   // total volts to traverse for current glide (target - start)
    int   prevGlideNorm = -1;         // detect mode change to reset baselines
    bool  prevGlideNormEnabled = false; // detect enable toggle transitions
    // Comment: VoltsLinear = constant seconds-per-volt (legacy); CentLinear = constant seconds-per-semitone (1 V = 12 semitones);
    // StepSafe = constant seconds-per-step for active EDO/TET (ΔV = periodOct / N).
    bool softClipOut    = false;   // Soft-clip both Range (Clip mode) and the final safety clip; off = hard clamp
    // Output clip level selector: 0=20 Vpp (±10 V, default), 1=15 Vpp (±7.5 V), 2=10 Vpp (±5 V),
    // 3=5 Vpp (±2.5 V), 4=2 Vpp (±1 V), 5=1 Vpp (±0.5 V)
    int clipVppIndex = 0;
    // Pre-quant range enforcement: 0 = Clip (default), 1 = Scale
    int rangeMode = 0;
    // Per-channel offset quantization mode: 0=None, 1=Semitones (EDO/TET), 2=Cents
    int  quantizeOffsetModeCh[16] = {0};
    // Global convenience ("Quantize all offsets" submenu) applies this mode to all channels; kept for backward compat
    int  quantizeOffsetMode = 0;
    bool syncGlides = false;       // when true, start new glides in lock-step across channels (default OFF)

    // Dual-mode globals: per-mode banks and last modes (centralized helper)
    hi::ui::dual::DualBank<float> gSlew;    // a: slew-add raw [0..1], b: attenuverter raw [0..1]
    hi::ui::dual::DualBank<float> gOffset;  // a: global offset [-10..10], b: range offset [-5..5]
    // Always-on flags (allow applying both modes simultaneously)
    bool  attenuverterAlwaysOn = true;     // apply attenuverter gain even when knob is in Slew-add mode
    bool  slewAddAlwaysOn      = true;     // apply additional slew time even when knob is in Attenuverter mode
    bool  globalOffsetAlwaysOn = true;     // apply global offset even when knob is in Range offset mode
    bool  rangeOffsetAlwaysOn  = true;     // apply range center offset even when knob is in Global offset mode

    // Optional strum
    bool strumEnabled = false;     // enable strum timing offsets (default OFF)
    int  strumMode = 0;            // 0=Up (1..N), 1=Down (N..1), 2=Random
    // Strum behavior (default = Start-delay):
    //   0 = Time-stretch (adds per-channel delay to effective glide time)
    //   1 = Start-delay (holds start by per-channel delay, glide duration unchanged)
    int  strumType = 1;
    float strumMs = 0.f;           // delay between adjacent channels in ms (0 = none)
    // Per-channel assigned delay for the current glide event, and remaining time for start-delay mode
    float strumDelayAssigned[16] = {0};
    float strumDelayLeft[16] = {0};

    bool prevPitchSafeGlide = false; // track mode changes
    bool migratedQZ = false;         // one-time migration from old button params

    // Quantizer position in the signal chain (new)
    enum QuantizerPos { Pre = 0, Post = 1 };
    int quantizerPos = QuantizerPos::Post; // default for NEW instances
    // Signal-chain: Pre=Quantize→Slew (legacy), Post=Slew→Quantize (pitch-accurate).

    // Quantization strength (0..1), 1 = hard snap, 0 = off (when per-channel QZ enabled)
    float quantStrength = 1.f;
    // Quantization rounding mode:
    // 0 = Directional Snap (ceil when rising, floor when falling),
    // 1 = Nearest (current behavior), 2 = Up (ceil), 3 = Down (floor)
    int quantRoundMode = 0;
    // Global cents-based hysteresis (stickiness) to reduce flip-flop on slow slides.
    // User range 0..20 cents (clamped further at runtime per EDO so it cannot exceed 40% of one step).
    float stickinessCents = 5.f;

    // Tuning system
    // tuningMode: 0 = EDO (equal divisions of the octave), 1 = TET (non-octave equal temperament)
    int tuningMode = 0;
    // EDO (octave-based)
    int edo = 12; // default 12-EDO; menu offers curated presets and more
    // TET (non-octave equal temperament): steps per period and period size in octaves (log2 of the ratio)
    int   tetSteps    = 9;                         // Carlos Alpha default (9 divisions of the fifth)
    float tetPeriodOct = std::log2(3.f/2.f);       // perfect fifth period ≈ 0.5849625 octaves
    // Use a custom scale mask instead of predefined scales (per EDO)
    bool useCustomScale = false;
    // When false (default), enabling custom scale seeds the custom mask from the currently selected named scale
    bool rememberCustomScale = false;
    // When true (default), interpret custom scale bits as degrees relative to the root,
    // so the custom scale moves with root changes. When false, bits are absolute PCs.
    bool customScaleFollowsRoot = true;
    uint32_t customMask12 = 0xFFFu;                 // 12 bits set
    uint32_t customMask24 = 0xFFFFFFu;              // 24 bits set
    // Generic custom mask for arbitrary N (not limited to 12/24)
    // 0/1 flag per degree; used when tuning uses N other than 12 or 24.
    std::vector<uint8_t> customMaskGeneric;

    // MOS detection cache (UI only; avoids recomputation when menu opened repeatedly)
    struct MOSCache {
        bool     valid       = false; // cache populated
        bool     found       = false; // did we detect a MOS previously
        int      N           = 0;     // divisions (edo or tetSteps)
        int      m           = 0;     // MOS size
        int      g           = 0;     // MOS generator
        int      tuningMode  = 0;     // 0=EDO 1=TET
        int      edo         = 0;
        int      tetSteps    = 0;
        int      rootNote    = 0;
        bool     useCustom   = false;
        bool     followsRoot = false;
        uint64_t maskHash    = 0;     // fingerprint of scale mask + mode bits
    } mosCache;

    void invalidateMOSCache() { mosCache.valid = false; }
    // Stable fingerprint for active mask (when useCustomScale). Includes bits + flags.
    uint64_t hashMask(int N) const {
        uint64_t h = 1469598103934665603ull; // FNV-1a basis
        auto fnv1a = [&h](uint64_t v){ h ^= v; h *= 1099511628211ull; };
        fnv1a((uint64_t)N);
        fnv1a((uint64_t)useCustomScale);
        fnv1a((uint64_t)customScaleFollowsRoot);
        fnv1a((uint64_t)rootNote);
        if (!useCustomScale) {
            fnv1a(0xFFFFFFFFull);
            return h;
        }
        if (N==12) {
            fnv1a((uint64_t)customMask12);
        } else if (N==24) {
            fnv1a((uint64_t)customMask24);
        } else {
            size_t len = customMaskGeneric.size();
            for(size_t i=0;i<std::min<size_t>(len,(size_t)N);++i) fnv1a((uint64_t)(customMaskGeneric[i]&1));
            fnv1a((uint64_t)len);
        }
        return h;
    }

    // Randomize scope options
    bool randSlew = true;
    bool randOffset = true;
    bool randShapes = true;
    // Max randomize delta as fraction of full control range (0.1..1.0)
    float randMaxPct = 1.f;
    // Auto-randomize state (timed / clocked)
    bool rndAutoEnabled = false;      // cached from RND_AUTO_PARAM
    bool rndSyncMode = false;         // cached from RND_SYNC_PARAM (true = Sync/clock mode, false = Trig/free time)
    dsp::SchmittTrigger rndClockTrig; // separate trigger for measuring external clock (does not itself randomize)
    float rndTimerSec = 0.f;          // accumulator for next scheduled randomize (free mode)
    float rndClockPeriodSec = -1.f;   // smoothed measured clock period
    float rndClockLastEdge = -1.f;    // last rising edge absolute time
    bool  rndClockReady = false;      // at least two edges measured
    float rndAbsTimeSec = 0.f;        // running absolute time for edge timing
    // Per-mode raw knob memory (so switching modes recalls last position)
    float rndTimeRawFree = 0.5f;      // free (Trig) mode raw 0..1
    float rndTimeRawSync = 0.5f;      // sync (clock) mode raw 0..1
    float rndTimeRawLoaded = 0.5f;    // legacy single stored value
    bool  prevRndSyncMode = false;    // detect mode changes
    float rndNextFireTime = -1.f;     // absolute time of next scheduled randomize (sync mode)
    // Divider / Multiplier scheduling (sync mode)
    int   rndDivCounter = 0;          // counts incoming edges for division
    int   rndCurrentDivide = 1;       // active division factor (>1 when dividing)
    int   rndCurrentMultiply = 1;     // active multiplication factor (>1 when multiplying)
    int   rndMulIndex = 0;            // next subdivision index (0..rndCurrentMultiply-1)
    float rndMulBaseTime = -1.f;      // edge time anchoring current multiplication window
    float rndMulNextTime = -1.f;      // next scheduled subdivision time (absolute seconds)
    int   rndPrevRatioIdx = -1;       // detect knob ratio changes to reset phase

    // Per-control randomize locks
    bool lockSlew[16] = {false};
    bool lockOffset[16] = {false};
    bool lockRiseShape = false;
    bool lockFallShape = false;
    // Per-control randomize allows (opt-in when Scope is OFF)
    bool allowSlew[16] = {false};
    bool allowOffset[16] = {false};
    bool allowRiseShape = false;
    bool allowFallShape = false;

    // Map channel index -> your interleaved enum IDs
	static const int SL_PARAM[16];
	static const int OFF_PARAM[16];
    static const int QZ_PARAM[16];

    // Per-channel quantize enabled (replaces front-panel buttons)
    bool qzEnabled[16] = {false};
    // Previous pre-quant (relative) voltage per channel for Directional Snap
    float prevYRel[16] = {0.f};
    // Latched quantizer step per channel (integer index 0..N-1) and init flags
    // FIX: Directional Snap state
    double lastFs[16] = {0.0};
    int    lastDir[16] = {0};  // -1=down, 0=hold, +1=up
    int  latchedStep[16];
    bool latchedInit[16];
    // Track last-applied quantizer config to know when to invalidate latches
    int prevRootNote = -999;
    int prevScaleIndex = -999;
    int prevEdo = -999;
    int prevTetSteps = -999;
    float prevTetPeriodOct = -999.f;
    int prevTuningMode = -999;
    bool prevUseCustomScale = false;
    bool prevCustomFollowsRoot = false;
    uint32_t prevCustomMask12 = 0;
    uint32_t prevCustomMask24 = 0;

    // Per-channel post-quantizer octave shift (-5..+5 octaves; 0 default)
    int postOctShift[16] = {0};

    // Global quantization settings
    int rootNote = 0;      // index 0..(edo-1). For 12‑EDO: 0=C, 1=C#, ..., 11=B
    int scaleIndex = 0;    // index into preset scales table for current EDO (ignored if useCustomScale)

    // Preset scale data is inlined above (hi::music::scales12()/scales24()).

    // -------------------------------------------------------------------
    // Channel switching fade state (pop-free transitions)
    // - When desired channel count changes, fade OUT -> switch -> fade IN.
    // - Fade time is configurable via menu (default 100 ms), persisted to JSON.
    // -------------------------------------------------------------------
    // Polyphony channel transition state (centralized helper)
    hi::dsp::polytrans::State polyTrans;
    // Fade duration in seconds for channel-width transitions (0 = instant)
    float polyFadeSec = 0.1f; // 100 ms default

/*
-------------------------------------------------------------------
    Utility: Musical quantizer and range map
    - quantizeToScale(): snap a voltage (1 V/oct) to the active EDO scale,
        supporting named scales or custom masks and optional bounds.
    - currentClipLimit(): maps the selected Vpp choice to ±limit in volts.
-------------------------------------------------------------------
*/
    // Quantize a voltage to current root/scale (1 V/oct), honoring EDO and custom masks.
    // If boundToLimit is true, restrict the snapped step within ±boundLimit volts.
    float quantizeToScale(float v, int shiftSteps = 0, float boundLimit = 10.f, bool boundToLimit = false) const {
        hi::dsp::QuantConfig qc;
        if (tuningMode == 0) {
            qc.edo = (edo <= 0) ? 12 : edo;
            qc.periodOct = 1.f; // octave
        } else {
            qc.edo = tetSteps > 0 ? tetSteps : 9;
            qc.periodOct = (tetPeriodOct > 0.f) ? tetPeriodOct : std::log2(3.f/2.f);
        }
        qc.root = rootNote;
        qc.useCustom = useCustomScale;
        qc.customFollowsRoot = customScaleFollowsRoot;
        qc.customMask12 = customMask12;
        qc.customMask24 = customMask24;
        qc.scaleIndex = scaleIndex;
        // Provide generic mask for arbitrary N (when not 12 or 24)
        if (qc.useCustom && (qc.edo != 12 && qc.edo != 24)) {
            if ((int)customMaskGeneric.size() == qc.edo) {
                qc.customMaskGeneric = customMaskGeneric.data();
                qc.customMaskLen = (int)customMaskGeneric.size();
            } else {
                qc.customMaskGeneric = nullptr;
                qc.customMaskLen = 0;
            }
        }
        return hi::dsp::snapEDO(v, qc, boundLimit, boundToLimit, shiftSteps);
    }

    // Map clipVppIndex to half-range (±limit) in volts
    float currentClipLimit() const { return hi::dsp::range::clipLimitFromIndex(clipVppIndex); }

/*
-------------------------------------------------------------------
    Constructor: Configure params/IO and seed defaults
    - Per‑channel knobs (slew and offset) with helpful tooltips.
    - Global shape controls, dual‑mode global knobs with mode switches,
    and ports. Bypass routing is declared.
    - Seeds per‑mode banks and initializes runtime caches.
-------------------------------------------------------------------
*/
    PolyQuanta() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Per-channel knobs
        for (int i = 0; i < 16; ++i) {
            // Per‑channel offset: show semitones when quantizeOffsetMode == 1
            auto* pq = configParam<OffsetQuantity>(OFF_PARAM[i], -10.f, 10.f, 0.f, string::f("Ch %d offset", i+1), "");
            pq->quantizeOffsetModePtr = &quantizeOffsetModeCh[i];
            pq->edoPtr = &edo;
            // Exponential time taper for slew: store [0,1]; tooltip formats ms/s dynamically
            configParam<hi::ui::ExpTimeQuantity>(SL_PARAM[i], 0.f, 1.f, 0.0f, string::f("Ch %d slew (rise & fall)", i+1), "");
            // Output quantize is controlled via qzEnabled[] in the menu
        }
    for (int i = 0; i < 16; ++i) { latchedInit[i] = false; latchedStep[i] = 0; }
    // Global rise/fall curve: -1 = log-ish, 0 = linear, +1 = expo-ish
    configParam<hi::ui::ShapeQuantity>(RISE_SHAPE_PARAM, -1.f, 1.f, 0.f, "Rise shape");
    configParam<hi::ui::ShapeQuantity>(FALL_SHAPE_PARAM, -1.f, 1.f, 0.f, "Fall shape");
    // Global controls
    struct GlobalSlewDualQuantity : hi::ui::ExpTimeQuantity {
        std::string getDisplayValueString() override {
            auto* m = dynamic_cast<PolyQuanta*>(module);
            float raw = getValue();
            if (m && m->gSlew.mode) {
                // Attenuverter gain: map raw [0..1] -> [-10,+10]
                float g = -10.f + 20.f * rack::math::clamp(raw, 0.f, 1.f);
                return rack::string::f("Attenuverter: %+.2fx", g);
            }
            // Slew add mode: show time based on ExpTimeQuantity mapping
            float sec = hi::ui::ExpTimeQuantity::knobToSec(raw);
            if (sec < 1.f) return rack::string::f("Slew add: %.0f ms", sec * 1000.f);
            return rack::string::f("Slew add: %.2f s", sec);
        }
        void setValue(float v) override {
            // Preserve normal behavior
            hi::ui::ExpTimeQuantity::setValue(v);
        }
        void setDisplayValueString(std::string s) override {
            // When knob is in attenuverter mode, treat typed numbers as gain (x), so "1" => 1x
            auto* m = dynamic_cast<PolyQuanta*>(module);
            if (m && m->gSlew.mode) {
                // Accept optional trailing 'x'/'X', whitespace, and sign
                const char* c = s.c_str();
                char* end = nullptr;
                float g = std::strtof(c, &end);
                // If parse failed, fall back
                if (end == c) { ParamQuantity::setDisplayValueString(s); return; }
                float raw = rack::math::clamp((g + 10.f) / 20.f, 0.f, 1.f);
                setValue(raw);
                return;
            }
            ParamQuantity::setDisplayValueString(s);
        }
        float getDefaultValue() override {
            // Double-click default depends on mode at the time of reset:
            // - Slew add mode: 0 sec additional slew (raw 0.0)
            // - Attenuverter mode: 1.00x gain (raw ~0.55)
            auto* m = dynamic_cast<PolyQuanta*>(module);
            if (m && m->gSlew.mode) return 0.55f;
            return 0.f;
        }
    };
    configParam<GlobalSlewDualQuantity>(GLOBAL_SLEW_PARAM, 0.f, 1.f, 0.0f, "Global Slew (dual)", "");
    struct SlewModeQuantity : ParamQuantity {
    std::string getDisplayValueString() override { return (getValue() > 0.5f) ? "Attenuverter" : "Slew add"; }
    };
    configParam<SlewModeQuantity>(GLOBAL_SLEW_MODE_PARAM, 0.f, 1.f, 0.f, "Global Slew knob mode");
    // Dual-behavior quantity: shows semitones when quantizing offsets in Global mode,
    // but always shows/clamps volts (±5 V) in Range offset mode.
    struct GlobalOffsetDualQuantity : hi::ui::SemitoneVoltQuantity {
        std::string getUnit() override {
            // We embed labels in the value string; leave unit empty to avoid duplication.
            return "";
        }
        std::string getDisplayValueString() override {
            auto* m = dynamic_cast<PolyQuanta*>(module);
            if (m && m->gOffset.mode) {
                // Range offset mode: show volts explicitly with a clear label
                float v = getValue();
                return rack::string::f("Range offset: %.2f V", v);
            }
            // Global offset mode: reuse OffsetQuantity formatting (st or V), but label it
        return std::string("Global offset: ") + hi::ui::SemitoneVoltQuantity::getDisplayValueString();
        }
        void setDisplayValueString(std::string s) override {
            auto* m = dynamic_cast<PolyQuanta*>(module);
            if (m && m->gOffset.mode) {
                // Range offset active on knob: parse as volts directly. Typing "-5" => -5 V
                const char* c = s.c_str();
                char* end = nullptr;
                float v = std::strtof(c, &end);
                // If number not found, fall back to base behavior
                if (end == c) {
            hi::ui::SemitoneVoltQuantity::setDisplayValueString(s);
                } else {
                    setValue(clamp(v, -5.f, 5.f));
                }
                return;
            }
            // Otherwise, use base parsing (volts or semitones depending on module state)
        hi::ui::SemitoneVoltQuantity::setDisplayValueString(s);
        }
    };
    {
        auto* gq = configParam<GlobalOffsetDualQuantity>(GLOBAL_OFFSET_PARAM, -10.f, 10.f, 0.0f, "Global Offset (dual)", "");
    gq->quantizeOffsetModePtr = &quantizeOffsetMode; // global knob uses batch mode value
        gq->edoPtr = &edo;
    }
    struct OffsetModeQuantity : ParamQuantity {
        std::string getDisplayValueString() override {
            return (getValue() > 0.5f) ? "Range offset" : "Global offset";
        }
    };
    configParam<OffsetModeQuantity>(GLOBAL_OFFSET_MODE_PARAM, 0.f, 1.f, 0.f, "Global Offset knob mode");
		// Input and output ports (only 1 each, poly)
		configInput(IN_INPUT,  "Poly signal");                          // shown on hover
        configInput(RND_TRIG_INPUT, "Randomize trigger (gate)");	    // shown on hover
		configOutput(OUT_OUTPUT, "Poly signal (slewed + offset)");      // shown on hover

    // Seed dual-mode banks (slew bank1 default to 1x -> raw 0.55)
    gSlew.a = params[GLOBAL_SLEW_PARAM].getValue();
    gSlew.b = 0.55f; // 1.0x gain
    gSlew.mode = false; // start in Slew-add mode
    gOffset.a = params[GLOBAL_OFFSET_PARAM].getValue();
    gOffset.b = 0.f; // centered range offset
    gOffset.mode = false; // start in Global offset mode

        // When the module is bypassed in Rack, pass IN → OUT
        configBypass(IN_INPUT, OUT_OUTPUT);
        // Momentary button (edge-detected in process)
        configParam(RND_PARAM, 0.f, 1.f, 0.f, "Randomize");
    // Auto-randomize ParamQuantities (local structs so PolyQuanta is complete)
    struct RandomTimeQuantity : rack::engine::ParamQuantity {
        static float rawToSec(float r){ const float mn=0.001f,mx=10000.f; float lmn=std::log10(mn), lmx=std::log10(mx); float lx=lmn + rack::clamp(r,0.f,1.f)*(lmx-lmn); return std::pow(10.f,lx); }
        static float secToRaw(float s){ const float mn=0.001f,mx=10000.f; s=rack::clamp(s,mn,mx); float lmn=std::log10(mn), lmx=std::log10(mx); return (std::log10(s)-lmn)/(lmx-lmn); }
        std::string getDisplayValueString() override {
            auto* m = dynamic_cast<PolyQuanta*>(module); bool syncMode = m ? m->rndSyncMode : false; float r=getValue();
            if(syncMode){
                // 64..2 divides (left), center 1x, 2..64 multiplies (right)
                const int DIV_MAX = 64;
                const int TOTAL = (DIV_MAX-1) + 1 + (DIV_MAX-1); // 127
                int idx = (int)std::lround(rack::clamp(r,0.f,1.f)*(TOTAL-1)); // 0..126
                if(idx < (DIV_MAX-1)) { int d = DIV_MAX - idx; return rack::string::f("÷%d", d); }
                if(idx == (DIV_MAX-1)) return std::string("1×");
                int mfac = (idx - (DIV_MAX-1)) + 1; // starts at 2
                return rack::string::f("×%d", mfac);
            }
            float sec=rawToSec(r); if(sec<10.f) return rack::string::f("%.2f ms", sec*1000.f); return rack::string::f("%.2f s", sec);
        }
        void setDisplayValueString(std::string s) override {
            auto* m = dynamic_cast<PolyQuanta*>(module); bool syncMode = m ? m->rndSyncMode : false; std::string t=s; for(char& c:t) c=std::tolower((unsigned char)c);
            if(syncMode){
                // Accept forms: -N (divide), ÷N, xN, ×N, N (multiply), 1
                const int DIV_MAX = 64;
                const int TOTAL = (DIV_MAX-1)+1+(DIV_MAX-1);
                auto trim=[&](std::string& x){ while(!x.empty()&&isspace((unsigned char)x.front())) x.erase(x.begin()); while(!x.empty()&&isspace((unsigned char)x.back())) x.pop_back(); };
                trim(t);
                // Normalize UTF-8 symbols (÷ U+00F7, × U+00D7) to ASCII tokens to avoid multi-character literal warnings
                auto normalizeSymbols = [](std::string& u){
                    std::string out; out.reserve(u.size());
                    for(size_t i=0;i<u.size();) {
                        unsigned char c0 = (unsigned char)u[i];
                        if(c0==0xC3 && i+1<u.size()) { // possible two-byte UTF-8 sequence
                            unsigned char c1 = (unsigned char)u[i+1];
                            if(c1==0xB7) { out.push_back('/'); i+=2; continue; } // ÷
                            if(c1==0x97) { out.push_back('x'); i+=2; continue; } // ×
                        }
                        out.push_back(u[i]); ++i;
                    }
                    u.swap(out);
                };
                normalizeSymbols(t);
                if(t=="1"||t=="1x"||t=="1*"||t=="1/1") { setValue((float)(DIV_MAX-1)/(TOTAL-1)); return; }
                // Extract optional sign and digits
                int sign=1; size_t pos=0;
                if(!t.empty() && (t[0]=='-'||t[0]=='+')) {
                    if(t[0]=='-') sign=-1;
                    pos=1;
                }
                // Explicit divide marker: '/', 'd'
                if(pos < t.size() && (t[pos]=='/' || t[pos]=='d')) { sign=-1; ++pos; }
                // Explicit multiply markers: leading 'x' or '*'
                if(pos < t.size() && (t[pos]=='x' || t[pos]=='*')) { ++pos; }
                std::string digits; for(; pos<t.size(); ++pos){ if(isdigit((unsigned char)t[pos])) digits.push_back(t[pos]); else break; }
                if(digits.empty()){ return; }
                int val=0; try { val = std::stoi(digits); } catch(...) { return; }
                if(sign<0) { // division factor
                    if(val < 2) return;
                    if(val > DIV_MAX) val = DIV_MAX;
                    int idx = DIV_MAX - val; // 64..2 -> 0..62
                    setValue((float)idx/(TOTAL-1));
                    return;
                }
                if(val==1) {
                    setValue((float)(DIV_MAX-1)/(TOTAL-1));
                    return;
                }
                if(val >= 2) {
                    if(val > DIV_MAX) val = DIV_MAX;
                    int idx = (DIV_MAX-1) + (val-1); // 2..64 -> 63..126
                    setValue((float)idx/(TOTAL-1));
                    return;
                }
                return; }
            bool ms=false; if(t.find("ms")!=std::string::npos){ ms=true; t.erase(t.find("ms")); } if(t.find("s")!=std::string::npos){ ms=false; t.erase(t.find("s")); } try { float v=std::stof(t); if(ms) v/=1000.f; setValue(secToRaw(v)); } catch(...) {}
        }
    };
    struct PercentQuantity : rack::engine::ParamQuantity {
        std::string getDisplayValueString() override { return rack::string::f("%.0f%%", getValue()*100.f); }
        void setDisplayValueString(std::string s) override { std::string t=s; for(char& c:t) c=std::tolower((unsigned char)c); if(t.find('%')!=std::string::npos) t.erase(t.find('%')); try { float v=std::stof(t)/100.f; setValue(rack::clamp(v,0.f,1.f)); } catch(...) {} }
    };
    configParam<RandomTimeQuantity>(RND_TIME_PARAM, 0.f, 1.f, 0.5f, "Time");
    configParam<PercentQuantity>(RND_AMT_PARAM, 0.f, 1.f, 1.f, "Amount");
    configParam(RND_AUTO_PARAM, 0.f, 1.f, 0.f, "Auto (On/Off)");
    configParam(RND_SYNC_PARAM, 0.f, 1.f, 0.f, "Sync (Sync/Trig)");
        // init step tracking
        for (int i = 0; i < 16; ++i) {
            stepNorm[i] = 10.f;
            stepSign[i] = 0;
            prevRiseRate[i] = -1.f;
            prevFallRate[i]  = -1.f;
        }
	}

/*
-------------------------------------------------------------------
    Persistence: dataToJson()
    - Saves all module options, dual‑mode states/banks, quantization
        settings, per‑channel toggles and octave shifts, and randomize
        locks/allows. Keys are stable for forward/backward compatibility.
-------------------------------------------------------------------
*/
        json_t* dataToJson() override {
    json_t* rootJ = json_object();
    // Persist forced channel count (0 = Auto)
    json_object_set_new(rootJ, "forcedChannels", json_integer(forcedChannels));
    hi::util::jsonh::writeBool(rootJ, "sumToMonoOut",    sumToMonoOut);
    hi::util::jsonh::writeBool(rootJ, "avgWhenSumming",  avgWhenSumming);
    hi::util::jsonh::writeBool(rootJ, "pitchSafeGlide",  pitchSafeGlide);
    // (A.2 updated) Persist glide normalization enum + master enable (no legacy pitchSafe key now)
    json_object_set_new(rootJ, "glideNorm", json_integer(glideNorm)); // 0/1/2
    hi::util::jsonh::writeBool(rootJ, "glideNormEnabled", glideNormEnabled); // Master normalization toggle
    hi::util::jsonh::writeBool(rootJ, "softClipOut",     softClipOut);
    json_object_set_new(rootJ, "clipVppIndex", json_integer(clipVppIndex));
    json_object_set_new(rootJ, "rangeMode", json_integer(rangeMode));
    // Persist new enum; also write legacy boolean for backward compatibility (true only if semitone mode)
    json_object_set_new(rootJ, "quantizeOffsetMode", json_integer(quantizeOffsetMode)); // legacy/global
    // Per-channel modes
    {
        json_t* arr = json_array();
        for (int i=0;i<16;++i) json_array_append_new(arr, json_integer(quantizeOffsetModeCh[i]));
        json_object_set_new(rootJ, "quantizeOffsetModeCh", arr);
    }
    hi::util::jsonh::writeBool(rootJ, "quantizeOffsets", quantizeOffsetMode == 1);
    hi::util::jsonh::writeBool(rootJ, "syncGlides",      syncGlides);
    // Dual-mode globals
    // Persist using legacy keys for backward compatibility
    hi::util::jsonh::writeBool(rootJ, "globalSlewMode", gSlew.mode);
    json_object_set_new(rootJ, "globalSlewBank0", json_real(gSlew.a));
    json_object_set_new(rootJ, "globalSlewBank1", json_real(gSlew.b));
    hi::util::jsonh::writeBool(rootJ, "globalOffsetMode", gOffset.mode);
    json_object_set_new(rootJ, "globalOffsetBank0", json_real(gOffset.a));
    json_object_set_new(rootJ, "globalOffsetBank1", json_real(gOffset.b));
    // Always-on flags for dual-mode knobs
    hi::util::jsonh::writeBool(rootJ, "attenuverterAlwaysOn", attenuverterAlwaysOn);
    hi::util::jsonh::writeBool(rootJ, "slewAddAlwaysOn",      slewAddAlwaysOn);
    hi::util::jsonh::writeBool(rootJ, "globalOffsetAlwaysOn", globalOffsetAlwaysOn);
    hi::util::jsonh::writeBool(rootJ, "rangeOffsetAlwaysOn",  rangeOffsetAlwaysOn);
    hi::util::jsonh::writeBool(rootJ, "strumEnabled",    strumEnabled);
    json_object_set_new(rootJ, "strumMode", json_integer(strumMode));
    json_object_set_new(rootJ, "strumType", json_integer(strumType));
    json_object_set_new(rootJ, "strumMs", json_real(strumMs));
    hi::util::jsonh::writeBool(rootJ, "randSlew",        randSlew);
    hi::util::jsonh::writeBool(rootJ, "randOffset",      randOffset);
    hi::util::jsonh::writeBool(rootJ, "randShapes",      randShapes);
    json_object_set_new(rootJ, "randMaxPct", json_real(randMaxPct));
    hi::util::jsonh::writeBool(rootJ, "rndAutoEnabled", rndAutoEnabled);
    hi::util::jsonh::writeBool(rootJ, "rndSyncMode", rndSyncMode);
    // Persist per-mode raw time knob values (new) plus legacy single value for backward compat
    json_object_set_new(rootJ, "rndTimeRawFree", json_real(rndTimeRawFree));
    json_object_set_new(rootJ, "rndTimeRawSync", json_real(rndTimeRawSync));
    json_object_set_new(rootJ, "rndTimeRaw", json_real(params[RND_TIME_PARAM].getValue())); // legacy
        // Per-channel quantize enables
        for (int i = 0; i < 16; ++i) {
            char key[32];
            std::snprintf(key, sizeof(key), "qzEnabled%d", i+1);
            hi::util::jsonh::writeBool(rootJ, key, qzEnabled[i]);
            std::snprintf(key, sizeof(key), "postOctShift%d", i+1);
            json_object_set_new(rootJ, key, json_integer(postOctShift[i]));
        }
    // Phase 3D: delegate quant JSON to core (no behavior change)
    {
        hi::dsp::CoreState cs;
        fillCoreStateFromModule(*this, cs);
        hi::dsp::coreToJson(rootJ, cs);
    }
        // Locks
        for (int i = 0; i < 16; ++i) {
            char key[32];
            std::snprintf(key, sizeof(key), "lockSlew%d", i+1);
            hi::util::jsonh::writeBool(rootJ, key, lockSlew[i]);
            std::snprintf(key, sizeof(key), "lockOffset%d", i+1);
            hi::util::jsonh::writeBool(rootJ, key, lockOffset[i]);
            std::snprintf(key, sizeof(key), "allowSlew%d", i+1);
            hi::util::jsonh::writeBool(rootJ, key, allowSlew[i]);
            std::snprintf(key, sizeof(key), "allowOffset%d", i+1);
            hi::util::jsonh::writeBool(rootJ, key, allowOffset[i]);
        }
        hi::util::jsonh::writeBool(rootJ, "lockRiseShape", lockRiseShape);
        hi::util::jsonh::writeBool(rootJ, "lockFallShape", lockFallShape);
        hi::util::jsonh::writeBool(rootJ, "allowRiseShape", allowRiseShape);
        hi::util::jsonh::writeBool(rootJ, "allowFallShape", allowFallShape);
    // (rootNote/scaleIndex now serialized via CoreState above)
    // Polyphony transition fade settings
    json_object_set_new(rootJ, "polyFadeSec", json_real(polyFadeSec));
    // New: persist quantizer position (0=Pre legacy, 1=Post default)
    json_object_set_new(rootJ, "quantizerPos", json_integer(quantizerPos));
    return rootJ;
    }
/*
-------------------------------------------------------------------
    Persistence: dataFromJson()
    - Restores all saved fields; handles legacy keys where applicable.
        Also restores dual‑mode bank values to knobs and modes to switches.
        Performs one‑time migrations guarded by flags as needed.
-------------------------------------------------------------------
*/
        void dataFromJson(json_t* rootJ) override {
    // Back-compat: read legacy forcePolyOut => map to forcedChannels=16 when true
    if (auto* j = json_object_get(rootJ, "forcedChannels")) {
        forcedChannels = (int)json_integer_value(j);
    } else {
        bool legacyForce = hi::util::jsonh::readBool(rootJ, "forcePolyOut", false);
        if (legacyForce) forcedChannels = 16; // legacy behavior
    }
    sumToMonoOut    = hi::util::jsonh::readBool(rootJ, "sumToMonoOut",    sumToMonoOut);
    avgWhenSumming  = hi::util::jsonh::readBool(rootJ, "avgWhenSumming",  avgWhenSumming);
    pitchSafeGlide  = hi::util::jsonh::readBool(rootJ, "pitchSafeGlide",  pitchSafeGlide);
    // (A.2 updated) Determine master enable first: new key -> legacy pitchSafe -> default false
    if (auto* je = json_object_get(rootJ, "glideNormEnabled")) {
        glideNormEnabled = json_is_true(je);
    } else if (auto* jl = json_object_get(rootJ, "pitchSafe")) { // legacy fallback
        glideNormEnabled = json_is_true(jl);
    } else {
        glideNormEnabled = false;
    }
    // Read glideNorm or infer legacy Cent-linear when only pitchSafe existed
    if (auto* jg = json_object_get(rootJ, "glideNorm")) {
        if (json_is_integer(jg)) glideNorm = (int)json_integer_value(jg);
    } else {
        glideNorm = glideNormEnabled ? (int)GlideNorm::CentLinear : (int)GlideNorm::VoltsLinear;
    }
    // Update legacy internal semitone normalization flag when Cent-linear active and enabled
    pitchSafeGlide = (glideNormEnabled && glideNorm == (int)GlideNorm::CentLinear);
    softClipOut     = hi::util::jsonh::readBool(rootJ, "softClipOut",     softClipOut);
    if (auto* j = json_object_get(rootJ, "clipVppIndex")) clipVppIndex = (int)json_integer_value(j);
    if (auto* j = json_object_get(rootJ, "rangeMode")) rangeMode = (int)json_integer_value(j);
    // Read new enum; if absent fall back to legacy boolean
    if (auto* jm = json_object_get(rootJ, "quantizeOffsetMode")) {
        if (json_is_integer(jm)) quantizeOffsetMode = (int)json_integer_value(jm);
    }
    // Per-channel modes (new). If missing, seed from global/legacy.
    bool seededFromLegacy = false;
    if (auto* arr = json_object_get(rootJ, "quantizeOffsetModeCh")) {
        if (json_is_array(arr) && json_array_size(arr) == 16) {
            for (int i=0;i<16;++i) {
                auto* v = json_array_get(arr, i);
                if (v && json_is_integer(v)) quantizeOffsetModeCh[i] = (int)json_integer_value(v);
            }
            seededFromLegacy = true;
        }
    }
    if (!seededFromLegacy) {
        // Legacy paths: old enum or bool
        int legacyMode = quantizeOffsetMode;
        if (legacyMode == 0) {
            bool legacyBool = hi::util::jsonh::readBool(rootJ, "quantizeOffsets", false);
            if (legacyBool) legacyMode = 1;
        }
        for (int i=0;i<16;++i) quantizeOffsetModeCh[i] = legacyMode;
    }
    syncGlides      = hi::util::jsonh::readBool(rootJ, "syncGlides",      syncGlides);
    // Dual-mode globals (migrated to shared DualBank)
    gSlew.mode   = hi::util::jsonh::readBool(rootJ, "globalSlewMode", gSlew.mode);
    if (auto* j = json_object_get(rootJ, "globalSlewBank0")) gSlew.a = (float)json_number_value(j);
    if (auto* j = json_object_get(rootJ, "globalSlewBank1")) gSlew.b = (float)json_number_value(j);
    gOffset.mode = hi::util::jsonh::readBool(rootJ, "globalOffsetMode", gOffset.mode);
    if (auto* j = json_object_get(rootJ, "globalOffsetBank0")) gOffset.a = (float)json_number_value(j);
    if (auto* j = json_object_get(rootJ, "globalOffsetBank1")) gOffset.b = (float)json_number_value(j);
    // Always-on flags for dual-mode knobs
    attenuverterAlwaysOn = hi::util::jsonh::readBool(rootJ, "attenuverterAlwaysOn", attenuverterAlwaysOn);
    slewAddAlwaysOn      = hi::util::jsonh::readBool(rootJ, "slewAddAlwaysOn",      slewAddAlwaysOn);
    globalOffsetAlwaysOn = hi::util::jsonh::readBool(rootJ, "globalOffsetAlwaysOn", globalOffsetAlwaysOn);
    rangeOffsetAlwaysOn  = hi::util::jsonh::readBool(rootJ, "rangeOffsetAlwaysOn",  rangeOffsetAlwaysOn);
    // Ensure knob positions reflect loaded modes
    params[GLOBAL_SLEW_PARAM].setValue(gSlew.mode ? gSlew.b : gSlew.a);
    params[GLOBAL_SLEW_MODE_PARAM].setValue(gSlew.mode ? 1.f : 0.f);
    params[GLOBAL_OFFSET_PARAM].setValue(gOffset.mode ? gOffset.b : gOffset.a);
    params[GLOBAL_OFFSET_MODE_PARAM].setValue(gOffset.mode ? 1.f : 0.f);
    strumEnabled    = hi::util::jsonh::readBool(rootJ, "strumEnabled",    strumEnabled);
    if (auto* j = json_object_get(rootJ, "strumMode")) strumMode = (int)json_integer_value(j);
    if (auto* j = json_object_get(rootJ, "strumType")) strumType = (int)json_integer_value(j);
    if (auto* j = json_object_get(rootJ, "strumMs"))   strumMs    = (float)json_number_value(j);
    randSlew        = hi::util::jsonh::readBool(rootJ, "randSlew",        randSlew);
    randOffset      = hi::util::jsonh::readBool(rootJ, "randOffset",      randOffset);
    randShapes      = hi::util::jsonh::readBool(rootJ, "randShapes",      randShapes);
    if (auto* j = json_object_get(rootJ, "randMaxPct")) randMaxPct = (float)json_number_value(j);
    rndAutoEnabled  = hi::util::jsonh::readBool(rootJ, "rndAutoEnabled", rndAutoEnabled);
    rndSyncMode     = hi::util::jsonh::readBool(rootJ, "rndSyncMode", rndSyncMode);
    if (auto* j = json_object_get(rootJ, "rndTimeRawFree")) rndTimeRawFree = (float)json_number_value(j);
    if (auto* j = json_object_get(rootJ, "rndTimeRawSync")) rndTimeRawSync = (float)json_number_value(j);
    if (auto* j = json_object_get(rootJ, "rndTimeRaw")) rndTimeRawLoaded = (float)json_number_value(j); // legacy single value
    // If new keys were absent, seed both from legacy
    if (rndTimeRawFree < 0.f || rndTimeRawFree > 1.f) rndTimeRawFree = rndTimeRawLoaded;
    if (rndTimeRawSync < 0.f || rndTimeRawSync > 1.f) rndTimeRawSync = rndTimeRawLoaded;
    // Apply loaded param value matching current mode
    if (RND_TIME_PARAM < PARAMS_LEN) params[RND_TIME_PARAM].setValue(rndSyncMode ? rndTimeRawSync : rndTimeRawFree);
    if (RND_AMT_PARAM  < PARAMS_LEN) params[RND_AMT_PARAM].setValue(randMaxPct);
    if (RND_AUTO_PARAM < PARAMS_LEN) params[RND_AUTO_PARAM].setValue(rndAutoEnabled ? 1.f : 0.f);
    if (RND_SYNC_PARAM < PARAMS_LEN) params[RND_SYNC_PARAM].setValue(rndSyncMode ? 1.f : 0.f);
        for (int i = 0; i < 16; ++i) {
            char key[32];
            std::snprintf(key, sizeof(key), "qzEnabled%d", i+1);
            qzEnabled[i] = hi::util::jsonh::readBool(rootJ, key, qzEnabled[i]);
            std::snprintf(key, sizeof(key), "postOctShift%d", i+1);
            if (auto* jv = json_object_get(rootJ, key)) postOctShift[i] = (int)json_integer_value(jv);
        }
    // Phase 3D: delegate quant JSON to core (no behavior change)
    {
        hi::dsp::CoreState cs; // defaults match previous initialization paths
        hi::dsp::coreFromJson(rootJ, cs);
        applyCoreStateToModule(cs, *this);
    }
        for (int i = 0; i < 16; ++i) {
            char key[32];
            std::snprintf(key, sizeof(key), "lockSlew%d", i+1);
            lockSlew[i] = hi::util::jsonh::readBool(rootJ, key, lockSlew[i]);
            std::snprintf(key, sizeof(key), "lockOffset%d", i+1);
            lockOffset[i] = hi::util::jsonh::readBool(rootJ, key, lockOffset[i]);
            std::snprintf(key, sizeof(key), "allowSlew%d", i+1);
            allowSlew[i] = hi::util::jsonh::readBool(rootJ, key, allowSlew[i]);
            std::snprintf(key, sizeof(key), "allowOffset%d", i+1);
            allowOffset[i] = hi::util::jsonh::readBool(rootJ, key, allowOffset[i]);
        }
        lockRiseShape = hi::util::jsonh::readBool(rootJ, "lockRiseShape", lockRiseShape);
        lockFallShape = hi::util::jsonh::readBool(rootJ, "lockFallShape", lockFallShape);
        allowRiseShape = hi::util::jsonh::readBool(rootJ, "allowRiseShape", allowRiseShape);
        allowFallShape = hi::util::jsonh::readBool(rootJ, "allowFallShape", allowFallShape);
    // (rootNote/scaleIndex restored via CoreState above)
    if (auto* j = json_object_get(rootJ, "polyFadeSec")) polyFadeSec = (float)json_number_value(j);
    // New key (quantizerPos). Back-compat: if absent (old patches) force legacy Pre to preserve prior sound.
    if (auto* jq = json_object_get(rootJ, "quantizerPos")) {
        if (json_is_integer(jq)) quantizerPos = (int)json_integer_value(jq);
    } else {
        quantizerPos = QuantizerPos::Pre; // old patches keep legacy chain (Quantize→Slew)
    }
    // One-time migration placeholder
    if (!migratedQZ) {
        for (int i = 0; i < 16; ++i) {
            // If existing JSON has explicit qzEnabled, keep it; otherwise check legacy param if non-zero default was stored (unlikely)
            // No direct param storage across sessions; so nothing to do, just mark migrated to avoid re-evaluating
        }
        migratedQZ = true;
    }
    }

/*
-------------------------------------------------------------------
    Lifecycle: onReset()
    - Clears per‑channel step tracking, cached rates, LEDs, and strum
        delay state. Does not modify user options or saved parameters.
-------------------------------------------------------------------
*/
        void onReset() override {
        for (int i = 0; i < 16; ++i) {
            stepNorm[i] = 10.f;
            stepSign[i] = 0;
            prevRiseRate[i] = -1.f;
            prevFallRate[i]  = -1.f;
            lastOut[i] = 0.f;
            lights[CH_LIGHT + 2*i + 0].setBrightness(0.f);
            lights[CH_LIGHT + 2*i + 1].setBrightness(0.f);
            strumDelayAssigned[i] = 0.f;
            strumDelayLeft[i] = 0.f;
            latchedInit[i] = false;
            latchedStep[i] = 0;
            prevYRel[i] = 0.f;
        }
    // Reset auto-randomize timing state
    rndTimerSec = 0.f;
    rndClockPeriodSec = -1.f;
    rndClockLastEdge = -1.f;
    rndClockReady = false;
    rndAbsTimeSec = 0.f;
    rndNextFireTime = -1.f;
    rndDivCounter = 0; rndCurrentDivide = 1; rndCurrentMultiply = 1; rndMulIndex = 0; rndMulBaseTime = -1.f; rndMulNextTime = -1.f; rndPrevRatioIdx = -1;
    }

/*
-------------------------------------------------------------------
    Randomization: doRandomize()
    - Applies scoped random changes to slews, offsets, and shape knobs.
        Honors per‑control locks (when scope ON) or allows (when scope OFF).
        Magnitude is bounded by the Max percentage option.
-------------------------------------------------------------------
*/
        void doRandomize() {
        using hi::util::rnd::randSpanClamp;
        float maxPct = clamp(randMaxPct, 0.f, 1.f);
        // Per-channel slews and offsets
        for (int i = 0; i < 16; ++i) {
            bool doSlew = randSlew ? (!lockSlew[i]) : allowSlew[i];
            if (doSlew) {
                float v = params[SL_PARAM[i]].getValue();     // [0,1]
                randSpanClamp(v, 0.f, 1.f, maxPct);
                params[SL_PARAM[i]].setValue(v);
            }
            bool doOff = randOffset ? (!lockOffset[i]) : allowOffset[i];
            if (doOff) {
                float v = params[OFF_PARAM[i]].getValue();     // [-10,10]
                randSpanClamp(v, -10.f, 10.f, maxPct);
                params[OFF_PARAM[i]].setValue(v);
            }
        }
        // Global shapes (rise/fall)
        {
            bool doRise = randShapes ? (!lockRiseShape) : allowRiseShape;
            if (doRise) {
                float v = params[RISE_SHAPE_PARAM].getValue(); // [-1,1]
                randSpanClamp(v, -1.f, 1.f, maxPct);
                params[RISE_SHAPE_PARAM].setValue(v);
            }
            bool doFall = randShapes ? (!lockFallShape) : allowFallShape;
            if (doFall) {
                float v = params[FALL_SHAPE_PARAM].getValue(); // [-1,1]
                randSpanClamp(v, -1.f, 1.f, maxPct);
                params[FALL_SHAPE_PARAM].setValue(v);
            }
        }
    }

/*
-------------------------------------------------------------------
    Audio/DSP: process()
    - Determines processing width (auto/forced), debounces randomize, and
        prepares shape coefficients.
    - Manages dual‑mode knob banking and derives global gain/seconds and
        offsets with "always on" overrides.
    - Pass 1: computes per‑channel targets and error norms (volts or semis)
        to detect step starts; optionally syncs glides across channels.
    - Assigns strum delays per event.
    - Pass 2: applies slew with shape modulation, optional start‑delay or
        time‑stretch, pre‑quant range handling (Clip/Scale around 0 V),
        range offset, per‑channel octave shift, blendable quantization, and
        final safety clip; updates lights and outputs.
-------------------------------------------------------------------
*/
        void process(const ProcessArgs& args) override {
    // Determine desired channel counts (handled via fade state machine)
        const bool inConn = inputs[IN_INPUT].isConnected();
        const int inCh    = inConn ? inputs[IN_INPUT].getChannels() : 0;
        int desiredProcN = 0;
        if (forcedChannels > 0)
            desiredProcN = rack::math::clamp(forcedChannels, 1, 16);
        else
            desiredProcN = hi::dsp::poly::processWidth(false, inConn, inCh, 16);
        int desiredOutN = sumToMonoOut ? 1 : desiredProcN;

    // Note: We intentionally use polyTrans.* directly below (no local aliases)
    // to make it obvious these are fields of the shared transition state.

        // Initialize current counts on first process() call
        if (polyTrans.curProcN <= 0 && polyTrans.curOutN <= 0) {
            polyTrans.curProcN = desiredProcN;
            polyTrans.curOutN  = desiredOutN;
            outputs[OUT_OUTPUT].setChannels(polyTrans.curOutN);
            polyTrans.transPhase = TRANS_STABLE;
            polyTrans.polyRamp = 1.f;
        }

        // Detect a change in desired channel counts
        bool widthChange = (desiredProcN != polyTrans.curProcN) || (desiredOutN != polyTrans.curOutN);
        if (widthChange && polyTrans.transPhase == TRANS_STABLE) {
            polyTrans.pendingProcN = desiredProcN;
            polyTrans.pendingOutN  = desiredOutN;
            // Start fade out if time > 0, otherwise immediate switch
            if (polyFadeSec > 0.f) {
                polyTrans.transPhase = TRANS_FADE_OUT;
            } else {
                // Immediate switch
                polyTrans.curProcN = polyTrans.pendingProcN;
                polyTrans.curOutN  = polyTrans.pendingOutN;
                outputs[OUT_OUTPUT].setChannels(polyTrans.curOutN);
                polyTrans.initToTargetsOnSwitch = true;
                polyTrans.transPhase = TRANS_STABLE;
                polyTrans.polyRamp = 1.f;
            }
        }

    // Set the output channel count for this block (current polyTrans.curOutN, not desired)
        outputs[OUT_OUTPUT].setChannels(polyTrans.curOutN);

        // Randomize on UI button or CV gate rising edge (with hysteresis)
        // Update amount each block from knob (overrides legacy menu)
        if (RND_AMT_PARAM < PARAMS_LEN)
            randMaxPct = rack::clamp(params[RND_AMT_PARAM].getValue(), 0.f, 1.f);
        // Cache toggle states
        if (RND_AUTO_PARAM < PARAMS_LEN) rndAutoEnabled = params[RND_AUTO_PARAM].getValue() > 0.5f;
        if (RND_SYNC_PARAM < PARAMS_LEN) rndSyncMode    = params[RND_SYNC_PARAM].getValue() > 0.5f;
        // Mode switch: recall per-mode stored raw value & reset schedulers appropriately
        if (rndSyncMode != prevRndSyncMode) {
            if (RND_TIME_PARAM < PARAMS_LEN) params[RND_TIME_PARAM].setValue(rndSyncMode ? rndTimeRawSync : rndTimeRawFree);
            if (rndSyncMode) { rndNextFireTime = -1.f; } else { rndTimerSec = 0.f; }
            prevRndSyncMode = rndSyncMode;
        }
        // Manual button always fires
        bool manualFire = rndBtnTrig.process(params[RND_PARAM].getValue() > 0.5f);
        // External trigger immediate fire only when NOT in sync mode
        bool extFire = (!rndSyncMode) && rndGateTrig.process(inputs[RND_TRIG_INPUT].getVoltage());
        if (manualFire || extFire) {
            doRandomize();
        }
        // Measure external clock period when in sync mode (edges do NOT randomize directly)
        float dt = args.sampleTime;
        rndAbsTimeSec += dt;
        bool edgeThisBlock = false;
        if (rndSyncMode) {
            // Measure clock period (single SchmittTrigger.process call per block)
            edgeThisBlock = rndClockTrig.process(inputs[RND_TRIG_INPUT].getVoltage());
            if (edgeThisBlock) {
                if (rndClockLastEdge >= 0.f) {
                    float p = rndAbsTimeSec - rndClockLastEdge;
                    if (p > 1e-4f) {
                        const float alpha = 0.25f; // EMA smoothing (slightly faster than previous 0.2)
                        if (rndClockPeriodSec < 0.f) rndClockPeriodSec = p; else rndClockPeriodSec = (1.f - alpha) * rndClockPeriodSec + alpha * p;
                        rndClockReady = true;
                    }
                }
                rndClockLastEdge = rndAbsTimeSec;
                rndDivCounter++; // advance division counter
            }
        }
        // Auto schedule: free or sync
        if (rndAutoEnabled) {
            float raw = (RND_TIME_PARAM < PARAMS_LEN) ? params[RND_TIME_PARAM].getValue() : 0.5f;
            auto rawToSec = [](float r){ const float mn=0.001f, mx=10000.f; float lmn=std::log10(mn), lmx=std::log10(mx); float lx = lmn + rack::clamp(r,0.f,1.f)*(lmx-lmn); return std::pow(10.f,lx); };
            // Centered 1x mapping: indices 0..125 divides (64..2), 126 center (1x), 127..251 multiplies (2..64)
            const int DIV_MAX = 64;
            const int TOTAL_SYNC_STEPS = (DIV_MAX-1) + 1 + (DIV_MAX-1); // 127 positions -> indices 0..126 (but we double to allow higher resolution raw?)
            // We'll map raw to 0..(TOTAL_SYNC_STEPS-1)
            const int SYNC_LAST_INDEX = TOTAL_SYNC_STEPS - 1; // 126
            if (rndSyncMode) {
                rndTimeRawSync = raw;
                if (rndClockReady && rndClockPeriodSec > 0.f) {
                    int idx = (int)std::lround(rack::clamp(raw,0.f,1.f)*SYNC_LAST_INDEX); // 0..126
                    if (idx < 0) idx = 0; else if (idx > SYNC_LAST_INDEX) idx = SYNC_LAST_INDEX;
                    int div = 1, mul = 1;
                    if (idx < (DIV_MAX-1)) { // divides region
                        int d = DIV_MAX - idx; // 64..2
                        div = d; mul = 1;
                    } else if (idx == (DIV_MAX-1)) { // center 1x
                        div = 1; mul = 1;
                    } else { // multiplies region
                        int mfac = (idx - (DIV_MAX-1)) + 1; // 2..64
                        div = 1; mul = mfac;
                    }
                    bool ratioChanged = (idx != rndPrevRatioIdx);
                    if (ratioChanged) {
                        rndPrevRatioIdx = idx;
                        // Reset phase and counters
                        rndMulIndex = 0; rndMulNextTime = -1.f; rndMulBaseTime = rndAbsTimeSec;
                        if (div > 1) rndDivCounter = 0; else if (mul > 1) { /* first pulse also at edge, handled below */ }
                    }
                    rndCurrentDivide = div; rndCurrentMultiply = mul;
                    if (div > 1 && mul == 1) {
                        // Pure division: emit only on qualifying edges
                        if (edgeThisBlock && (rndDivCounter % div) == 0) doRandomize();
                    } else if (div == 1 && mul == 1) {
                        // 1x: fire on every edge
                        if (edgeThisBlock) doRandomize();
                    } else if (mul > 1 && div == 1) {
                        // Multiplication: first pulse AT edge, then (mul-1) evenly spaced after
                        if (edgeThisBlock || ratioChanged) {
                            if (edgeThisBlock) doRandomize(); // pulse at the edge
                            rndMulBaseTime = rndAbsTimeSec; // anchor
                            rndMulIndex = 0; // counts interior pulses emitted
                            float period = rndClockPeriodSec;
                            if (period <= 0.f) { rndMulNextTime = -1.f; }
                            else {
                                float subdiv = period / (float)mul;
                                rndMulNextTime = rndMulBaseTime + subdiv; // first interior pulse time
                            }
                        }
                        if (rndMulNextTime >= 0.f && rndClockPeriodSec > 0.f) {
                            float subdiv = rndClockPeriodSec / (float)mul;
                            while (rndMulNextTime >= 0.f && rndMulNextTime <= rndAbsTimeSec + 1e-9f) {
                                doRandomize();
                                rndMulIndex++;
                                if (rndMulIndex >= mul - 1) { rndMulNextTime = -1.f; break; }
                                rndMulNextTime += subdiv;
                            }
                        }
                    }
                }
            } else {
                // Free mode (legacy logarithmic timing)
                rndTimeRawFree = raw;
                float intervalSec = rawToSec(raw);
                if (intervalSec < 0.001f) intervalSec = 0.001f;
                rndTimerSec += dt;
                if (rndTimerSec >= intervalSec) {
                    doRandomize();
                    while (rndTimerSec >= intervalSec) rndTimerSec -= intervalSec;
                }
            }
        } else {
            if (rndTimerSec > 60.f) rndTimerSec = std::fmod(rndTimerSec, 60.f);
        }

        // Global shapes and precomputed constants (shared per block)
    const float riseShape = params[RISE_SHAPE_PARAM].getValue(); // [-1,1]
    const float fallShape = params[FALL_SHAPE_PARAM].getValue(); // [-1,1]
    auto riseParams = hi::dsp::glide::makeShape(riseShape);
    auto fallParams = hi::dsp::glide::makeShape(fallShape);

    // Pre‑quant range enforcement helper
        float clipLimit = currentClipLimit();
        // Dual-mode knob state management and derived globals
        bool modeSlewNow = params[GLOBAL_SLEW_MODE_PARAM].getValue() > 0.5f;
        bool modeOffNow  = params[GLOBAL_OFFSET_MODE_PARAM].getValue() > 0.5f;
        // Persist active bank values from current raw knob positions
        if (gSlew.mode) gSlew.b = params[GLOBAL_SLEW_PARAM].getValue(); else gSlew.a = params[GLOBAL_SLEW_PARAM].getValue();
        if (gOffset.mode) gOffset.b = params[GLOBAL_OFFSET_PARAM].getValue(); else gOffset.a = params[GLOBAL_OFFSET_PARAM].getValue();
        // On mode change, snap the knob to the saved value of the new bank
        if (modeSlewNow != gSlew.mode) {
            params[GLOBAL_SLEW_PARAM].setValue(modeSlewNow ? gSlew.b : gSlew.a);
            gSlew.mode = modeSlewNow;
        }
        if (modeOffNow != gOffset.mode) {
            params[GLOBAL_OFFSET_PARAM].setValue(modeOffNow ? gOffset.b : gOffset.a);
            gOffset.mode = modeOffNow;
        }
        // Derived controls
        float gsecAdd = 0.f; // additional slew time in seconds
        float gGain    = 1.f; // attenuverter gain
        // Allow both modes when "always on" flags are set
        bool useSlewAdd = (!gSlew.mode) || slewAddAlwaysOn;
        bool useAttv    = ( gSlew.mode) || attenuverterAlwaysOn;
        if (useSlewAdd) {
            // Use the banked value for slew-add when knob is currently set to attenuverter
            float rawSlew = gSlew.mode ? gSlew.a : params[GLOBAL_SLEW_PARAM].getValue();
            gsecAdd = hi::ui::ExpTimeQuantity::knobToSec(rawSlew);
        }
        if (useAttv) {
            float rawAttv = gSlew.mode ? params[GLOBAL_SLEW_PARAM].getValue() : gSlew.b;
            rawAttv = rack::math::clamp(rawAttv, 0.f, 1.f);
            gGain = -10.f + 20.f * rawAttv; // [-10,+10]
        }
        // Offsets
        float rangeOffset = 0.f;      // applied AFTER range, BEFORE quantizer
        float globalOffset = 0.f;     // applied with per-channel offsets
        bool useRangeOff  = gOffset.mode || rangeOffsetAlwaysOn;
        bool useGlobOff   = (!gOffset.mode) || globalOffsetAlwaysOn;
        if (useRangeOff) {
            float v = gOffset.mode ? params[GLOBAL_OFFSET_PARAM].getValue() : gOffset.b;
            rangeOffset = clamp(v, -5.f, 5.f);
        }
        if (useGlobOff) {
            float v = gOffset.mode ? gOffset.a : params[GLOBAL_OFFSET_PARAM].getValue();
            globalOffset = clamp(v, -10.f, 10.f);
        }
        // Pre-range limiter/scaler operates around 0V only; offset is applied after this
        auto preRange = [&](float v) -> float {
            return hi::dsp::range::apply(v, rangeMode == 0 ? hi::dsp::range::Mode::Clip : hi::dsp::range::Mode::Scale, clipLimit, softClipOut);
        };

    bool modeChanged = (pitchSafeGlide != prevPitchSafeGlide);
        float outVals[16] = {0};
        // Pass 1: compute targets and detect global start
        float targetArr[16] = {0};
        float aerrNArr[16] = {0};
        int   signArr[16]  = {0};
    for (int c = 0; c < polyTrans.curProcN; ++c) {
            float in  = 0.f;
            if (inConn) {
                if (inCh <= 1) in = inputs[IN_INPUT].getVoltage(0);
                else if (c < inCh) in = inputs[IN_INPUT].getVoltage(c);
            }
            // Global attenuverter (if enabled) applies to input before offset/slew
            if (useAttv) in *= gGain;
            float offCh = params[OFF_PARAM[c]].getValue();
            float offTot = offCh + globalOffset;
            int qm = quantizeOffsetModeCh[c];
            if (qm == 1) { // Semitone (EDO or TET) quantization
                int Nsteps = (tuningMode == 0 ? ((edo <= 0) ? 12 : edo) : (tetSteps > 0 ? tetSteps : 9));
                float period = (tuningMode == 0) ? 1.f : ((tetPeriodOct > 0.f) ? tetPeriodOct : std::log2(3.f/2.f));
                // Steps per octave (volts) = steps per period divided by period size in octaves
                float stepsPerOct = (float)Nsteps / period;
                offTot = std::round(offTot * stepsPerOct) / stepsPerOct;
            } else if (qm == 2) { // Cents (1/1200 V)
                offTot = std::round(offTot * 1200.f) / 1200.f;
            }
            float target = in + offTot;
            targetArr[c] = target;

            float yPrev = lastOut[c];
            float err   = target - yPrev;
            int   sign  = (err > 0.f) - (err < 0.f);
            float aerrV = std::fabs(err);
            float aerrN = pitchSafeGlide ? hi::dsp::glide::voltsToSemitones(aerrV) : aerrV;
            signArr[c]  = sign;
            aerrNArr[c] = aerrN;
        }
        bool globalStart = modeChanged;
    for (int c = 0; c < polyTrans.curProcN; ++c) {
            if (signArr[c] != stepSign[c] || aerrNArr[c] > stepNorm[c]) { globalStart = true; break; }
        }
        if (syncGlides && globalStart) {
            for (int c = 0; c < polyTrans.curProcN; ++c) {
                stepSign[c] = signArr[c];
                stepNorm[c] = std::max(aerrNArr[c], hconst::EPS_ERR);
                float aerrV = std::fabs(targetArr[c] - lastOut[c]);
                baseJumpV[c] = aerrV;
                auto unitSizeVCalc = [&](){
                    if (!glideNormEnabled) return 1.f;
                    switch (static_cast<GlideNorm>(glideNorm)) {
                        case GlideNorm::VoltsLinear: return 1.f;
                        case GlideNorm::CentLinear:  return 1.f/12.f;
                        case GlideNorm::StepSafe: {
                            int Nsteps = (tuningMode == 0 ? ((edo <= 0) ? 12 : edo) : (tetSteps > 0 ? tetSteps : 9));
                            float period = (tuningMode == 0) ? 1.f : ((tetPeriodOct > 0.f) ? tetPeriodOct : std::log2(3.f/2.f));
                            return period / std::max(1, Nsteps);
                        }
                    }
                    return 1.f;
                };
                normUnitAtStep[c] = unitSizeVCalc();
            }
        }

        // Assign strum delays per event (so Random is stable within an event)
        auto assignDelayFor = [&](int ch){
            if (!(strumEnabled && strumMs > 0.f && polyTrans.curProcN > 1)) { strumDelayAssigned[ch]=0.f; strumDelayLeft[ch]=0.f; return; }
            hi::dsp::strum::Mode mode = (strumMode==0 ? hi::dsp::strum::Mode::Up : (strumMode==1 ? hi::dsp::strum::Mode::Down : hi::dsp::strum::Mode::Random));
            float tmp[16] = {0};
            hi::dsp::strum::assign(strumMs, polyTrans.curProcN, mode, tmp);
            strumDelayAssigned[ch] = tmp[ch];
            strumDelayLeft[ch] = tmp[ch];
        };
        if (strumEnabled && strumMs > 0.f && polyTrans.curProcN > 1) {
            if (syncGlides) {
                if (globalStart) {
                    for (int c = 0; c < polyTrans.curProcN; ++c) assignDelayFor(c);
                }
            } else {
                for (int c = 0; c < polyTrans.curProcN; ++c) {
                    if (modeChanged || signArr[c] != stepSign[c] || aerrNArr[c] > stepNorm[c]) assignDelayFor(c);
                }
            }
        }

    // Pass 2: process with (optionally) synchronized step norms
    for (int c = 0; c < polyTrans.curProcN; ++c) {
            float target = targetArr[c];
            float yPrev = lastOut[c];
            float err   = target - yPrev;
            float aerrV = std::fabs(err);
            float aerrN = pitchSafeGlide ? hi::dsp::glide::voltsToSemitones(aerrV) : aerrV;
            int   sign  = (err > 0.f) - (err < 0.f);

            // Per-channel seconds + global + optional strum behavior
            float sec  = hi::ui::ExpTimeQuantity::knobToSec(params[SL_PARAM[c]].getValue());
            float gsec = gsecAdd; // from dual-mode global slew
            float assignedDelay = (strumEnabled && polyTrans.curProcN > 1) ? strumDelayAssigned[c] : 0.f;
            if (strumEnabled && strumType == 0) {
                // Time-stretch: add assigned delay to effective glide time
                sec += gsec + assignedDelay;
            } else {
                // No time-stretch addition; start-delay handled below
                sec += gsec;
            }
            bool noSlew = (sec <= hconst::MIN_SEC);

            if (!syncGlides) {
                bool modeToggle = (prevGlideNorm != glideNorm) || (prevGlideNormEnabled != glideNormEnabled) || modeChanged;
                if (modeToggle) { stepNorm[c] = hconst::EPS_ERR; stepSign[c] = sign; }
                if (modeToggle || sign != stepSign[c] || aerrN > stepNorm[c]) {
                    stepSign[c] = sign;
                    stepNorm[c] = std::max(aerrN, hconst::EPS_ERR);
                    float aerrV0 = std::fabs(target - yPrev);
                    baseJumpV[c] = aerrV0;
                    auto unitSizeVCalc = [&](){
                        if (!glideNormEnabled) return 1.f;
                        switch (static_cast<GlideNorm>(glideNorm)) {
                            case GlideNorm::VoltsLinear: return 1.f;
                            case GlideNorm::CentLinear:  return 1.f/12.f;
                            case GlideNorm::StepSafe: {
                                int Nsteps = (tuningMode == 0 ? ((edo <= 0) ? 12 : edo) : (tetSteps > 0 ? tetSteps : 9));
                                float period = (tuningMode == 0) ? 1.f : ((tetPeriodOct > 0.f) ? tetPeriodOct : std::log2(3.f/2.f));
                                return period / std::max(1, Nsteps);
                            }
                        }
                        return 1.f;
                    };
                    normUnitAtStep[c] = unitSizeVCalc();
                }
            }

            float yRaw = target;
            bool inStartDelay = (strumEnabled && strumType == 1 && strumDelayLeft[c] > 0.f);
            if (inStartDelay) {
                // Hold output until delay elapses
                hi::dsp::strum::tickStartDelays((float)args.sampleTime, polyTrans.curProcN, strumDelayLeft);
                yRaw = yPrev; // hold
            } else {
                if (!noSlew && quantizerPos == QuantizerPos::Post) { // In Post mode we slew BEFORE quantizer
                    float remainingV = aerrV;
                    float totalJumpV = std::max(baseJumpV[c], hconst::EPS_ERR);
                    float baseRateV = 0.f;
                    if (!glideNormEnabled) {
                        baseRateV = totalJumpV / sec; // equal-time total distance over seconds
                    } else {
                        float unitV = std::max(normUnitAtStep[c], hconst::EPS_ERR);
                        baseRateV = unitV / sec; // constant units per second
                    }
                    float u = clamp(remainingV / std::max(totalJumpV, hconst::EPS_ERR), 0.f, 1.f);
                    float rateRise = baseRateV * hi::dsp::glide::shapeMul(u, riseParams, hconst::EPS_ERR);
                    float rateFall = baseRateV * hi::dsp::glide::shapeMul(u, fallParams, hconst::EPS_ERR);
                    if (std::fabs(rateRise - prevRiseRate[c]) > hconst::RATE_EPS || std::fabs(rateFall - prevFallRate[c]) > hconst::RATE_EPS) {
                        slews[c].setRiseFall(rateRise, rateFall);
                        prevRiseRate[c] = rateRise;
                        prevFallRate[c] = rateFall;
                    }
                    yRaw = slews[c].process(args.sampleTime, target);
                }
            }

            // Apply pre-quant Range (around 0V)
            float yPre = preRange(yRaw);
            // Octave shift pre-quant and apply Range offset before quantizer to shift whole window up/down
            float yBasePre = yPre + rangeOffset + (float)postOctShift[c] * 1.f;

            // Quantizer position: Pre (legacy Q→S) vs Post (S→Q). Post keeps tuning stable under heavy slew; no math changed—only order.
            float yFinal = 0.f;
            if (quantizerPos == QuantizerPos::Pre) {
                // Legacy behavior: Quantize first, then mix strength, then slew.
                // Quantizer path (identical core calls) operating on yPre domain.
                float yPreForQ = yBasePre; // includes rangeOffset & octave shift
                float yRel = yPreForQ - rangeOffset;
                float yQRel = yRel; // quantized relative volts
                if (qzEnabled[c]) {
                    // --- Core quantizer logic (unchanged) operating on unslewed signal ---
                    hi::dsp::QuantConfig qc; if (tuningMode == 0) { qc.edo = (edo <= 0) ? 12 : edo; qc.periodOct = 1.f; } else { qc.edo = tetSteps > 0 ? tetSteps : 9; qc.periodOct = (tetPeriodOct > 0.f) ? tetPeriodOct : std::log2(3.f/2.f); }
                    qc.root = rootNote; qc.useCustom = useCustomScale; qc.customFollowsRoot = customScaleFollowsRoot; qc.customMask12 = customMask12; qc.customMask24 = customMask24; qc.scaleIndex = scaleIndex; if (qc.useCustom && (qc.edo!=12 && qc.edo!=24)) { if ((int)customMaskGeneric.size()==qc.edo) { qc.customMaskGeneric=customMaskGeneric.data(); qc.customMaskLen=(int)customMaskGeneric.size(); } }
                    bool cfgChanged = (prevRootNote!=rootNote || prevScaleIndex!=scaleIndex || prevEdo!=qc.edo || prevTetSteps!=tetSteps || prevTetPeriodOct!=qc.periodOct || prevTuningMode!=tuningMode || prevUseCustomScale!=useCustomScale || prevCustomFollowsRoot!=customScaleFollowsRoot || prevCustomMask12!=customMask12 || prevCustomMask24!=customMask24); if (cfgChanged) { for (int k=0;k<16;++k) latchedInit[k]=false; prevRootNote=rootNote; prevScaleIndex=scaleIndex; prevEdo=qc.edo; prevTetSteps=tetSteps; prevTetPeriodOct=qc.periodOct; prevTuningMode=tuningMode; prevUseCustomScale=useCustomScale; prevCustomFollowsRoot=customScaleFollowsRoot; prevCustomMask12=customMask12; prevCustomMask24=customMask24; }
                    int N = qc.edo; float period = qc.periodOct;
                    double fs = (double)yRel * (double)N / (double)period;
                    if (!latchedInit[c]) {
                        latchedStep[c] = hi::dsp::nearestAllowedStep((int)std::round(fs), (float)fs, qc);
                        lastFs[c]  = fs;   // FIX: seed direction state with current fs
                        lastDir[c] = 0;    // FIX: start neutral so peaks don't mis-set direction
                        latchedInit[c] = true;
                    }
                    
                    // FIX: Directional Snap with direction hysteresis (before latch decision)
                    int baseStep = (int)std::round(fs);  // default for non-directional modes
                    int dir = 0;  // direction state for Directional Snap
                    if (quantRoundMode == 0) {  // Directional Snap
                        float Hc = rack::clamp(stickinessCents, 0.f, 20.f);
                        const float maxAllowed = 0.4f * 1200.f * (period / (float)N);
                        if (Hc > maxAllowed) Hc = maxAllowed;
                        float Hs = (Hc * (float)N) / 1200.0f;                      // cents→steps
                        float Hd = std::max(0.75f*Hs, 0.02f);                      // FIX: widen direction hysteresis a bit
                        double d = fs - lastFs[c];
                        dir = lastDir[c];
                        if (d > +Hd) dir = +1;
                        else if (d < -Hd) dir = -1;
                        if (dir > 0)      baseStep = (int)std::ceil(fs);
                        else if (dir < 0) baseStep = (int)std::floor(fs);
                        else              baseStep = latchedStep[c];  // hold candidate at peak
                        lastDir[c] = dir;
                        lastFs[c]  = fs;
                    }
                    if (!hi::dsp::isAllowedStep(latchedStep[c], qc)) { latchedStep[c] = hi::dsp::nearestAllowedStep(latchedStep[c], (float)fs, qc); }
                    // FIX: Directional Snap should move at most one allowed degree in the current direction.
                    // Use nextAllowedStep from the *latched* degree when Directional Snap is active.
                    int targetStep;
                    if (quantRoundMode == 0) { // Directional Snap
                        int candidate = latchedStep[c];
                        if (dir > 0)      candidate = hi::dsp::nextAllowedStep(latchedStep[c], +1, qc);
                        else if (dir < 0) candidate = hi::dsp::nextAllowedStep(latchedStep[c], -1, qc);
                        // dir == 0 → hold candidate at current latched step
                        targetStep = candidate;
                    } else {
                        // Other modes unchanged
                        targetStep = hi::dsp::nearestAllowedStep(baseStep, (float)fs, qc);
                    }
                    // (existing center-anchored Schmitt latch logic follows)
                    float Hc = rack::clamp(stickinessCents, 0.f, 20.f);
                    float stepCents = 1200.f * (period / (float)N);
                    const float maxAllowed = 0.4f * stepCents;
                    if (Hc > maxAllowed) Hc = maxAllowed;
                    float Hs = (Hc * (float)N) / 1200.0f;             // convert cents→steps
                    float d  = (float)(fs - (double)latchedStep[c]);  // steps from latched center
                    float upThresh = +0.5f + Hs;
                    float downThresh = -0.5f - Hs;
                    // Switch only when crossing thresholds and only by ±1 step
                    if (targetStep > latchedStep[c] && d > upThresh) latchedStep[c] = latchedStep[c] + 1;
                    else if (targetStep < latchedStep[c] && d < downThresh) latchedStep[c] = latchedStep[c] - 1;
                    // else hold at latchedStep[c]
                    yQRel = hi::dsp::snapEDO((latchedStep[c] / (float)N) * period, qc, 10.f, false, 0);
                    if (quantRoundMode != 1) { float rawSemi = yRel * 12.f; float snappedSemi = yQRel * 12.f; float diff = rawSemi - snappedSemi; float prev = prevYRel[c]; float dir = (yRel > prev + 1e-6f) ? 1.f : (yRel < prev - 1e-6f ? -1.f : 0.f); int slopeDir = (dir > 0.f) ? +1 : (dir < 0.f ? -1 : 0); hi::dsp::RoundMode rm = (quantRoundMode==0? hi::dsp::RoundMode::Directional : (quantRoundMode==2? hi::dsp::RoundMode::Ceil : (quantRoundMode==3? hi::dsp::RoundMode::Floor : hi::dsp::RoundMode::Nearest))); hi::dsp::RoundPolicy rp{rm}; (void)hi::dsp::pickRoundingTarget(0, diff, (int)slopeDir, rp); if (rm == hi::dsp::RoundMode::Directional) { if (slopeDir > 0 && diff > 0.f) { float nudged = quantizeToScale(yQRel + (1.f/12.f)*0.51f, 0, clipLimit, true); if (nudged > yQRel + 1e-5f) yQRel = nudged; } else if (slopeDir < 0 && diff < 0.f) { float nudged = quantizeToScale(yQRel - (1.f/12.f)*0.51f, 0, clipLimit, true); if (nudged < yQRel - 1e-5f) yQRel = nudged; } } else if (rm == hi::dsp::RoundMode::Ceil) { if (diff > 1e-5f) { float nudged = quantizeToScale(yQRel + (1.f/12.f)*0.51f, 0, clipLimit, true); if (nudged > yQRel + 1e-5f) yQRel = nudged; } } else if (rm == hi::dsp::RoundMode::Floor) { if (diff < -1e-5f) { float nudged = quantizeToScale(yQRel - (1.f/12.f)*0.51f, 0, clipLimit, true); if (nudged < yQRel - 1e-5f) yQRel = nudged; } } prevYRel[c] = yRel; } else { prevYRel[c] = yRel; }
                } else { prevYRel[c] = yRel; }
                float yQAbs = yQRel + rangeOffset; float t = clamp(quantStrength, 0.f, 1.f); float yMix = yPreForQ + (yQAbs - yPreForQ) * t; // Quant strength blend (raw = yPreForQ)
                // Now apply slew AFTER quantization (legacy order): reuse rate calc path targeting yMix
                float yPost = yMix;
                if (!noSlew && !inStartDelay) {
                    float remainingV = std::fabs(yMix - lastOut[c]);
                    float totalJumpV = std::max(baseJumpV[c], hconst::EPS_ERR);
                    float baseRateV = 0.f;
                    if (!glideNormEnabled) {
                        baseRateV = totalJumpV / sec;
                    } else {
                        float unitV = std::max(normUnitAtStep[c], hconst::EPS_ERR);
                        baseRateV = unitV / sec;
                    }
                    float u = clamp(remainingV / std::max(totalJumpV, hconst::EPS_ERR), 0.f, 1.f);
                    float rateRise = baseRateV * hi::dsp::glide::shapeMul(u, riseParams, hconst::EPS_ERR);
                    float rateFall = baseRateV * hi::dsp::glide::shapeMul(u, fallParams, hconst::EPS_ERR);
                    if (std::fabs(rateRise - prevRiseRate[c]) > hconst::RATE_EPS || std::fabs(rateFall - prevFallRate[c]) > hconst::RATE_EPS) { slews[c].setRiseFall(rateRise, rateFall); prevRiseRate[c] = rateRise; prevFallRate[c] = rateFall; }
                    yPost = slews[c].process(args.sampleTime, yMix);
                }
                yFinal = yPost;
            } else {
                // New default: Slew first (already done), then quantize for pitch stability under long glides.
                float ySlewed = yPre + rangeOffset + (float)postOctShift[c] * 1.f; // identical placement; yPre already includes range handling
                float yRel = (ySlewed - rangeOffset); // relative before quant
                float yOutQuant = ySlewed; // will hold final blended result
                if (qzEnabled[c]) {
                    // --- Begin unchanged quantizer logic (post-slew domain) ---
                    hi::dsp::QuantConfig qc;
                    if (tuningMode == 0) { qc.edo = (edo <= 0) ? 12 : edo; qc.periodOct = 1.f; }
                    else { qc.edo = tetSteps > 0 ? tetSteps : 9; qc.periodOct = (tetPeriodOct > 0.f) ? tetPeriodOct : std::log2(3.f/2.f); }
                    qc.root = rootNote; qc.useCustom = useCustomScale; qc.customFollowsRoot = customScaleFollowsRoot;
                    qc.customMask12 = customMask12; qc.customMask24 = customMask24; qc.scaleIndex = scaleIndex;
                    if (qc.useCustom && (qc.edo!=12 && qc.edo!=24)) {
                        if ((int)customMaskGeneric.size()==qc.edo) { qc.customMaskGeneric=customMaskGeneric.data(); qc.customMaskLen=(int)customMaskGeneric.size(); }
                    }
                    int N = qc.edo; float period = qc.periodOct; bool cfgChanged = (prevRootNote!=rootNote || prevScaleIndex!=scaleIndex || prevEdo!=qc.edo || prevTetSteps!=tetSteps || prevTetPeriodOct!=qc.periodOct || prevTuningMode!=tuningMode || prevUseCustomScale!=useCustomScale || prevCustomFollowsRoot!=customScaleFollowsRoot || prevCustomMask12!=customMask12 || prevCustomMask24!=customMask24); if (cfgChanged) { for (int k=0;k<16;++k) latchedInit[k]=false; prevRootNote=rootNote; prevScaleIndex=scaleIndex; prevEdo=qc.edo; prevTetSteps=tetSteps; prevTetPeriodOct=qc.periodOct; prevTuningMode=tuningMode; prevUseCustomScale=useCustomScale; prevCustomFollowsRoot=customScaleFollowsRoot; prevCustomMask12=customMask12; prevCustomMask24=customMask24; }
                    float fs = yRel * (float)N / period; if (!latchedInit[c]) { latchedStep[c] = hi::dsp::nearestAllowedStep( (int)std::round(fs), fs, qc ); latchedInit[c]=true; } if (!hi::dsp::isAllowedStep(latchedStep[c], qc)) { latchedStep[c] = hi::dsp::nearestAllowedStep(latchedStep[c], fs, qc); }
                    // FIX: Use stateful tie-breaking for boundary stability
                    float dV = period / (float)N; float stepCents = 1200.f * dV; float Hc = rack::clamp(stickinessCents, 0.f, 20.f); float maxAllowed = 0.4f * stepCents; if (Hc > maxAllowed) Hc = maxAllowed; float H_V = Hc / 1200.f; int upStep = hi::dsp::nextAllowedStep(latchedStep[c], +1, qc); int dnStep = hi::dsp::nextAllowedStep(latchedStep[c], -1, qc); float center = (latchedStep[c] / (float)N) * period; float vUp = (upStep / (float)N) * period; hi::dsp::HystSpec hs{ (vUp - center) * 2.f, H_V }; auto th = hi::dsp::computeHysteresis(center, hs); float T_up = th.up; float T_down = th.down; if (yRel >= T_up && upStep != latchedStep[c]) latchedStep[c] = upStep; else if (yRel <= T_down && dnStep != latchedStep[c]) latchedStep[c] = dnStep; float yqRel = hi::dsp::snapEDO((latchedStep[c] / (float)N) * period, qc, 10.f, false, 0);
                    if (quantRoundMode != 1) { float rawSemi = yRel * 12.f; float snappedSemi = yqRel * 12.f; float diff = rawSemi - snappedSemi; float prev = prevYRel[c]; float dir = (yRel > prev + 1e-6f) ? 1.f : (yRel < prev - 1e-6f ? -1.f : 0.f); int slopeDir = (dir > 0.f) ? +1 : (dir < 0.f ? -1 : 0); hi::dsp::RoundMode rm = (quantRoundMode==0? hi::dsp::RoundMode::Directional : (quantRoundMode==2? hi::dsp::RoundMode::Ceil : (quantRoundMode==3? hi::dsp::RoundMode::Floor : hi::dsp::RoundMode::Nearest))); hi::dsp::RoundPolicy rp{rm}; float posWithin = diff; (void)hi::dsp::pickRoundingTarget(0, posWithin, slopeDir, rp); 
                        // FIX: Replace chromatic nudging with scale-aware directional selection
                        if (rm == hi::dsp::RoundMode::Directional && std::fabs(diff) > 1e-5f) { int targetStep = (slopeDir > 0) ? hi::dsp::nextAllowedStep(latchedStep[c], +1, qc) : hi::dsp::nextAllowedStep(latchedStep[c], -1, qc); if (targetStep != latchedStep[c]) { float targetV = (targetStep / (float)N) * period; yqRel = targetV; } } else if (rm == hi::dsp::RoundMode::Ceil && diff > 1e-5f) { int targetStep = hi::dsp::nextAllowedStep(latchedStep[c], +1, qc); if (targetStep != latchedStep[c]) { float targetV = (targetStep / (float)N) * period; yqRel = targetV; } } else if (rm == hi::dsp::RoundMode::Floor && diff < -1e-5f) { int targetStep = hi::dsp::nextAllowedStep(latchedStep[c], -1, qc); if (targetStep != latchedStep[c]) { float targetV = (targetStep / (float)N) * period; yqRel = targetV; } }
                        prevYRel[c] = (ySlewed - rangeOffset); // In Post mode, track pre-quant slew domain for directional snap sign
                    } else {
                        prevYRel[c] = (ySlewed - rangeOffset);
                    }
                    float yq = yqRel + rangeOffset; float t = clamp(quantStrength, 0.f, 1.f); yOutQuant = ySlewed + (yq - ySlewed) * t; // Strength blend AFTER slew (raw signal: ySlewed in Post mode)
                    // Quantizer position toggle (no math changes) — Post mode uses ySlewed as raw blend source.
                } else {
                    prevYRel[c] = (ySlewed - rangeOffset);
                }
                float yPost = yOutQuant; // Already slewed; quant done last
                yFinal = yPost;
            }

            // Strength crossfade raw source note: Pre mode uses yPre(yPreForQ), Post uses ySlewed. (Comment clarifies raw signal for blend.)
            // Post safety clip at ±10 V, respecting softClipOut choice
            if (softClipOut) yFinal = hi::dsp::clip::soft(yFinal, hconst::MAX_VOLT_CLAMP);
            else            yFinal = clamp(yFinal, -hconst::MAX_VOLT_CLAMP, hconst::MAX_VOLT_CLAMP);
            outVals[c] = yFinal;
            lastOut[c] = yFinal;
            hi::ui::led::setBipolar(lights[CH_LIGHT + 2*c + 0], lights[CH_LIGHT + 2*c + 1], yFinal, args.sampleTime);
        }

    // Emit outputs
    // Apply poly ramp for pop-free fade during channel switches
        float ramp = clamp(polyTrans.polyRamp, 0.f, 1.f);
        if (sumToMonoOut) {
            float sum = 0.f;
            for (int c = 0; c < polyTrans.curProcN; ++c) sum += outVals[c];
            if (avgWhenSumming && polyTrans.curProcN > 0) sum /= (float)polyTrans.curProcN;
            outputs[OUT_OUTPUT].setVoltage(clamp(sum * ramp, -hconst::MAX_VOLT_CLAMP, hconst::MAX_VOLT_CLAMP), 0);
        } else {
            for (int c = 0; c < polyTrans.curProcN; ++c) outputs[OUT_OUTPUT].setVoltage(outVals[c] * ramp, c);
        }

        // Clear any unused LEDs
        for (int c = polyTrans.curProcN; c < 16; ++c) {
            lights[CH_LIGHT + 2*c + 0].setBrightness(0.f);
            lights[CH_LIGHT + 2*c + 1].setBrightness(0.f);
        }

        // Handle fade phase progression at the end of block
        if (polyTrans.transPhase == TRANS_FADE_OUT) {
            if (polyFadeSec <= 0.f) {
                polyTrans.polyRamp = 0.f;
            } else {
                polyTrans.polyRamp = std::max(0.f, polyTrans.polyRamp - (float)args.sampleTime / polyFadeSec);
            }
            if (polyTrans.polyRamp <= 0.f + 1e-6f) {
                // Switch to new width while silent
                polyTrans.curProcN = polyTrans.pendingProcN;
                polyTrans.curOutN  = polyTrans.pendingOutN;
                outputs[OUT_OUTPUT].setChannels(polyTrans.curOutN);
                polyTrans.initToTargetsOnSwitch = true;
                polyTrans.transPhase = TRANS_FADE_IN;
                // prepare to fade in
            }
        } else if (polyTrans.transPhase == TRANS_FADE_IN) {
            // On entry (polyTrans.initToTargetsOnSwitch), reinitialize lastOut to current targets to avoid jumps
            if (polyTrans.initToTargetsOnSwitch) {
                // Recompute targets with current settings for polyTrans.curProcN channels
                for (int c = 0; c < polyTrans.curProcN; ++c) {
                    // Recreate minimal target calculation (in + offsets), matching earlier pass
                    float in  = 0.f;
                    if (inConn) {
                        if (inCh <= 1) in = inputs[IN_INPUT].getVoltage(0);
                        else if (c < inCh) in = inputs[IN_INPUT].getVoltage(c);
                    }
                    if ((params[GLOBAL_SLEW_MODE_PARAM].getValue() > 0.5f) || attenuverterAlwaysOn) {
                        // useAttv true; recompute gGain quickly
                        float rawAttv = gSlew.mode ? params[GLOBAL_SLEW_PARAM].getValue() : gSlew.b;
                        rawAttv = rack::math::clamp(rawAttv, 0.f, 1.f);
                        float gGain2 = -10.f + 20.f * rawAttv;
                        in *= gGain2;
                    }
                    float offCh = params[OFF_PARAM[c]].getValue();
                    float offTot = offCh + (gOffset.mode ? gOffset.a : params[GLOBAL_OFFSET_PARAM].getValue());
                    int qm = quantizeOffsetModeCh[c];
                    if (qm == 1) {
                        int Nsteps = (tuningMode == 0 ? ((edo <= 0) ? 12 : edo) : (tetSteps > 0 ? tetSteps : 9));
                        float period = (tuningMode == 0) ? 1.f : ((tetPeriodOct > 0.f) ? tetPeriodOct : std::log2(3.f/2.f));
                        float stepsPerOct = (float)Nsteps / period;
                        offTot = std::round(offTot * stepsPerOct) / stepsPerOct;
                    } else if (qm == 2) {
                        offTot = std::round(offTot * 1200.f) / 1200.f;
                    }
                    float target = in + offTot;
                    // Initialize slews and outputs to targets so fade up is from current value
                    lastOut[c] = target;
                    slews[c].reset();
                }
                polyTrans.initToTargetsOnSwitch = false;
                polyTrans.polyRamp = 0.f; // start from silence
            }
            if (polyFadeSec <= 0.f) {
                polyTrans.polyRamp = 1.f;
            } else {
                polyTrans.polyRamp = std::min(1.f, polyTrans.polyRamp + (float)args.sampleTime / polyFadeSec);
            }
            if (polyTrans.polyRamp >= 1.f - 1e-6f) {
                polyTrans.polyRamp = 1.f;
                polyTrans.transPhase = TRANS_STABLE;
            }
        } else {
            // Keep ramp at 1 when stable
            polyTrans.polyRamp = 1.f;
        }

    // Remember mode
    prevPitchSafeGlide = pitchSafeGlide;
    prevGlideNorm = glideNorm;
    prevGlideNormEnabled = glideNormEnabled;
    }
};

// -----------------------------------------------------------------------------
// Phase 3D: CoreState glue helper definitions (no behavior change). These
// mirror the previous JSON read/write logic exactly, simply centralized.
// -----------------------------------------------------------------------------
static void fillCoreStateFromModule(const PolyQuanta& m, hi::dsp::CoreState& cs) noexcept {
    cs.quantStrength = m.quantStrength;
    cs.quantRoundMode = m.quantRoundMode;
    cs.stickinessCents = m.stickinessCents;
    cs.edo = m.edo; cs.tuningMode = m.tuningMode; cs.tetSteps = m.tetSteps; cs.tetPeriodOct = m.tetPeriodOct;
    cs.useCustomScale = m.useCustomScale; cs.rememberCustomScale = m.rememberCustomScale; cs.customScaleFollowsRoot = m.customScaleFollowsRoot;
    cs.customMask12 = m.customMask12; cs.customMask24 = m.customMask24;
    cs.customMaskGeneric = m.customMaskGeneric; // byte-per-step copy
    for (int i=0;i<16;++i) { cs.qzEnabled[i] = m.qzEnabled[i]; cs.postOctShift[i] = m.postOctShift[i]; }
    cs.rootNote = m.rootNote; cs.scaleIndex = m.scaleIndex;
}
static void applyCoreStateToModule(const hi::dsp::CoreState& cs, PolyQuanta& m) noexcept {
    m.quantStrength = cs.quantStrength;
    m.quantRoundMode = cs.quantRoundMode;
    m.stickinessCents = cs.stickinessCents;
    m.edo = cs.edo; m.tuningMode = cs.tuningMode; m.tetSteps = cs.tetSteps; m.tetPeriodOct = cs.tetPeriodOct;
    m.useCustomScale = cs.useCustomScale; m.rememberCustomScale = cs.rememberCustomScale; m.customScaleFollowsRoot = cs.customScaleFollowsRoot;
    m.customMask12 = cs.customMask12; m.customMask24 = cs.customMask24;
    m.customMaskGeneric = cs.customMaskGeneric; // copy vector
    for (int i=0;i<16;++i) { m.qzEnabled[i] = cs.qzEnabled[i]; m.postOctShift[i] = cs.postOctShift[i]; }
    m.rootNote = cs.rootNote; m.scaleIndex = cs.scaleIndex;
}

/*
-----------------------------------------------------------------------
 Section: Out-of-class param index arrays
 - Maps compact array indices (0..15) to the generated enum IDs for Slew,
     Offset, and legacy per-channel Quantize parameters.
-----------------------------------------------------------------------
*/
// out-of-class definitions required by C++11
const int PolyQuanta::SL_PARAM[16] = {
    PolyQuanta::SL1_PARAM,  PolyQuanta::SL2_PARAM,
    PolyQuanta::SL3_PARAM,  PolyQuanta::SL4_PARAM,
    PolyQuanta::SL5_PARAM,  PolyQuanta::SL6_PARAM,
    PolyQuanta::SL7_PARAM,  PolyQuanta::SL8_PARAM,
    PolyQuanta::SL9_PARAM,  PolyQuanta::SL10_PARAM,
    PolyQuanta::SL11_PARAM, PolyQuanta::SL12_PARAM,
    PolyQuanta::SL13_PARAM, PolyQuanta::SL14_PARAM,
    PolyQuanta::SL15_PARAM, PolyQuanta::SL16_PARAM
};

const int PolyQuanta::OFF_PARAM[16] = {
    PolyQuanta::OFF1_PARAM,  PolyQuanta::OFF2_PARAM,
    PolyQuanta::OFF3_PARAM,  PolyQuanta::OFF4_PARAM,
    PolyQuanta::OFF5_PARAM,  PolyQuanta::OFF6_PARAM,
    PolyQuanta::OFF7_PARAM,  PolyQuanta::OFF8_PARAM,
    PolyQuanta::OFF9_PARAM,  PolyQuanta::OFF10_PARAM,
    PolyQuanta::OFF11_PARAM, PolyQuanta::OFF12_PARAM,
    PolyQuanta::OFF13_PARAM, PolyQuanta::OFF14_PARAM,
    PolyQuanta::OFF15_PARAM, PolyQuanta::OFF16_PARAM
};

const int PolyQuanta::QZ_PARAM[16] = {
    PolyQuanta::QZ1_PARAM,  PolyQuanta::QZ2_PARAM,
    PolyQuanta::QZ3_PARAM,  PolyQuanta::QZ4_PARAM,
    PolyQuanta::QZ5_PARAM,  PolyQuanta::QZ6_PARAM,
    PolyQuanta::QZ7_PARAM,  PolyQuanta::QZ8_PARAM,
    PolyQuanta::QZ9_PARAM,  PolyQuanta::QZ10_PARAM,
    PolyQuanta::QZ11_PARAM, PolyQuanta::QZ12_PARAM,
    PolyQuanta::QZ13_PARAM, PolyQuanta::QZ14_PARAM,
    PolyQuanta::QZ15_PARAM, PolyQuanta::QZ16_PARAM
};

// --- MOS helpers needing full PolyQuanta definition -------------------------------------------
namespace hi { namespace music { namespace mos {
    void buildMaskFromCycle(PolyQuanta* mod, int N, const std::vector<int>& pcs, bool followsRoot){
        if(!mod||N<=0) return;
        if(N==12) mod->customMask12=0u; else if(N==24) mod->customMask24=0u; else mod->customMaskGeneric.assign(N,0);
        for(int p: pcs){
            if(p<0||p>=N) continue;
            int bit = followsRoot? p : ((mod->rootNote + p)%N+N)%N;
            if(N==12) mod->customMask12|=(1u<<bit);
            else if(N==24) mod->customMask24|=(1u<<bit);
            else {
                if((int)mod->customMaskGeneric.size()!=N) mod->customMaskGeneric.assign(N,0);
                mod->customMaskGeneric[(size_t)bit]=1;
            }
        }
    }
    bool detectCurrentMOS(PolyQuanta* mod, int& mOut, int& gOut){
        if(!mod) return false;
        int N = (mod->tuningMode==0 ? mod->edo : mod->tetSteps);
        if(N < 2 || N > 24) { mod->mosCache.valid=false; return false; }
        // Build cache key
        uint64_t h = mod->hashMask(N);
        bool keyMatch = mod->mosCache.valid &&
            mod->mosCache.N==N &&
            mod->mosCache.tuningMode==mod->tuningMode &&
            mod->mosCache.edo==mod->edo &&
            mod->mosCache.tetSteps==mod->tetSteps &&
            mod->mosCache.rootNote==mod->rootNote &&
            mod->mosCache.useCustom==mod->useCustomScale &&
            mod->mosCache.followsRoot==mod->customScaleFollowsRoot &&
            mod->mosCache.maskHash==h;
        if(keyMatch){
            if(mod->mosCache.found){ mOut=mod->mosCache.m; gOut=mod->mosCache.g; }
            return mod->mosCache.found;
        }
        // Recompute
        mod->mosCache.valid = true;
        mod->mosCache.N = N;
        mod->mosCache.tuningMode = mod->tuningMode;
        mod->mosCache.edo = mod->edo;
        mod->mosCache.tetSteps = mod->tetSteps;
        mod->mosCache.rootNote = mod->rootNote;
        mod->mosCache.useCustom = mod->useCustomScale;
        mod->mosCache.followsRoot = mod->customScaleFollowsRoot;
        mod->mosCache.maskHash = h;
        mod->mosCache.found = false;
        mod->mosCache.m = 0; mod->mosCache.g = 0;
        if(!mod->useCustomScale){ return false; }
        // Gather pitch classes (raw mask interpretation)
        std::vector<int> pcs;
        pcs.reserve(32);
        if(N==12){ for(int i=0;i<12;++i) if((mod->customMask12>>i)&1u) pcs.push_back(i); }
        else if(N==24){ for(int i=0;i<24;++i) if((mod->customMask24>>i)&1u) pcs.push_back(i); }
        else { if((int)mod->customMaskGeneric.size()!=N) return false; for(int i=0;i<N;++i) if(mod->customMaskGeneric[(size_t)i]) pcs.push_back(i); }
        // Bounds
        if(pcs.size()<2 || pcs.size()>24) return false;
        // Normalization: rotate so reference (root or min) maps to 0
        int rotateBy = 0;
        if(mod->customScaleFollowsRoot) {
            rotateBy = 0; // already relative to root
        } else if(mod->useCustomScale) {
            // absolute PCs; rotate by -rootNote
            int r = mod->rootNote % N; if(r<0) r+=N; rotateBy = (N - r) % N;
        }
        if(!mod->customScaleFollowsRoot && !mod->useCustomScale) {
            // unreachable given earlier branch, but keep for safety
            int mn = *std::min_element(pcs.begin(),pcs.end()); rotateBy = (N - (mn%N)+N)%N;
        }
        for(int& p : pcs){ p = (p + rotateBy) % N; }
        std::sort(pcs.begin(),pcs.end()); pcs.erase(std::unique(pcs.begin(),pcs.end()), pcs.end());
        if(pcs.size()<2 || pcs.size()>24) return false;
        int m = (int)pcs.size();
        // Try all coprime generators
        for(int g=1; g<N; ++g){
            if(gcdInt(g,N)!=1) continue;
            auto cyc=generateCycle(N,g,m);
            if((int)cyc.size()!=m) continue;
            if(cyc==pcs){
                mod->mosCache.found=true; mod->mosCache.m=m; mod->mosCache.g=g;
                mOut=m; gOut=g; return true;
            }
        }
        return false;
    }
} } }


/*
-----------------------------------------------------------------------
 Section: ModuleWidget (panel layout and menus)
 - Lays out global shapes, dual‑mode knobs with mode toggles, and an
     8×2 grid of Slew/Offset controls with bipolar LEDs for 16 channels.
 - Custom Trimpot subclass augments context menus with per‑control randomize
     lock/opt‑in and, for Offset knobs, per‑channel quantization controls.
 - Main module context menu groups Output, Controls, and Quantization.
-----------------------------------------------------------------------
*/
struct PolyQuantaWidget : ModuleWidget {
	PolyQuantaWidget(PolyQuanta* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/PolyQuanta.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    // ---------------------------------------------------------------
    // Placement constants — all component positions defined here
    // - Units use millimeters (mm) for consistency with panel SVGs.
    // - cxMM is computed from runtime panel width so any HP change
    //   requires no code edits.
    // ---------------------------------------------------------------
    // Center X in millimeters (derived from pixels using 1HP = 5.08 mm)
    const float pxPerMM = RACK_GRID_WIDTH / 5.08f;
    const float cxMM    = (box.size.x * 0.5f) / pxPerMM;
    // Global labels/knobs rows
    const float yShapeMM   = 17.5f;   // Rise/Fall shape row (under labels)
    const float yGlobalMM  = 27.8f;   // Global Slew/Offset row
    // Horizontal offsets (separated so Global Slew/Offset can be adjusted independently of Rise/Fall)
    const float dxColShapesMM  = 17.5f;   // Rise/Fall (shape) column offset from center
    const float dxColGlobalsMM = 19.5f;   // Global Slew/Offset column offset from center
    const float dxToggleMM = 7.0f;    // Inset for the tiny CKSS toggles near global knobs
    // 8x2 channel grid
    const float yRow0MM    = 41.308f; // First row Y
    const float rowDyMM    = 8.252f;  // Row step
    const float ledDxMM    = 1.2f;    // LED offset from center
    const float knobDx1MM  = 17.0f;    // Inner knob offset from LED
    const float knobDx2MM  = 25.0f;   // Outer knob offset from LED
    // Per-channel cents readouts (text) — offsets from each row center
    // - Horizontal offsets are measured from the panel center (cxMM).
    //   They include the LED inset (ledDxMM) plus additional spacing so text
    //   clears the LED graphic. Adjust here to move readouts left/right.
    // - Vertical offset is relative to the row centerline (yRowN). Adjust
    //   to raise/lower the readouts as needed.
    const float dxCentsLeftMM  = -(ledDxMM + 8.0f); // Left column cents X offset from center
    const float dxCentsRightMM =  (ledDxMM + 8.0f); // Right column cents X offset from center
    const float dyCentsMM      =  0.0f;            // Cents text Y offset from row center
    // Bottom I/O and button row
    const float yInOutMM   = 114.000f; // IN/OUT jacks Y
    const float yTrigMM    = 122.000f; // Randomize trigger jack Y
    const float yBtnMM     = 106.000f; // Randomize button Y
    const float dxPortsMM  = 22.000f;  // Horizontal offset from center to IN/OUT jacks
    // Auto-randomize new control placements (cluster around existing Randomize button)
    const float yRndKnobMM = 114.0f;    // Row for Time & Amount knobs
    const float dxRndKnobMM = 10.0f;   // Horizontal offset from center to each knob
    const float yRndSwMM   = 122.0f;   // Switch row aligned with button
    const float dxRndSwMM  = 10.0f;    // Horizontal offset for switches (Auto left, Sync right)

    // Custom knob with per-control randomize lock in context menu
        struct LockableTrimpot : Trimpot {
            void appendContextMenu(Menu* menu) override {
                Trimpot::appendContextMenu(menu);
                auto* pq = getParamQuantity();
                if (!pq || !pq->module) return;
                auto* m = dynamic_cast<PolyQuanta*>(pq->module);
                if (!m) return;
                int pid = pq->paramId;
                bool* lockPtr = nullptr;
                bool* allowPtr = nullptr;
                bool isSlew = false, isOffset = false, isRise = false, isFall = false;
                int chIndex = -1; // for per-channel features
        // Dual-mode always-on toggles on the respective global knobs
                if (pid == PolyQuanta::GLOBAL_SLEW_PARAM) {
                    menu->addChild(new MenuSeparator);
                    menu->addChild(rack::createMenuLabel("Dual-mode: Global Slew"));
                    menu->addChild(rack::createBoolMenuItem("Attenuverter always on", "", [m]{ return m->attenuverterAlwaysOn; }, [m](bool v){ m->attenuverterAlwaysOn = v; }));
                    menu->addChild(rack::createBoolMenuItem("Global slew always on", "", [m]{ return m->slewAddAlwaysOn; }, [m](bool v){ m->slewAddAlwaysOn = v; }));
                }
                if (pid == PolyQuanta::GLOBAL_OFFSET_PARAM) {
                    menu->addChild(new MenuSeparator);
                    menu->addChild(rack::createMenuLabel("Dual-mode: Global Offset"));
                    menu->addChild(rack::createBoolMenuItem("Global offset always on", "", [m]{ return m->globalOffsetAlwaysOn; }, [m](bool v){ m->globalOffsetAlwaysOn = v; }));
                    menu->addChild(rack::createBoolMenuItem("Range offset always on", "", [m]{ return m->rangeOffsetAlwaysOn; }, [m](bool v){ m->rangeOffsetAlwaysOn = v; }));
                }
                // Detect which param and select the right lock
                for (int i = 0; i < 16; ++i) {
                    if (pid == PolyQuanta::SL_PARAM[i]) { lockPtr = &m->lockSlew[i]; allowPtr = &m->allowSlew[i]; isSlew = true; chIndex = i; break; }
                    if (pid == PolyQuanta::OFF_PARAM[i]) { lockPtr = &m->lockOffset[i]; allowPtr = &m->allowOffset[i]; isOffset = true; chIndex = i; break; }
                }
                if (pid == PolyQuanta::RISE_SHAPE_PARAM) { lockPtr = &m->lockRiseShape; allowPtr = &m->allowRiseShape; isRise = true; }
                if (pid == PolyQuanta::FALL_SHAPE_PARAM) { lockPtr = &m->lockFallShape; allowPtr = &m->allowFallShape; isFall = true; }
                // Per-channel Quantization section for Offset knobs: Octave shift (pre-quant)
                if (isOffset && chIndex >= 0) {
                    menu->addChild(new MenuSeparator);
                    menu->addChild(rack::createMenuLabel("Quantization"));
                    // Per-channel offset quantization mode
                    menu->addChild(rack::createSubmenuItem("Quantize knob", "", [m, chIndex](rack::ui::Menu* sm){
                        sm->addChild(rack::createCheckMenuItem("None", "", [m,chIndex]{ return m->quantizeOffsetModeCh[chIndex]==0; }, [m,chIndex]{ m->quantizeOffsetModeCh[chIndex]=0; }));
                        sm->addChild(rack::createCheckMenuItem("Semitones", "", [m,chIndex]{ return m->quantizeOffsetModeCh[chIndex]==1; }, [m,chIndex]{ m->quantizeOffsetModeCh[chIndex]=1; }));
                        sm->addChild(rack::createCheckMenuItem("Cents", "", [m,chIndex]{ return m->quantizeOffsetModeCh[chIndex]==2; }, [m,chIndex]{ m->quantizeOffsetModeCh[chIndex]=2; }));
                    }));
                    // Toggle quantization for this channel
                    menu->addChild(rack::createCheckMenuItem(
                        "Quantize to scale",
                        "",
                        [m, chIndex]{ return m->qzEnabled[chIndex]; },
                        [m, chIndex]{ m->qzEnabled[chIndex] = !m->qzEnabled[chIndex]; }
                    ));
                    // Quick action for this channel
                    menu->addChild(rack::createMenuItem("Reset this channel's oct shift", "", [m, chIndex]{ m->postOctShift[chIndex] = 0; }));
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
                if (!lockPtr && !allowPtr) return;
                menu->addChild(new MenuSeparator);
                menu->addChild(rack::createMenuLabel("Randomize"));
                // Decide which option to show based on global Scope toggle for this control type
                bool scopeOn = false;
                if (isSlew) scopeOn = m->randSlew;
                else if (isOffset) scopeOn = m->randOffset;
                else if (isRise || isFall) scopeOn = m->randShapes;
                if (scopeOn) {
                    // Normal mode: offer per-control lock
                    if (lockPtr)
                        menu->addChild(rack::createBoolPtrMenuItem("Don't randomize me :(", "", lockPtr));
                } else {
                    // Scope is OFF: offer per-control opt-in
                    if (allowPtr)
                        menu->addChild(rack::createBoolPtrMenuItem("Please randomize me :)", "", allowPtr));
                }
            }
        };

        // Global shape controls (Rise / Fall), placed symmetrically from panel center.
        {
            addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM - dxColShapesMM, yShapeMM)), module, PolyQuanta::RISE_SHAPE_PARAM));
            addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM + dxColShapesMM, yShapeMM)), module, PolyQuanta::FALL_SHAPE_PARAM));
        }
    // Global controls: Global slew under Rise shape, Global offset under Fall shape
        {
            addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM - dxColGlobalsMM, yGlobalMM)), module, PolyQuanta::GLOBAL_SLEW_PARAM));
            addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM + dxColGlobalsMM, yGlobalMM)), module, PolyQuanta::GLOBAL_OFFSET_PARAM));
            // Inset latching toggles for dual-mode
            addParam(createParamCentered<CKSS>(mm2px(Vec(cxMM - dxColGlobalsMM - dxToggleMM, yGlobalMM)), module, PolyQuanta::GLOBAL_SLEW_MODE_PARAM));
            addParam(createParamCentered<CKSS>(mm2px(Vec(cxMM + dxColGlobalsMM + dxToggleMM, yGlobalMM)), module, PolyQuanta::GLOBAL_OFFSET_MODE_PARAM));
        }
        // Small per-channel cents display next to each LED (relative to 0 V = middle C)
        struct CentsDisplay : TransparentWidget {
            PolyQuanta* mod = nullptr; int ch = 0; std::shared_ptr<Font> font;
            CentsDisplay(Vec centerPx, Vec sizePx, PolyQuanta* m, int channel) {
                box.size = sizePx; box.pos = centerPx.minus(sizePx.div(2)); mod = m; ch = channel;
            }
            void drawLayer(const DrawArgs& args, int layer) override {
                if (layer != 1) return; // draw on foreground layer
                if (!font) font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
                if (!font) return;
                nvgFontFaceId(args.vg, font->handle);
                nvgFontSize(args.vg, 9.f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                NVGcolor col = nvgRGB(220, 220, 220);
                nvgFillColor(args.vg, col);
                std::string txt = "—";
                if (mod) {
                    // Show only when channel is active; otherwise draw dash
                    int activeN = std::max(0, mod->polyTrans.curProcN);
                    if (ch < activeN) {
                        float v = mod->lastOut[ch];
                        int cents = (int)std::round(v * 1200.f);
                        // Clamp to practical bounds (±12000 for ±10 V)
                        if (cents > 12000) cents = 12000; else if (cents < -12000) cents = -12000;
                        txt = rack::string::f("%+dc", cents);
                    }
                }
                nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, txt.c_str(), nullptr);
            }
        };

        // Grid layout: for each row (8 rows), place [LED][Slew][Slew] on left and [Offset][Offset][LED] on right
        {
            for (int row = 0; row < 8; ++row) {
                int chL = row * 2 + 0;
                int chR = row * 2 + 1;
                float y = yRow0MM + row * rowDyMM;
                // Left side: LED, then TWO SLEWS (far-left = chL, inner-left = chR)
                addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(cxMM - ledDxMM, y)), module, PolyQuanta::CH_LIGHT + 2*chL));
                addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM - ledDxMM - knobDx2MM, y)), module, PolyQuanta::SL_PARAM[chL])); // far-left
                addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM - ledDxMM - knobDx1MM, y)), module, PolyQuanta::SL_PARAM[chR])); // inner-left
                // Cents display for left channel
                addChild(new CentsDisplay(mm2px(Vec(cxMM + dxCentsLeftMM, y + dyCentsMM)), Vec(28.f, 12.f), module, chL));

                // Right side: then TWO OFFSETS (inner-right = chL, far-right = chR), LED
                addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM + ledDxMM + knobDx1MM, y)), module, PolyQuanta::OFF_PARAM[chL])); // inner-right
                addParam(createParamCentered<LockableTrimpot>(mm2px(Vec(cxMM + ledDxMM + knobDx2MM, y)), module, PolyQuanta::OFF_PARAM[chR])); // far-right
                addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(cxMM + ledDxMM, y)), module, PolyQuanta::CH_LIGHT + 2*chR));
                // Cents display for right channel
                addChild(new CentsDisplay(mm2px(Vec(cxMM + dxCentsRightMM, y + dyCentsMM)), Vec(28.f, 12.f), module, chR));
            }
        }
        // Ports and button row: anchor to center so width changes require no code edits
        {
            // Input port (poly)
            addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(cxMM - dxPortsMM, yInOutMM)), module, PolyQuanta::IN_INPUT));
            // Randomize trigger jack (centered between IN/OUT)
            addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(cxMM, yTrigMM)), module, PolyQuanta::RND_TRIG_INPUT));
            // Randomize pushbutton slightly above the jack row (centered)
            addParam(createParamCentered<VCVButton>(mm2px(Vec(cxMM, yBtnMM)), module, PolyQuanta::RND_PARAM));
            // Output port (poly)
            addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(cxMM + dxPortsMM, yInOutMM)), module, PolyQuanta::OUT_OUTPUT));
            // Auto-randomize controls
            if (module) {
                addParam(createParamCentered<Trimpot>(mm2px(Vec(cxMM - dxRndKnobMM, yRndKnobMM)), module, PolyQuanta::RND_TIME_PARAM));
                addParam(createParamCentered<Trimpot>(mm2px(Vec(cxMM + dxRndKnobMM, yRndKnobMM)), module, PolyQuanta::RND_AMT_PARAM));
                addParam(createParamCentered<CKSS>(mm2px(Vec(cxMM - dxRndSwMM, yRndSwMM)), module, PolyQuanta::RND_AUTO_PARAM));
                addParam(createParamCentered<CKSS>(mm2px(Vec(cxMM + dxRndSwMM, yRndSwMM)), module, PolyQuanta::RND_SYNC_PARAM));
            }
        }
	}

/*
-------------------------------------------------------------------
    Menu: appendContextMenu()
    - Output: range status, channels, sum/avg, soft‑clip, range Vpp/mode.
    - Controls: offset quantize, dual‑mode always‑on flags, bulk actions.
    - Randomize: scope and magnitude.
    - Quantization: status line (EDO, root, scale, strength), then EDO,
        Root, Scale/Custom editor, Quantize strength, glide/strum controls,
        batch quantize enable, and reset all octave shifts.
-------------------------------------------------------------------
*/
        void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<PolyQuanta*>(module);
        hi::ui::menu::addSection(menu, "Output");
        // Range status directly under Output header
        {
            const char* rngMode = (m->rangeMode == 0) ? "Clip" : "Scale";
            float vpp = 2.f * m->currentClipLimit();
            menu->addChild(rack::createMenuLabel(rack::string::f("Range: %s %.0f Vpp", rngMode, vpp)));
        }
        // Channels selector: Auto or 1..16
        menu->addChild(rack::createSubmenuItem("Channels", "", [m](rack::ui::Menu* sm){
            sm->addChild(rack::createCheckMenuItem("Auto (match input)", "", [m]{ return m->forcedChannels == 0; }, [m]{ m->forcedChannels = 0; }));
            sm->addChild(new MenuSeparator);
            for (int n = 1; n <= 16; ++n) {
                sm->addChild(rack::createCheckMenuItem(rack::string::f("%d", n), "", [m,n]{ return m->forcedChannels == n; }, [m,n]{ m->forcedChannels = n; }));
            }
            sm->addChild(new MenuSeparator);
            // Channel switch fade time options
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
        hi::ui::menu::addBoolPtr(menu, "Sum to mono (post‑slew)", &m->sumToMonoOut);
        hi::ui::menu::addBoolPtr(menu, "Average when summing", &m->avgWhenSumming, [m]{ return m->sumToMonoOut; });
    hi::ui::menu::addBoolPtr(menu, "Soft clip (range + final)", &m->softClipOut);
        // Range level menu
        menu->addChild(rack::createSubmenuItem("Range (Vpp)", "", [m](rack::ui::Menu* sm){
            struct Opt { const char* label; int idx; };
            const Opt opts[] = {
                {"20 V", 0}, {"15 V", 1}, {"10 V", 2}, {"5 V", 3}, {"2 V", 4}, {"1 V", 5}
            };
            for (auto& o : opts) {
                sm->addChild(rack::createCheckMenuItem(o.label, "", [m,o]{ return m->clipVppIndex == o.idx; }, [m,o]{ m->clipVppIndex = o.idx; }));
            }
        }));
        // Range mode menu
        menu->addChild(rack::createSubmenuItem("Range mode (pre‑quant)", "", [m](rack::ui::Menu* sm){
            sm->addChild(rack::createCheckMenuItem("Clip", "", [m]{ return m->rangeMode == 0; }, [m]{ m->rangeMode = 0; }));
            sm->addChild(rack::createCheckMenuItem("Scale", "", [m]{ return m->rangeMode == 1; }, [m]{ m->rangeMode = 1; }));
        }));
    hi::ui::menu::addSection(menu, "Controls");
        // Batch apply offset quantization mode to all channels
        menu->addChild(rack::createSubmenuItem("Quantize all offsets", "", [m](rack::ui::Menu* sm){
            auto applyAll=[m](int mode){ m->quantizeOffsetMode = mode; for(int i=0;i<16;++i) m->quantizeOffsetModeCh[i]=mode; };
            sm->addChild(rack::createCheckMenuItem("None", "", [m]{ return m->quantizeOffsetMode == 0; }, [applyAll]{ applyAll(0); }));
            sm->addChild(rack::createCheckMenuItem("Semitones (scale steps)", "", [m]{ return m->quantizeOffsetMode == 1; }, [applyAll]{ applyAll(1); }));
            sm->addChild(rack::createCheckMenuItem("Cents (1/1200 V)", "", [m]{ return m->quantizeOffsetMode == 2; }, [applyAll]{ applyAll(2); }));
        }));
        // Quick actions
        menu->addChild(rack::createMenuItem("Set all slews to 0", "", [m]{
            for (int i = 0; i < 16; ++i) m->params[PolyQuanta::SL_PARAM[i]].setValue(0.f);
        }));
        menu->addChild(rack::createMenuItem("Set all offsets to 0", "", [m]{
            for (int i = 0; i < 16; ++i) m->params[PolyQuanta::OFF_PARAM[i]].setValue(0.f);
        }));
    // Export richer panel snapshot SVG (panel artwork + component approximations)
    menu->addChild(rack::createMenuItem("Export layout SVG (user folder)", "", [this]{
            // Delegated to extracted implementation (behavior identical to original inline code).
            PanelExport::exportPanelSnapshot(this, "PolyQuanta", "res/PolyQuanta.svg");
        }));
    // Randomize
    menu->addChild(rack::createSubmenuItem("Randomize", "", [m](rack::ui::Menu* sm){
        // Scope toggles
        sm->addChild(rack::createSubmenuItem("Scope", "", [m](rack::ui::Menu* sm2){
            sm2->addChild(rack::createBoolPtrMenuItem("Slews", "", &m->randSlew));
            sm2->addChild(rack::createBoolPtrMenuItem("Offsets", "", &m->randOffset));
            sm2->addChild(rack::createBoolPtrMenuItem("Shapes", "", &m->randShapes));
        }));
    // Amount now driven by front-panel knob (RND_AMT_PARAM); submenu retained only as informational label
    sm->addChild(rack::createMenuLabel("Amount: front panel knob"));
    }));

    // Quantization (musical) settings
    hi::ui::menu::addSection(menu, "Quantization");
        // Quantizer position submenu (new)
        menu->addChild(rack::createSubmenuItem("Signal chain →", "", [m](rack::ui::Menu* sm){
            // Quantizer position toggle (no math changes).
            sm->addChild(rack::createCheckMenuItem("Pitch-bend: Quantize → Slew (Q→S)", "", [m]{ return m->quantizerPos == PolyQuanta::QuantizerPos::Pre; }, [m]{ m->quantizerPos = PolyQuanta::QuantizerPos::Pre; }));
            sm->addChild(rack::createCheckMenuItem("Pitch-accurate: Slew → Quantize (S→Q)", "", [m]{ return m->quantizerPos == PolyQuanta::QuantizerPos::Post; }, [m]{ m->quantizerPos = PolyQuanta::QuantizerPos::Post; }));
        }));
        // Status line
        {
            int steps = (m->tuningMode==0) ? m->edo : m->tetSteps;
            // Root label
            std::string rootStr;
            if (m->tuningMode==0 && steps == 12) {
                static const char* noteNames[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                int rn = (m->rootNote % 12 + 12) % 12;
                rootStr = noteNames[rn];
            } else {
                rootStr = rack::string::f("%d", (m->rootNote % std::max(1, steps) + std::max(1, steps)) % std::max(1, steps));
            }
            // Scale label
            std::string scaleStr;
            if (m->tuningMode==0 && steps == 12 && !m->useCustomScale) {
                int idx = (m->scaleIndex >= 0 && m->scaleIndex < hi::music::NUM_SCALES12) ? m->scaleIndex : 0;
                scaleStr = hi::music::scales12()[idx].name;
            } else if (m->tuningMode==0 && steps == 24 && !m->useCustomScale) {
                int idx = (m->scaleIndex >= 0 && m->scaleIndex < hi::music::NUM_SCALES24) ? m->scaleIndex : 0;
                scaleStr = hi::music::scales24()[idx].name;
            } else {
                scaleStr = "Custom";
            }
            int pct = (int)std::round(clamp(m->quantStrength, 0.f, 1.f) * 100.f);
            float period = (m->tuningMode==0)?1.f:m->tetPeriodOct;
            int N = std::max(1, steps);
            float dV = period / (float)N;
            float stepCents = 1200.f * dV;
            float maxStick = std::floor(0.4f * stepCents);
            const char* roundStr = "Directional Snap";
            switch (m->quantRoundMode) {
                case 0: roundStr = "Directional Snap"; break;
                case 1: roundStr = "Nearest"; break;
                case 2: roundStr = "Up"; break;
                case 3: roundStr = "Down"; break;
            }
            // Attempt ephemeral MOS detection for status annotation
            int mosM=0, mosG=0; bool mosOk = hi::music::mos::detectCurrentMOS(m, mosM, mosG);
            std::string mosStr = mosOk? rack::string::f(", MOS %d/gen %d", mosM, mosG) : "";
            menu->addChild(rack::createMenuLabel(rack::string::f(
                "Status: %s %d, Root %s, Scale %s%s, Strength %d%%, Round %s, Stickiness %.1f¢ (max %.0f¢)",
                (m->tuningMode==0?"EDO":"TET"), steps, rootStr.c_str(), scaleStr.c_str(), mosStr.c_str(), pct,
                roundStr, m->stickinessCents, maxStick)));
        }
        // Tuning system selector
        menu->addChild(rack::createSubmenuItem("Tuning system", "", [m](rack::ui::Menu* sm){
            sm->addChild(rack::createCheckMenuItem("EDO (octave)", "", [m]{ return m->tuningMode==0; }, [m]{ m->tuningMode=0; m->invalidateMOSCache(); }));
            sm->addChild(rack::createCheckMenuItem("TET (non-octave)", "", [m]{ return m->tuningMode==1; }, [m]{ m->tuningMode=1; m->invalidateMOSCache(); }));
        }));
    // EDO selection: curated quick picks with descriptions + full range navigator (labels show cents/step)
        menu->addChild(rack::createSubmenuItem("EDO", "", [m](rack::ui::Menu* sm){
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
                {24, "Quarter-tone classic; bends & nuance."},
                {25, "Mid-resolution microtonal; pairs with 50."},
                {26, "Blackwood sweet spot; flexible modes."},
                {31, "Huygens/Fokker; elegant 5-limit control."},
                {34, "Tighter 17-EDO lattice variant."},
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
            sm->addChild(rack::createMenuLabel("Quick picks"));
            for (const auto& q : quicks) {
                int e = q.edo;
                float cents = 1200.f / (float)e;
                sm->addChild(rack::createCheckMenuItem(
                    rack::string::f("%d-EDO (%.2f¢)", e, cents), q.desc,
                    [m,e]{ return m->tuningMode==0 && m->edo==e; },
                    [m,e]{ m->tuningMode=0; m->edo=e; m->rootNote%=e; }
                ));
            }
            sm->addChild(new MenuSeparator);
            sm->addChild(rack::createMenuLabel("N-EDO"));
            auto addRange = [m](rack::ui::Menu* dst, int a, int b){
                for (int e = a; e <= b; ++e) {
                    float cents = 1200.f / (float)e;
                    dst->addChild(rack::createCheckMenuItem(
                        rack::string::f("%d-EDO (%.2f¢)", e, cents), "",
                        [m,e]{ return m->tuningMode==0 && m->edo==e; },
                        [m,e]{ m->tuningMode=0; m->edo=e; m->rootNote%=e; }
                    ));
                }
            };
            sm->addChild(rack::createSubmenuItem("1-30",  "", [addRange](rack::ui::Menu* sm2){ addRange(sm2, 1, 30); }));
            sm->addChild(rack::createSubmenuItem("31-60", "", [addRange](rack::ui::Menu* sm2){ addRange(sm2, 31, 60); }));
            sm->addChild(rack::createSubmenuItem("61-90", "", [addRange](rack::ui::Menu* sm2){ addRange(sm2, 61, 90); }));
            sm->addChild(rack::createSubmenuItem("91-120","", [addRange](rack::ui::Menu* sm2){ addRange(sm2, 91, 120); }));
        }));
        // Root: label entries by their 12-EDO pitch-class mapping from 1 V/oct (0 -> C)
        menu->addChild(rack::createSubmenuItem("Root", "", [m](rack::ui::Menu* sm) {
            int N = (m->tuningMode==0 ? m->edo : m->tetSteps);
            if (N <= 0) N = 12;
            float period = (m->tuningMode==0) ? 1.f : ((m->tetPeriodOct > 0.f) ? m->tetPeriodOct : std::log2(3.f/2.f));
            // Capture N and period by value so that submenu population works after this lambda returns
            auto addRange = [m,N,period](rack::ui::Menu* menuDest, int start, int end){
                static const char* noteNames[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                for (int n = start; n <= end && n < N; ++n) {
                    float semis = (float)n * 12.f * period / (float)N;
                    int nearestPc = (int)std::round(semis);
                    float delta = semis - (float)nearestPc;
                    float err = std::fabs(delta);
                    bool exact = false;
                    if (m->tuningMode==0 && (N % 12) == 0) {
                        int stepPerSemi = N / 12;
                        exact = (n % stepPerSemi) == 0;
                    } else {
                        exact = err <= 1e-6f;
                    }
                    int pc12 = ((nearestPc % 12) + 12) % 12;
                    std::string label;
                    if (exact) {
                        label = rack::string::f("%d (%s)", n, noteNames[pc12]);
                    } else if (err <= 0.05f) {
                        int cents = (int)std::round(delta * 100.f);
                        if (cents != 0) label = rack::string::f("%d (≈%s %+dc)", n, noteNames[pc12], cents);
                        else label = rack::string::f("%d (≈%s)", n, noteNames[pc12]);
                    } else {
                        label = rack::string::f("%d", n);
                    }
                    menuDest->addChild(rack::createCheckMenuItem(label, "", [m,n]{ return m->rootNote == n; }, [m,n]{ m->rootNote = n; m->invalidateMOSCache(); }));
                }
            };
            if (N > 72) {
                // Split into thirds; distribute remainder to earlier groups.
                int base = N / 3;
                int rem = N % 3;
                int size1 = base + (rem > 0 ? 1 : 0);
                int size2 = base + (rem > 1 ? 1 : 0);
                // third size3 is implied by remainder; no variable needed
                int s1 = 0;            int e1 = size1 - 1;
                int s2 = e1 + 1;       int e2 = s2 + size2 - 1;
                int s3 = e2 + 1;       int e3 = N - 1;
                sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", s1, e1), "", [addRange,s1,e1](rack::ui::Menu* sm2){ addRange(sm2, s1, e1); }));
                sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", s2, e2), "", [addRange,s2,e2](rack::ui::Menu* sm2){ addRange(sm2, s2, e2); }));
                sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", s3, e3), "", [addRange,s3,e3](rack::ui::Menu* sm2){ addRange(sm2, s3, e3); }));
            } else if (N > 36) {
                // Split into halves (lower gets extra when odd)
                int halfLo = (N % 2 == 1) ? ((N + 1) / 2) : (N / 2);
                int loStart = 0;           int loEnd = halfLo - 1;
                int hiStart = halfLo;      int hiEnd = N - 1;
                sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", loStart, loEnd), "", [addRange,loStart,loEnd](rack::ui::Menu* sm2){ addRange(sm2, loStart, loEnd); }));
                sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", hiStart, hiEnd), "", [addRange,hiStart,hiEnd](rack::ui::Menu* sm2){ addRange(sm2, hiStart, hiEnd); }));
            } else {
                addRange(sm, 0, N-1);
            }
        }));
        // Scale or Custom degrees editor
        menu->addChild(rack::createSubmenuItem("Scale / Custom", "", [m](rack::ui::Menu* sm) {
            // Toggle custom scale; when turning ON and rememberCustomScale is false, seed from current named scale
            sm->addChild(rack::createCheckMenuItem("Use custom scale", "", [m]{ return m->useCustomScale; }, [m]{
                bool was = m->useCustomScale;
                m->useCustomScale = !m->useCustomScale;
                if (!was && m->useCustomScale && !m->rememberCustomScale) {
                    if (m->tuningMode==0 && m->edo == 12) {
                        int idx = (m->scaleIndex >= 0 && m->scaleIndex < hi::music::NUM_SCALES12) ? m->scaleIndex : 0;
                        m->customMask12 = hi::music::scales12()[idx].mask;
                    } else if (m->tuningMode==0 && m->edo == 24) {
                        int idx = (m->scaleIndex >= 0 && m->scaleIndex < hi::music::NUM_SCALES24) ? m->scaleIndex : 0;
                        m->customMask24 = hi::music::scales24()[idx].mask;
                    } else {
                        // For arbitrary N (EDO != 12/24 or TET), seed generic mask (all on)
                        int N = (m->tuningMode==0 ? m->edo : m->tetSteps);
                        if (N <= 0) N = 12;
                        m->customMaskGeneric.assign((size_t)N, 1);
                    }
                }
                m->invalidateMOSCache();
            }));
            sm->addChild(rack::createCheckMenuItem("Remember custom scale", "", [m]{ return m->rememberCustomScale; }, [m]{ m->rememberCustomScale = !m->rememberCustomScale; }));
            sm->addChild(rack::createCheckMenuItem("Custom scales follow root", "", [m]{ return m->customScaleFollowsRoot; }, [m]{ m->customScaleFollowsRoot = !m->customScaleFollowsRoot; m->invalidateMOSCache(); }));
            if (m->tuningMode==0 && m->edo == 12 && !m->useCustomScale) {
                for (int i = 0; i < hi::music::NUM_SCALES12; ++i) {
                    sm->addChild(rack::createCheckMenuItem(hi::music::scales12()[i].name, "", [m,i]{ return m->scaleIndex == i; }, [m,i]{ m->scaleIndex = i; }));
                }
            } else if (m->tuningMode==0 && m->edo == 24 && !m->useCustomScale) {
                for (int i = 0; i < hi::music::NUM_SCALES24; ++i) {
                    sm->addChild(rack::createCheckMenuItem(hi::music::scales24()[i].name, "", [m,i]{ return m->scaleIndex == i; }, [m,i]{ m->scaleIndex = i; }));
                }
            } else {
                // MOS presets submenu (current EDO)
                sm->addChild(rack::createSubmenuItem("MOS presets (current EDO)", "", [m](rack::ui::Menu* smMos){
                    if (m->tuningMode!=0) return; 
                    int N = std::max(1,m->edo);
                    auto it = hi::music::mos::curated.find(N);
                    if (it==hi::music::mos::curated.end()) return;
                    for (int msz : it->second){
                        if(msz<2) continue;
                        int mClamped = std::min(std::min(msz,N),24);
                        std::string lbl = rack::string::f("%d notes", mClamped);
                        smMos->addChild(rack::createSubmenuItem(lbl, "", [m,N,mClamped](rack::ui::Menu* smAdv){
                            // Build generator choices only; do NOT modify scale until user selects.
                            smAdv->addChild(rack::createMenuLabel("Generators"));
                            int bestG = hi::music::mos::findBestGenerator(N,mClamped);
                            for(int gTest=1; gTest<N; ++gTest){
                                if(hi::music::mos::gcdInt(gTest,N)!=1) continue;
                                auto cyc=hi::music::mos::generateCycle(N,gTest,mClamped);
                                if((int)cyc.size()!=mClamped) continue;
                                if(!hi::music::mos::isMOS(cyc,N)) continue;
                                std::string pat = hi::music::mos::patternLS(cyc,N);
                                bool isBest = (gTest==bestG);
                                std::string glabel = rack::string::f("gen %d %s%s", gTest, pat.c_str(), isBest?" (best)":"");
                                smAdv->addChild(rack::createMenuItem(glabel, "", [m,N,gTest,mClamped]{
                                    auto cyc2=hi::music::mos::generateCycle(N,gTest,mClamped);
                                    m->useCustomScale=true;
                                    m->customScaleFollowsRoot=true;
                                    hi::music::mos::buildMaskFromCycle(m,N,cyc2,true);
                                    m->scaleIndex=0;
                                    m->invalidateMOSCache();
                                }));
                            }
                        }));
                    }
                }));
                // Custom scale editing helpers
                sm->addChild(rack::createMenuItem("Select All Notes", "", [m]{
                    int N = std::max(1, (m->tuningMode==0 ? m->edo : m->tetSteps));
                    if (N == 12) m->customMask12 = 0xFFFu;
                    else if (N == 24) m->customMask24 = 0xFFFFFFu;
                    else m->customMaskGeneric.assign((size_t)N, 1);
                    m->invalidateMOSCache();
                }));
                sm->addChild(rack::createMenuItem("Clear All Notes", "", [m]{
                    int N = std::max(1, (m->tuningMode==0 ? m->edo : m->tetSteps));
                    if (N == 12) m->customMask12 = 0u;
                    else if (N == 24) m->customMask24 = 0u;
                    else m->customMaskGeneric.assign((size_t)N, 0);
                    m->invalidateMOSCache();
                }));
                sm->addChild(rack::createMenuItem("Invert Selection", "", [m]{
                    int N = std::max(1, (m->tuningMode==0 ? m->edo : m->tetSteps));
                    if (N == 12) m->customMask12 = (~m->customMask12) & 0xFFFu;
                    else if (N == 24) m->customMask24 = (~m->customMask24) & 0xFFFFFFu;
                    else {
                        if ((int)m->customMaskGeneric.size() != N) m->customMaskGeneric.assign((size_t)N, 0);
                        for (int i = 0; i < N; ++i) m->customMaskGeneric[(size_t)i] = m->customMaskGeneric[(size_t)i] ? 0 : 1;
                    }
                    m->invalidateMOSCache();
                }));
                // Quick action: select degrees aligned to 12-EDO semitones (EDO mode only)
                if (m->tuningMode == 0) {
                    sm->addChild(rack::createMenuItem("Custom: Select aligned 12-EDO notes", "", [m]{
                        int N = std::max(1, m->edo);
                        auto setDeg = [&](int degAbs, bool on){
                            // degAbs is absolute index 0..N-1. Write into the mask's index space.
                            int bit = m->customScaleFollowsRoot ? (((degAbs - m->rootNote) % N + N) % N) : degAbs;
                            if (N == 12) {
                                if (on) m->customMask12 |= (1u << bit); else m->customMask12 &= ~(1u << bit);
                            } else if (N == 24) {
                                if (on) m->customMask24 |= (1u << bit); else m->customMask24 &= ~(1u << bit);
                            } else {
                                if ((int)m->customMaskGeneric.size() != N) m->customMaskGeneric.assign((size_t)N, 0);
                                m->customMaskGeneric[(size_t)bit] = on ? 1 : 0;
                            }
                        };
                        if (N % 12 == 0) {
                            // Multiples of 12 EDO: choose aligned absolute indices n = (root + d) % N where d is multiple of N/12
                            int stepPerSemi = N / 12;
                            for (int d = 0; d < N; ++d) {
                                int n = ((m->rootNote + d) % N + N) % N;
                                bool aligned = (d % stepPerSemi) == 0;
                                setDeg(n, aligned);
                            }
                        } else {
                            // Non-multiples: mark absolute indices near semitone boundaries once root offset is included
                            for (int d = 0; d < N; ++d) {
                                int n = ((m->rootNote + d) % N + N) % N;
                                float semis = (float)n * 12.f / (float)N;
                                float nearest = std::round(semis);
                                float err = std::fabs(semis - nearest);
                                bool aligned = (err <= 0.05f);
                                setDeg(n, aligned);
                            }
                        }
                        m->invalidateMOSCache();
                    }));
                }
                sm->addChild(new MenuSeparator);
                sm->addChild(rack::createMenuLabel("Degrees"));
                int N = (m->tuningMode==0 ? m->edo : m->tetSteps);
                if (N <= 0) N = 1;
                auto addDegree = [&](rack::ui::Menu* menuDeg, int d){
                    std::string label;
                    // Smart labeling: show 12-EDO pitch-class names when aligned.
                    // Aligned when (root + d) lands on a 12‑EDO boundary for multiples of 12;
                    // otherwise, show nearest 12‑EDO pitch (≈) within a small cents threshold.
                    static const char* noteNames12[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                    bool named = false;
                    // Degree labels are relative to Root so degree 1 = Root:
                    // stepIndex = (root + d) % N, where d is zero-based (degreeNumber-1).
                    float period = (m->tuningMode==0) ? 1.f : ((m->tetPeriodOct > 0.f) ? m->tetPeriodOct : std::log2(3.f/2.f));
                    int stepIndex = ((m->rootNote + d) % N + N) % N;
                    float semis = (float)stepIndex * 12.f * period / (float)N; // absolute fractional semitone index
                    int nearestPc = (int)std::round(semis);
                    float delta = semis - (float)nearestPc; // in semitones (±)
                    float err = std::fabs(delta);
                    bool exact = false;
                    if (m->tuningMode==0 && (N % 12 == 0)) {
                        int stepPerSemi = N / 12;
                        exact = (stepIndex % stepPerSemi) == 0;
                    } else {
                        exact = err <= 1e-6f; // unlikely unless semis hits integer exactly
                    }
                    if (exact) {
                        int pc12 = ((nearestPc % 12) + 12) % 12;
                        label = rack::string::f("%d (%s)", d + 1, noteNames12[pc12]);
                        named = true;
                    } else if (err <= 0.05f) { // within ~6 cents: show approximate with signed cents
                        int pc12 = ((nearestPc % 12) + 12) % 12;
                        int cents = (int)std::round(delta * 100.f);
                        if (cents != 0) label = rack::string::f("%d (≈%s %+dc)", d + 1, noteNames12[pc12], cents);
                        else label = rack::string::f("%d (≈%s)", d + 1, noteNames12[pc12]);
                        named = true;
                    }
                    if (!named) label = rack::string::f("%d", d + 1);
            menuDeg->addChild(rack::createCheckMenuItem(
                        label,
                        "",
                        [m,d]{
                            int Nloc = std::max(1, (m->tuningMode==0 ? m->edo : m->tetSteps));
                            int bit = m->customScaleFollowsRoot ? d : ((m->rootNote + d) % Nloc + Nloc) % Nloc;
                            if (Nloc==12) return ((m->customMask12 >> bit) & 1u) != 0u;
                            if (Nloc==24) return ((m->customMask24 >> bit) & 1u) != 0u;
                            if ((int)m->customMaskGeneric.size() != Nloc) return false;
                            return m->customMaskGeneric[(size_t)bit] != 0;
                        },
                        [m,d]{
                            int Nloc = std::max(1, (m->tuningMode==0 ? m->edo : m->tetSteps));
                            int bit = m->customScaleFollowsRoot ? d : ((m->rootNote + d) % Nloc + Nloc) % Nloc;
                            if (Nloc==12) m->customMask12 ^= (1u << bit);
                            else if (Nloc==24) m->customMask24 ^= (1u << bit);
                            else {
                                if ((int)m->customMaskGeneric.size() != Nloc) m->customMaskGeneric.assign((size_t)Nloc, 0);
                                m->customMaskGeneric[(size_t)bit] = m->customMaskGeneric[(size_t)bit] ? 0 : 1;
                            }
                        }
                    ));
                };
                if (N <= 36) {
                    for (int d = 0; d < N; ++d) addDegree(sm, d);
                } else if (N <= 72) {
                    // Halves
                    int halfLo = (N % 2 == 1) ? ((N + 1) / 2) : (N / 2);
                    int loStart = 0;           int loEnd = halfLo - 1;
                    int hiStart = halfLo;      int hiEnd = N - 1;
                    auto addRange = [addDegree](rack::ui::Menu* dest, int start, int end){ for (int d = start; d <= end; ++d) addDegree(dest, d); };
                    sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", loStart + 1, loEnd + 1), "", [loStart,loEnd,addRange](rack::ui::Menu* sm2){ addRange(sm2, loStart, loEnd); }));
                    sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", hiStart + 1, hiEnd + 1), "", [hiStart,hiEnd,addRange](rack::ui::Menu* sm2){ addRange(sm2, hiStart, hiEnd); }));
                } else {
                    // Thirds
                    int base = N / 3; int rem = N % 3;
                    int size1 = base + (rem > 0 ? 1 : 0);
                    int size2 = base + (rem > 1 ? 1 : 0);
                    // third size3 is implied; remaining entries fill final range
                    int s1 = 0;           int e1 = size1 - 1;
                    int s2 = e1 + 1;      int e2 = s2 + size2 - 1;
                    int s3 = e2 + 1;      int e3 = N - 1;
                    auto addRange = [addDegree](rack::ui::Menu* dest, int start, int end){ for (int d = start; d <= end; ++d) addDegree(dest, d); };
                    sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", s1 + 1, e1 + 1), "", [s1,e1,addRange](rack::ui::Menu* sm2){ addRange(sm2, s1, e1); }));
                    sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", s2 + 1, e2 + 1), "", [s2,e2,addRange](rack::ui::Menu* sm2){ addRange(sm2, s2, e2); }));
                    sm->addChild(rack::createSubmenuItem(rack::string::f("%d..%d", s3 + 1, e3 + 1), "", [s3,e3,addRange](rack::ui::Menu* sm2){ addRange(sm2, s3, e3); }));
                }
            }
        }));
        // TET presets (non-octave): Carlos family
        menu->addChild(rack::createSubmenuItem("TET presets (non-octave)", "", [m](rack::ui::Menu* sm){
            auto add = [&](const hi::music::tets::Tet& t){
                float cents = 1200.f * t.periodOct / (float)t.steps;
                std::string label = rack::string::f("%s — %d steps / period, %.1f cents/step", t.name, t.steps, cents);
                sm->addChild(rack::createCheckMenuItem(label, "", [m,&t]{ return m->tuningMode==1 && m->tetSteps==t.steps && std::fabs(m->tetPeriodOct - t.periodOct) < 1e-6f; }, [m,&t]{ m->tuningMode=1; m->tetSteps=t.steps; m->tetPeriodOct=t.periodOct; m->rootNote%=std::max(1,m->tetSteps); }));
            };
            sm->addChild(rack::createMenuLabel("Carlos"));
            for (const auto& t : hi::music::tets::carlos()) add(t);
        }));
    // Quantize strength
        menu->addChild(rack::createSubmenuItem("Quantize strength", "", [m](rack::ui::Menu* sm){
            const char* labels[] = {"0%","25%","50%","75%","100%"};
            const float vals[] = {0.f, 0.25f, 0.5f, 0.75f, 1.f};
            for (int i = 0; i < 5; ++i) {
                sm->addChild(rack::createCheckMenuItem(labels[i], "", [m,i,vals]{ return std::fabs(m->quantStrength - vals[i]) < 1e-4f; }, [m,i,vals]{ m->quantStrength = vals[i]; }));
            }
        }));
        // Quantize rounding mode
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
                sm->addChild(rack::createCheckMenuItem(label, desc, [m,mode]{ return m->quantRoundMode == mode; }, [m,mode]{ m->quantRoundMode = mode; }));
            }
        }));
        // Stickiness (cents) hysteresis
        menu->addChild(rack::createSubmenuItem("Stickiness (¢)", "", [m](rack::ui::Menu* sm){
            const float presets[] = {0.f, 2.f, 5.f, 7.f, 10.f, 15.f, 20.f};
            for (float v : presets) {
                sm->addChild(rack::createCheckMenuItem(rack::string::f("%.0f", v), "", [m,v]{ return std::fabs(m->stickinessCents - v) < 1e-3f; }, [m,v]{ m->stickinessCents = v; }));
            }
            // Info label showing current value; presets above handle selection
            sm->addChild(rack::createMenuLabel(rack::string::f("Current: %.2f¢", m->stickinessCents)));
        }));
    // Glide normalization master enable + mode selection
        menu->addChild(rack::createCheckMenuItem("Glide normalization (enable)", "When off: equal-time glide (all jumps same duration)", [m]{ return m->glideNormEnabled; }, [m]{ m->glideNormEnabled = !m->glideNormEnabled; }));
        menu->addChild(rack::createSubmenuItem("Glide normalization mode", "Ignored while disabled", [m](rack::ui::Menu* sm){
            sm->addChild(rack::createCheckMenuItem("Volts-linear", "Seconds per volt (distance-proportional)", [m]{ return m->glideNorm == (int)PolyQuanta::GlideNorm::VoltsLinear; }, [m]{ m->glideNorm = (int)PolyQuanta::GlideNorm::VoltsLinear; }));
            sm->addChild(rack::createCheckMenuItem("Cent-linear (1 V/oct)", "Seconds per semitone", [m]{ return m->glideNorm == (int)PolyQuanta::GlideNorm::CentLinear; }, [m]{ m->glideNorm = (int)PolyQuanta::GlideNorm::CentLinear; }));
            sm->addChild(rack::createCheckMenuItem("Step-safe (EDO/TET period)", "Seconds per scale step", [m]{ return m->glideNorm == (int)PolyQuanta::GlideNorm::StepSafe; }, [m]{ m->glideNorm = (int)PolyQuanta::GlideNorm::StepSafe; }));
        }));
        hi::ui::menu::addBoolPtr(menu, "Sync glides across channels", &m->syncGlides);
        // Strum submenu
        menu->addChild(rack::createSubmenuItem("Strum", "", [m](rack::ui::Menu* sm){
            // Enabled toggle (default OFF). When enabling, auto-set spread to 100 ms if unset.
            sm->addChild(rack::createCheckMenuItem("Enabled (default off)", "", [m]{ return m->strumEnabled; }, [m]{
                m->strumEnabled = !m->strumEnabled;
                if (m->strumEnabled) {
                    if (m->strumMs <= 0.f) m->strumMs = 100.f; // sensible default so enabling has an audible effect
                } else {
                    m->strumMs = 0.f;
                }
            }));
            sm->addChild(new MenuSeparator);
            // Behavior
            sm->addChild(rack::createSubmenuItem("Behavior", "", [m](rack::ui::Menu* sm2){
                sm2->addChild(rack::createCheckMenuItem("Time-stretch", "", [m]{ return m->strumType == 0; }, [m]{ m->strumType = 0; }));
                sm2->addChild(rack::createCheckMenuItem("Start-delay",  "", [m]{ return m->strumType == 1; }, [m]{ m->strumType = 1; }));
            }));
            // Directions
            sm->addChild(rack::createSubmenuItem("Direction", "", [m](rack::ui::Menu* sm2){
                sm2->addChild(rack::createCheckMenuItem("Up", "",   [m]{ return m->strumMode==0; }, [m]{ m->strumMode=0; }));
                sm2->addChild(rack::createCheckMenuItem("Down", "", [m]{ return m->strumMode==1; }, [m]{ m->strumMode=1; }));
                sm2->addChild(rack::createCheckMenuItem("Random", "", [m]{ return m->strumMode==2; }, [m]{ m->strumMode=2; }));
            }));
            // Spread presets
            sm->addChild(rack::createSubmenuItem("Spread (ms)", "", [m](rack::ui::Menu* sm2){
                const float vals[] = {5.f, 10.f, 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f};
                for (int i=0;i<8;++i) {
                    float v = vals[i];
                    sm2->addChild(rack::createCheckMenuItem(rack::string::f("%.0f", v), "", [m,v]{ return m->strumEnabled && std::fabs(m->strumMs - v) < 1e-3f; }, [m,v]{ m->strumEnabled=true; m->strumMs=v; }));
                }
            }));
        }));

    // Batch: Quantize to scale enable (all/even/odd)
    menu->addChild(rack::createSubmenuItem("Batch: Quantize to scale", "", [m](rack::ui::Menu* sm){
            sm->addChild(rack::createMenuItem("All ON", "", [m]{ for (int i=0;i<16;++i) m->qzEnabled[i]=true; }));
            sm->addChild(rack::createMenuItem("Even ON", "", [m]{ for (int i=0;i<16;++i) if ((i%2)==1) m->qzEnabled[i]=true; }));
            sm->addChild(rack::createMenuItem("Odd ON", "", [m]{ for (int i=0;i<16;++i) if ((i%2)==0) m->qzEnabled[i]=true; }));
            sm->addChild(new MenuSeparator);
            sm->addChild(rack::createMenuItem("All OFF", "", [m]{ for (int i=0;i<16;++i) m->qzEnabled[i]=false; }));
            sm->addChild(rack::createMenuItem("Even OFF", "", [m]{ for (int i=0;i<16;++i) if ((i%2)==1) m->qzEnabled[i]=false; }));
            sm->addChild(rack::createMenuItem("Odd OFF", "", [m]{ for (int i=0;i<16;++i) if ((i%2)==0) m->qzEnabled[i]=false; }));
        }));
    // Reset all octave shifts (after Batch submenu)
    menu->addChild(rack::createMenuItem("Reset all oct shifts", "", [m]{ for (int i=0;i<16;++i) m->postOctShift[i] = 0; }));

    // Per-channel octave shift moved to offset knob context menus
    }
};


Model* modelPolyQuanta = createModel<PolyQuanta, PolyQuantaWidget>("PolyQuanta");

/*
-----------------------------------------------------------------------
 Section: Quantity utilities and scale tables
 - OffsetQuantity methods implement context‑aware display/parse for volts
     vs semitones. Static tables define preset scales for 12‑ and 24‑EDO.
-----------------------------------------------------------------------
*/
// Preset scales are defined in the inlined helpers at the top of this file.