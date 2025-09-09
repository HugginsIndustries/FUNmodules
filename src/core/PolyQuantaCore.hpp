#pragma once
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>
// (Relocated helpers add'l includes)
#include <map>
#include <unordered_set>
#include <set>

// Phase 3D: JSON bridge (verbatim relocation of quantization field packing/unpacking)
// Provide json_t without forcing Rack dependency when building headless tests.
// Headless tests don’t link Jansson; JSON functions are compiled out.
#ifndef UNIT_TESTS
#  include "plugin.hpp"   // provides json_t from Rack SDK
#else
struct json_t;             // forward declaration so signatures compile in tests
#endif

// PolyQuantaCore: Phase 2A extraction of pure DSP helpers from PolyQuanta.cpp.
// IMPORTANT: All functions and constants are moved verbatim (logic, math,
// strings) to avoid any behavior change. Original comments are preserved
// where they conveyed semantics; brief one-line summaries added for clarity.
// Namespaces mirror original structure (hi::consts, hi::dsp::{clip,glide,range}).
// This lets existing call sites remain unchanged aside from including this header.

namespace hi { namespace consts {
// Global reusable constants — C++11 friendly (verbatim move)
static constexpr float MAX_VOLT_CLAMP = 10.f;   // Output clamp (±10 V typical)
static constexpr float LED_SCALE_V    = 10.f;   // LED normalization divisor
static constexpr float MIN_SEC        = 1e-4f;  // ~0.1 ms → treat as "no slew"
static constexpr float MAX_SEC        = 10.f;   // 10 s max
static constexpr float EPS_ERR        = 1e-4f;  // tiny error epsilon for early-out and guards
static constexpr float RATE_EPS       = 1e-3f;  // minimal rate change to update SlewLimiter
}} // namespace hi::consts

namespace hi { namespace dsp { namespace clip {
// Hard clamp to ±maxV (verbatim)
float hard(float v, float maxV);
// Soft clip with 1 V knee approaching ±maxV without compressing interior range.
// Cosine easing over the final 1 V knee; exact pass-through below.
float soft(float v, float maxV);
}}} // namespace hi::dsp::clip

namespace hi { namespace dsp { namespace glide {
// 1 V/oct helpers (verbatim)
float voltsToSemitones(float v);
float semitonesToVolts(float s);
// Shape mapping: pack parameters from shape in [-1,1].
struct ShapeParams { float k = 0.f; float c = 1.f; bool negative = false; };
ShapeParams makeShape(float shape, float kPos = 6.f, float kNeg = 8.f);
// u in [0,1] normalized progress → multiplier (≥ eps) shaping rise/fall curvature.
float shapeMul(float u, const ShapeParams& p, float eps = 1e-6f);
}}} // namespace hi::dsp::glide

namespace hi { namespace dsp { namespace range {
// Range mode (verbatim enum)
enum class Mode { Clip = 0, Scale = 1 };
// Map UI index to half-range (±limit) in volts.
float clipLimitFromIndex(int idx);
// Apply pre-quant range handling around 0V only; optionally soft clip in Clip mode.
float apply(float v, Mode mode, float clipLimit, bool soft);
}}} // namespace hi::dsp::range

namespace hi { namespace dsp {
// Phase 3B: Rounding + hysteresis helper types (pure calculation only)
enum class RoundMode { Nearest = 0, Floor = 1, Ceil = 2, Directional = 3 }; // Directional = slope-dependent snap
struct RoundPolicy { RoundMode mode; }; // Wrap mode for future extension
struct HystSpec { float deltaV; float H_V; }; // deltaV = step size volts, H_V = added hysteresis volts
struct HystThresholds { float up; float down; }; // Absolute thresholds (relative domain input uses center +/- ...)
// Compute hysteresis thresholds: T_up = center + (ΔV/2) + H_V; T_down = center - (ΔV/2) - H_V
HystThresholds computeHysteresis(float centerVolts, const HystSpec& h) noexcept;
// Choose target rounding step bias based on policy and slope direction.
// baseStep: integer snapped center step; posWithinStep: raw fractional offset relative to that step (-0.5..+0.5 range semantics)
// slopeDir: -1 descending, +1 ascending, 0 neutral; returns 0 adjust (center) or +/-1 step bias request.
int pickRoundingTarget(int baseStep, float posWithinStep, int slopeDir, RoundPolicy pol) noexcept;
// Quantization config and snapper supporting arbitrary period sizes (EDO/TET)
struct QuantConfig {
	int edo = 12; float periodOct = 1.f; int root = 0; bool useCustom = false; bool customFollowsRoot = true;
	uint32_t customMask12 = 0xFFFu; uint32_t customMask24 = 0xFFFFFFu; int scaleIndex = 0;
	const uint8_t* customMaskGeneric = nullptr; int customMaskLen = 0;
};
// Snap a voltage to the nearest allowed EDO/TET degree per QuantConfig.
float snapEDO(float volts, const QuantConfig& qc, float boundLimit = 10.f, bool boundToLimit = false, int shiftSteps = 0);
// Helper predicates (exposed because PolyQuanta.cpp logic references them for hysteresis/latched decisions)
bool isAllowedStep(int s, const QuantConfig& qc);
int nextAllowedStep(int start, int dir, const QuantConfig& qc);
int nearestAllowedStep(int sGuess, float fs, const QuantConfig& qc);
// FIX: Stateful tie-breaking version for boundary stability
int nearestAllowedStepWithHistory(int sGuess, float fs, const QuantConfig& qc, int prevStep);

// -----------------------------------------------------------------------------
// Phase 3D: CoreState captures ONLY quantization/tuning/scale/mask/rounding/
// hysteresis/root-alignment fields exactly as previously serialized in
// PolyQuanta.cpp. NO new fields; types unchanged to preserve JSON identity.
// -----------------------------------------------------------------------------
struct CoreState {
	// Quantization meta
	float  quantStrength = 1.f;          // "quantStrength"
	int    quantRoundMode = 0;           // "quantRoundMode"
	float  stickinessCents = 5.f;        // "stickinessCents"
	// Tuning / system
	int    edo = 12;                     // "edo"
	int    tuningMode = 0;               // "tuningMode"
	int    tetSteps = 9;                 // "tetSteps"
	float  tetPeriodOct = 0.f;           // "tetPeriodOct" (default set by module ctor; kept here for round‑trip)
	bool   useCustomScale = false;       // "useCustomScale"
	bool   rememberCustomScale = false;  // "rememberCustomScale"
	bool   customScaleFollowsRoot = true;// "customScaleFollowsRoot"
	uint32_t customMask12 = 0xFFFu;      // "customMask12"
	uint32_t customMask24 = 0xFFFFFFu;   // "customMask24"
	std::vector<uint8_t> customMaskGeneric; // "customMaskGeneric" + length key
	// Per‑channel enable + octave shift
	bool   qzEnabled[16] = {false};      // keys: qzEnabled1..qzEnabled16
	int    postOctShift[16] = {0};       // keys: postOctShift1..postOctShift16
	// Scale/root selection
	int    rootNote = 0;                 // "rootNote"
	int    scaleIndex = 0;               // "scaleIndex"
};

// Write EXACT existing keys/values (no renames, order preserved as much as possible).
void coreToJson(json_t* root, const CoreState& s) noexcept;
// Read SAME keys; apply defaults for missing ones. NO behavior change.
void coreFromJson(const json_t* root, CoreState& s) noexcept;
}} // namespace hi::dsp

// -----------------------------------------------------------------------------
// Phase 4A (relocated): MOS (Moment of Symmetry) helpers and lightweight poly
// width helper moved from top of PolyQuanta.cpp. Behavior unchanged. Public
// surface kept minimal (only symbols referenced by PolyQuanta.cpp exposed).
// -----------------------------------------------------------------------------
namespace hi { namespace music { namespace mos {
// Curated generator-count suggestions per division N (read-only map)
extern const std::map<int, std::vector<int>> curated; // relocated
int gcdInt(int a, int b);                              // relocated
std::vector<int> generateCycle(int N, int g, int m);   // relocated
bool isMOS(const std::vector<int>& pcs, int N);        // relocated
int findBestGenerator(int N, int m);                   // relocated
std::string patternLS(const std::vector<int>& pcs, int N); // relocated
}}} // namespace hi::music::mos

namespace hi { namespace dsp { namespace poly {
int processWidth(bool forcePolyOut, bool inputConnected, int inputChannels, int maxCh = 16); // relocated
}}} // namespace hi::dsp::poly

// -----------------------------------------------------------------------------
// Phase 3C: Test runner forward declaration (only visible when UNIT_TESTS).
// This keeps production builds free of the test symbol while allowing the
// standalone console runner to link against core logic.
// -----------------------------------------------------------------------------
#ifdef UNIT_TESTS
namespace pqtests { int run_core_tests(); }
#endif
