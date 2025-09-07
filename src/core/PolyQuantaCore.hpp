#pragma once
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>

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
}} // namespace hi::dsp
