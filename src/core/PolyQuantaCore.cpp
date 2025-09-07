#include "PolyQuantaCore.hpp"
#include <cmath>
#include <limits>
#include "ScaleDefs.hpp" // centralized scale definitions (single source of truth)

namespace hi { namespace dsp {
// (Verbatim moved from PolyQuanta.cpp; formatting and logic preserved)
float snapEDO(float volts, const QuantConfig& qc, float boundLimit, bool boundToLimit, int shiftSteps) {
    // Custom masks for 12/24 or generic length2, length3 etc sequences. (Originally: allows any subset of steps)
    const int edo = qc.edo;
    const float periodSize = qc.periodOct; // Typically 1.f (octave) but can differ.

    // Helper lambda (verbatim; no behavior change). Removed unused lambda from earlier version.
    auto isAllowedStep = [&](int step) -> bool {
        if (!qc.useCustom) return true;
        int s = step;
        if (qc.customFollowsRoot) s -= qc.root; // shift mask alignment with root
        // Wrap negative indices into positive domain
        s %= edo; if (s < 0) s += edo;
        if (edo == 12) return (qc.customMask12 >> s) & 1U;
        if (edo == 24) return (qc.customMask24 >> s) & 1U;
        if (qc.customMaskGeneric && qc.customMaskLen == edo) {
            int byteIndex = s / 8; int bitIndex = s % 8;
            return (qc.customMaskGeneric[byteIndex] >> bitIndex) & 1U;
        }
        return true; // Fallback: allow all if mask inconsistent.
    };
    // (Removed previously unused nextAllowedStep lambda to silence warning; logic elsewhere unaffected.)
    auto nearestAllowedStep = [&](int baseStep) -> int {
        if (isAllowedStep(baseStep)) return baseStep;
        // Expand ring search symmetrical (1, -1, 2, -2, ...)
        for (int radius = 1; radius <= edo; ++radius) {
            if (isAllowedStep(baseStep + radius)) return baseStep + radius;
            if (isAllowedStep(baseStep - radius)) return baseStep - radius;
        }
        return baseStep; // fallback
    };

    // Convert volts to step index, accounting for root offset & shiftSteps.
    const float stepsPerVolt = (float)edo / periodSize; // number of EDO steps per volt span
    float rawSteps = volts * stepsPerVolt + (float)qc.root + (float)shiftSteps;
    int baseStep = (int)std::round(rawSteps);
    int quantStep = nearestAllowedStep(baseStep);

    // Bound quantized step within a symmetric range if requested.
    if (boundToLimit) {
        // Compute min/max step that map inside ±boundLimit volts.
        int maxStep = (int)std::floor(boundLimit * stepsPerVolt);
        int minStep = -maxStep;
        if (quantStep > maxStep) quantStep = maxStep;
        else if (quantStep < minStep) quantStep = minStep;
    }

    // Map steps back to volts, remove root & shift, accounting for period size.
    float snapped = ((float)quantStep - (float)qc.root - (float)shiftSteps) / stepsPerVolt;
    return snapped;
}
// Return whether pitch-class step s is allowed under qc (root/mask aware)
bool isAllowedStep(int s, const QuantConfig& qc) {
    int N = (qc.edo <= 0) ? 12 : qc.edo; if (N <= 0) return true; float period = (qc.periodOct > 0.f) ? qc.periodOct : 1.f; (void)period;
    int root = qc.root % N; if (root < 0) root += N;
    auto allowedPc = [&](int pc)->bool {
        if (!qc.useCustom) {
            if (N == 12) { const auto* S = hi::music::scales12(); int idx = (qc.scaleIndex >= 0 && qc.scaleIndex < hi::music::NUM_SCALES12) ? qc.scaleIndex : 0; unsigned int base = S[idx].mask; int idxBit = (pc - root) % N; if (idxBit < 0) idxBit += N; return ((base >> idxBit) & 1u) != 0u; }
            else if (N == 24) { const auto* S = hi::music::scales24(); int idx = (qc.scaleIndex >= 0 && qc.scaleIndex < hi::music::NUM_SCALES24) ? qc.scaleIndex : 0; unsigned int base = S[idx].mask; int idxBit = (pc - root) % N; if (idxBit < 0) idxBit += N; return ((base >> idxBit) & 1u) != 0u; }
            return true;
        } else {
            if (N == 12) { unsigned int base = qc.customMask12; if (qc.customFollowsRoot) { int idxBit = (pc - root) % N; if (idxBit < 0) idxBit += N; return ((base >> idxBit) & 1u) != 0u; } return ((base >> pc) & 1u) != 0u; }
            else if (N == 24) { unsigned int base = qc.customMask24; if (qc.customFollowsRoot) { int idxBit = (pc - root) % N; if (idxBit < 0) idxBit += N; return ((base >> idxBit) & 1u) != 0u; } return ((base >> pc) & 1u) != 0u; }
            else { if (!qc.customMaskGeneric || qc.customMaskLen < N) { return true; } int idxBit = qc.customFollowsRoot ? ((pc - root) % N) : pc; if (idxBit < 0) idxBit += N; uint8_t bit = qc.customMaskGeneric[idxBit]; return bit != 0; }
        }
    };
    int pc = s % N; if (pc < 0) pc += N; return allowedPc(pc);
}
int nextAllowedStep(int start, int dir, const QuantConfig& qc) {
    int N = (qc.edo <= 0) ? 12 : qc.edo; if (N <= 0) return start; if (dir == 0) return start; // invalid dir
    for (int k = 1; k <= N; ++k) {
        int s = start + dir * k;
        if (isAllowedStep(s, qc)) return s;
    }
    return start;
}
int nearestAllowedStep(int sGuess, float fs, const QuantConfig& qc) {
    int N = (qc.edo <= 0) ? 12 : qc.edo; if (N <= 0) return 0; int s0 = (int)std::round(fs); int best = s0; float bestDist = 1e9f;
    for (int d = 0; d <= N; ++d) {
        int c1 = s0 + d, c2 = s0 - d; if (isAllowedStep(c1, qc)) { float dist = std::fabs(fs - (float)c1); if (dist < bestDist) { bestDist = dist; best = c1; if (d == 0) break; } }
        if (d > 0 && isAllowedStep(c2, qc)) { float dist = std::fabs(fs - (float)c2); if (dist < bestDist) { bestDist = dist; best = c2; } }
        if (bestDist < 1e-6f) break;
    }
    return best;
}
}} // namespace hi::dsp
#include <cmath>
#include <cstdlib>
#include <algorithm>

// NOTE: Definitions moved verbatim from PolyQuanta.cpp (Phase 2A). All logic
// and comments retained; only static inline specifiers dropped to provide
// out-of-line linkage. Behavior must remain identical.

namespace hi { namespace dsp { namespace clip {
// Hard clamp to ±maxV
float hard(float v, float maxV) {
    if (v >  maxV) return  maxV;
    if (v < -maxV) return -maxV;
    return v;
}
// Soft clip with 1 V knee approaching ±maxV without compressing interior range.
// Behavior: linear pass-through until |v| exceeds (maxV - knee). Within the last
// knee volts, apply a smooth cosine easing to reach exactly ±maxV. Anything
// beyond ±maxV hard-clips. This preserves precise offsets (e.g. +10 V stays +10 V)
// while still avoiding a sharp corner at the ceiling when soft clipping is chosen.
float soft(float v, float maxV) {
    const float knee = 1.f; // knee width in volts
    float a = std::fabs(v);
    if (a <= maxV - knee) return v;          // fully linear region
    float sign = (v >= 0.f) ? 1.f : -1.f;
    if (a >= maxV) return sign * maxV;       // clamp beyond limit
    // Smooth knee: a in (maxV - knee, maxV)
    float x = (a - (maxV - knee)) / knee;    // x in (0,1)
    // Cosine ease-in (smooth, monotonic, zero first derivative at boundaries after scaling)
    const float PI = 3.14159265358979323846f;
    float shape = 0.5f * (1.f - std::cos(PI * x));
    float out = (maxV - knee) + shape * knee;
    return sign * out;
}
}}} // namespace hi::dsp::clip

namespace hi { namespace dsp { namespace glide {
// 1 V/oct helpers
float voltsToSemitones(float v) { return v * 12.f; }
float semitonesToVolts(float s) { return s / 12.f; }
// Shape helpers: map shape in [-1,1] to expo/log-ish multiplier parameters.
struct ShapeParams; // forward not needed (already in header), kept for clarity.
ShapeParams makeShape(float shape, float kPos, float kNeg) {
    ShapeParams p;
    if (std::fabs(shape) < 1e-6f) { p.k = 0.f; p.c = 1.f; p.negative = false; return p; }
    if (shape < 0.f) { p.k = kNeg * (-shape); p.c = (1.f - std::exp(-p.k)) / p.k; p.negative = true; }
    else { p.k = kPos * shape; p.c = 1.f + 0.5f * p.k; p.negative = false; }
    return p;
}
// u in [0,1] is normalized error progress. Returns multiplier ≥ EPS.
float shapeMul(float u, const ShapeParams& p, float eps) {
    if (p.k == 0.f) return 1.f;
    float m = p.negative ? std::exp(p.k * u) : 1.f / (1.f + p.k * u);
    float out = p.c * m;
    return out < eps ? eps : out;
}
}}} // namespace hi::dsp::glide

namespace hi { namespace dsp { namespace range {
// Map a UI index to a half-range (±limit) in volts.
float clipLimitFromIndex(int idx) {
    switch (idx) {
        case 0: return 10.f; case 1: return 7.5f; case 2: return 5.f;
        case 3: return 2.5f; case 4: return 1.f;  case 5: return 0.5f;
    }
    return 10.f;
}
// Apply pre-quant range handling around 0V only.
float apply(float v, Mode mode, float clipLimit, bool soft) {
    if (mode == Mode::Clip) {
        return soft ? clip::soft(v, clipLimit) : std::max(-clipLimit, std::min(v, clipLimit));
    }
    float s = clipLimit / hi::consts::MAX_VOLT_CLAMP; // Scale mode
    float vs = v * s;
    // clamp final
    if (vs >  clipLimit) vs =  clipLimit;
    else if (vs < -clipLimit) vs = -clipLimit;
    return vs;
}
}}} // namespace hi::dsp::range
