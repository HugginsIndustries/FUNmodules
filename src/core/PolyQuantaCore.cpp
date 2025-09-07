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
    // Generic EDO branch (Phase 3 bug fix): generic masks are BYTE-PER-STEP, matching hi::dsp::isAllowedStep.
    // 0 byte = disallowed, non-zero = allowed. Previous bit-packed assumption collapsed many degrees.
    if (qc.customMaskGeneric && qc.customMaskLen == edo) return qc.customMaskGeneric[s] != 0; // direct lookup
    if (qc.customMaskGeneric && qc.customMaskLen != edo) { /* length mismatch: allow all (safe fallback) */ return true; }
    return true; // no generic mask provided => allow all (original fallback behavior)
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
#include <cassert>

// Phase 3B: Rounding + hysteresis helpers (verbatim relocation of formulas)
namespace hi { namespace dsp {
HystThresholds computeHysteresis(float centerVolts, const HystSpec& h) noexcept {
    HystThresholds t; // T_up = center + (ΔV/2) + H_V; T_down = center - (ΔV/2) - H_V
    t.up = centerVolts + 0.5f * h.deltaV + h.H_V;
    t.down = centerVolts - 0.5f * h.deltaV - h.H_V;
    return t;
}
int pickRoundingTarget(int baseStep, float posWithinStep, int slopeDir, RoundPolicy pol) noexcept {
    // posWithinStep: raw fractional position relative to baseStep center. Negative => below center.
    switch (pol.mode) {
        case RoundMode::Nearest: {
            if (posWithinStep > 0.5f) return +1; // beyond upper midpoint
            if (posWithinStep < -0.5f) return -1; // beyond lower midpoint
            return 0; // stay
        }
        case RoundMode::Floor: {
            return (posWithinStep < 0.f) ? -1 : 0; // bias downward when below center
        }
        case RoundMode::Ceil: {
            return (posWithinStep > 0.f) ? +1 : 0; // bias upward when above center
        }
        case RoundMode::Directional: {
            if (slopeDir > 0) { // rising => ceiling bias
                return (posWithinStep > 0.f) ? +1 : 0;
            } else if (slopeDir < 0) { // falling => floor bias
                return (posWithinStep < 0.f) ? -1 : 0;
            }
            // Neutral slope: act like Nearest inside midpoints
            if (posWithinStep > 0.5f) return +1;
            if (posWithinStep < -0.5f) return -1;
            return 0;
        }
    }
    return 0;
}
#ifdef UNIT_TESTS
static void _phase3b_selftest() {
    HystSpec h{1.f/12.f, 0.01f}; auto th = computeHysteresis(0.f, h); assert(th.up > 0.f && th.down < 0.f);
    int adjN = pickRoundingTarget(0, 0.6f, 0, {RoundMode::Nearest}); assert(adjN == +1);
    int adjF = pickRoundingTarget(0, -0.2f, 0, {RoundMode::Floor}); assert(adjF == -1);
    int adjC = pickRoundingTarget(0, 0.2f, 0, {RoundMode::Ceil}); assert(adjC == +1);
    int adjDirUp = pickRoundingTarget(0, 0.2f, +1, {RoundMode::Directional}); assert(adjDirUp == +1);
    int adjDirDown = pickRoundingTarget(0, -0.2f, -1, {RoundMode::Directional}); assert(adjDirDown == -1);
}
struct _Phase3BSelfTestInvoker { _Phase3BSelfTestInvoker(){ _phase3b_selftest(); } } _phase3bSelfTestInvoker;
#endif
// Additional UNIT_TESTS: verify generic EDO (13) byte-per-step mask behavior matches hi::dsp::isAllowedStep.
#ifdef UNIT_TESTS
static void _genericMaskTest() {
    hi::dsp::QuantConfig qc; qc.edo = 13; qc.periodOct = 1.f; qc.root = 0; qc.useCustom = true; qc.customFollowsRoot = true;
    static uint8_t mask13[13] = {};
    // Example 7-note MOS-like selection: {0,3,4,7,8,11,12}
    int allowedIdx[] = {0,3,4,7,8,11,12};
    for (int i : allowedIdx) if (i>=0 && i<13) mask13[i] = 1;
    qc.customMaskGeneric = mask13; qc.customMaskLen = 13;
    bool anyNonRoot = false;
    for (int s = 0; s < 13; ++s) {
        bool lambdaAllowed = true; // replicate inner lambda logic succinctly
        int adj = s; if (qc.customFollowsRoot) adj -= qc.root; adj %= qc.edo; if (adj < 0) adj += qc.edo;
        lambdaAllowed = (qc.customMaskGeneric && qc.customMaskLen == qc.edo) ? (qc.customMaskGeneric[adj] != 0) : true;
        bool apiAllowed = hi::dsp::isAllowedStep(s, qc);
        assert(lambdaAllowed == apiAllowed);
        if (s != 0 && apiAllowed) anyNonRoot = true;
    }
    assert(anyNonRoot); // ensure more than root is allowed (regression for collapsed mask bug)
}
struct _GenericMaskSelfTestInvoker { _GenericMaskSelfTestInvoker(){ _genericMaskTest(); } } _genericMaskSelfTestInvoker;
#endif
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

// -----------------------------------------------------------------------------
// Phase 3D: Verbatim relocation of quantization/tuning JSON packing helpers.
// (compiled out in UNIT_TESTS) These functions reproduce EXACT key names and
// value semantics previously handled inside PolyQuanta.cpp. No new keys, no
// renames, no structural changes. Order of insertion matches original sequence
// to preserve human patch diffs as much as feasible. Behavior: byte‑for‑byte
// identical JSON for quantization-related fields.
// -----------------------------------------------------------------------------
#if !defined(UNIT_TESTS)
namespace hi { namespace dsp {
void coreToJson(json_t* root, const CoreState& s) noexcept {
    // Quantization meta
    json_object_set_new(root, "quantStrength", json_real(s.quantStrength));
    json_object_set_new(root, "quantRoundMode", json_integer(s.quantRoundMode));
    json_object_set_new(root, "stickinessCents", json_real(s.stickinessCents));
    // Tuning system
    json_object_set_new(root, "edo", json_integer(s.edo));
    json_object_set_new(root, "tuningMode", json_integer(s.tuningMode));
    json_object_set_new(root, "tetSteps", json_integer(s.tetSteps));
    json_object_set_new(root, "tetPeriodOct", json_real(s.tetPeriodOct));
    // Custom scale flags
    json_object_set_new(root, "useCustomScale", s.useCustomScale ? json_true() : json_false());
    json_object_set_new(root, "rememberCustomScale", s.rememberCustomScale ? json_true() : json_false());
    json_object_set_new(root, "customScaleFollowsRoot", s.customScaleFollowsRoot ? json_true() : json_false());
    // Masks
    json_object_set_new(root, "customMask12", json_integer(s.customMask12));
    json_object_set_new(root, "customMask24", json_integer(s.customMask24));
    if (!s.customMaskGeneric.empty()) {
        json_t* arr = json_array();
        for (size_t i = 0; i < s.customMaskGeneric.size(); ++i)
            json_array_append_new(arr, json_integer((int)s.customMaskGeneric[i]));
        json_object_set_new(root, "customMaskGenericN", json_integer((int)s.customMaskGeneric.size()));
        json_object_set_new(root, "customMaskGeneric", arr);
    }
    // Per-channel quantize enables + octave shifts
    for (int i = 0; i < 16; ++i) {
        char key[32];
        std::snprintf(key, sizeof(key), "qzEnabled%d", i+1);
        json_object_set_new(root, key, s.qzEnabled[i] ? json_true() : json_false());
        std::snprintf(key, sizeof(key), "postOctShift%d", i+1);
        json_object_set_new(root, key, json_integer(s.postOctShift[i]));
    }
    // Scale/root
    json_object_set_new(root, "rootNote", json_integer(s.rootNote));
    json_object_set_new(root, "scaleIndex", json_integer(s.scaleIndex));
}

void coreFromJson(const json_t* root, CoreState& s) noexcept {
    auto getInt=[&](const char* k, int& dst){ if (auto* j = json_object_get(root, k)) { if (json_is_integer(j)) dst = (int)json_integer_value(j); } };
    auto getFloat=[&](const char* k, float& dst){ if (auto* j = json_object_get(root, k)) { if (json_is_number(j)) dst = (float)json_number_value(j); } };
    auto getBool=[&](const char* k, bool& dst){ if (auto* j = json_object_get(root, k)) { dst = json_is_true(j); } };
    // Quantization meta
    getFloat("quantStrength", s.quantStrength);
    getInt("quantRoundMode", s.quantRoundMode);
    getFloat("stickinessCents", s.stickinessCents);
    // Tuning system
    getInt("edo", s.edo); getInt("tuningMode", s.tuningMode); getInt("tetSteps", s.tetSteps); getFloat("tetPeriodOct", s.tetPeriodOct);
    // Custom scale flags
    getBool("useCustomScale", s.useCustomScale);
    getBool("rememberCustomScale", s.rememberCustomScale);
    getBool("customScaleFollowsRoot", s.customScaleFollowsRoot);
    // Masks
    if (auto* j = json_object_get(root, "customMask12")) if (json_is_integer(j)) s.customMask12 = (uint32_t)json_integer_value(j);
    if (auto* j = json_object_get(root, "customMask24")) if (json_is_integer(j)) s.customMask24 = (uint32_t)json_integer_value(j);
    s.customMaskGeneric.clear();
    if (auto* arr = json_object_get(root, "customMaskGeneric")) {
        if (json_is_array(arr)) {
            size_t len = json_array_size(arr);
            s.customMaskGeneric.resize(len);
            for (size_t i = 0; i < len; ++i) {
                json_t* v = json_array_get(arr, i);
                s.customMaskGeneric[i] = (uint8_t)((int)json_integer_value(v) ? 1 : 0);
            }
        }
    }
    // Per-channel quantize enable + octave shift
    for (int i = 0; i < 16; ++i) {
        char key[32];
        std::snprintf(key, sizeof(key), "qzEnabled%d", i+1);
        if (auto* j = json_object_get(root, key)) s.qzEnabled[i] = json_is_true(j);
        std::snprintf(key, sizeof(key), "postOctShift%d", i+1);
        if (auto* j = json_object_get(root, key)) if (json_is_integer(j)) s.postOctShift[i] = (int)json_integer_value(j);
    }
    // Scale/root
    getInt("rootNote", s.rootNote); getInt("scaleIndex", s.scaleIndex);
}
}} // namespace hi::dsp
#endif // !UNIT_TESTS

// -----------------------------------------------------------------------------
// Phase 3C: Headless core unit tests (compiled ONLY when UNIT_TESTS is defined)
// IMPORTANT: This section is PURELY ADDITIVE and does NOT alter any shipping
// functionality. It supplies a tiny deterministic assertion suite invoked by
// a standalone console runner (tests/main.cpp) outside the Rack build.
// -----------------------------------------------------------------------------
#ifdef UNIT_TESTS
#include <vector>
#include <iostream> // (Only used in optional diagnostic branches; no output on success.)
namespace pqtests {
    // Runs a few deterministic assertions covering boundary mapping, directional
    // tie‑break logic, hysteresis threshold math, and the generic 13‑EDO mask parity
    // bug fixed in the previous phase. Returns 0 on success (assert failures abort).
    int run_core_tests();
}

// Helper to compare floats with a tight tolerance (no dependency on Rack).
static inline void _assertClose(float a, float b, float eps, const char* ctx) {
    assert(std::fabs(a - b) <= eps && "_assertClose failed"); (void)ctx; (void)eps;
}

int pqtests::run_core_tests() {
    using namespace hi::dsp;
    // --- 12-EDO boundary mapping ---
    // Build QuantConfig for ordinary 12-EDO (period 1 V/oct). Verify snapped
    // outputs over canonical grid are monotonic and produce the expected 13
    // unique boundary voltages (0, 1/12, ..., 1). We explicitly test the half‑way
    // points (n + 0.5)/12 map upward (std::round tie away from zero) preserving
    // existing shipping behavior.
    {
        QuantConfig qc; qc.edo = 12; qc.periodOct = 1.f; qc.root = 0; qc.useCustom = false;
        const float step = 1.f / 12.f;
        std::vector<float> uniques;
        for (int k = 0; k <= 12; ++k) {
            float v = k * step; // exact lattice point
            float snapped = snapEDO(v, qc);
            _assertClose(snapped, v, 1e-6f, "exact lattice mapping");
            if (uniques.empty() || std::fabs(snapped - uniques.back()) > 1e-6f)
                uniques.push_back(snapped);
            if (k < 12) {
                // Midpoint just above lower step should round up because std::round() halves go up for positive values.
                float mid = (k + 0.5f) * step;
                float snappedMid = snapEDO(mid, qc);
                _assertClose(snappedMid, (k + 1) * step, 1e-6f, "midpoint upward rounding");
            }
        }
        assert(uniques.size() == 13 && "Expected 13 unique snapped voltages in [0,1] inclusive for 12-EDO");
        // Monotonic check
        for (size_t i = 1; i < uniques.size(); ++i) {
            assert(uniques[i] > uniques[i-1] - 1e-9f && "Snapped sequence not monotonic");
        }
    }

    // --- Directional tie-break ---
    // pickRoundingTarget() biases the decision based on slope direction in Directional mode.
    // We synthesize a fractional position exactly at +0.2 and −0.2 around the center and verify
    // slope sign chooses the expected adjustment (+1 when rising above center, -1 when falling below).
    {
        RoundPolicy pol{RoundMode::Directional};
        int up = pickRoundingTarget(0, +0.2f, +1, pol);  // rising, above center ⇒ +1
        int stayUp = pickRoundingTarget(0, +0.2f, 0, pol); // neutral slope acts like nearest inside midpoints ⇒ 0
        int down = pickRoundingTarget(0, -0.2f, -1, pol); // falling, below center ⇒ -1
        int stayDown = pickRoundingTarget(0, -0.2f, 0, pol); // neutral slope ⇒ 0
        assert(up == +1 && down == -1 && stayUp == 0 && stayDown == 0 && "Directional tie-break mismatch");
    }

    // --- Hysteresis thresholds ---
    // computeHysteresis(center, {ΔV, H_V}) → {center + ΔV/2 + H_V, center - ΔV/2 - H_V}.
    {
        float center = 0.0f; float deltaV = 1.f/12.f; float H_V = 0.01f;
        HystSpec hs{deltaV, H_V};
        auto thr = computeHysteresis(center, hs);
        _assertClose(thr.up,   center + 0.5f*deltaV + H_V, 1e-9f, "hyst up");
        _assertClose(thr.down, center - 0.5f*deltaV - H_V, 1e-9f, "hyst down");
        assert(thr.up > thr.down && "Threshold ordering invalid");
    }

    // --- 13-EDO generic mask parity (recent fix) ---
    // Construct a byte-per-step mask allowing indices {0,3,4,7,8,11,12}. Ensure parity between
    // isAllowedStep() and direct mask lookup AND that snapEDO only lands on allowed steps for a
    // selection of probe voltages spanning the period.
    {
        QuantConfig qc; qc.edo = 13; qc.periodOct = 1.f; qc.root = 0; qc.useCustom = true; qc.customFollowsRoot = true;
        static uint8_t mask13[13] = {0};
        int allowedIdx[] = {0,3,4,7,8,11,12};
        for (int i : allowedIdx) mask13[i] = 1;
        qc.customMaskGeneric = mask13; qc.customMaskLen = 13;
        // Parity across all steps
        for (int s = 0; s < 13; ++s) {
            bool api = hi::dsp::isAllowedStep(s, qc);
            bool mask = (mask13[s] != 0);
            assert(api == mask && "Generic mask parity mismatch (byte-per-step)");
        }
        // Probe a range of voltages (including exact steps + midpoints) and assert snapped step is allowed.
        const float step = 1.f / 13.f;
        for (int k = 0; k <= 13; ++k) {
            float probes[2]; probes[0] = k * step; probes[1] = (k < 13) ? (k + 0.49f) * step : k * step; // stay below tie boundary
            for (float v : probes) {
                float snapped = snapEDO(v, qc);
                // Convert snapped volts back to (possibly >13) step domain then wrap.
                float stepsF = snapped * qc.edo; // since periodOct = 1
                int sIdx = (int)std::round(stepsF);
                sIdx %= qc.edo; if (sIdx < 0) sIdx += qc.edo;
                assert(mask13[sIdx] && "snapEDO produced disallowed step under generic mask");
            }
        }
    }

    // (CoreState JSON round-trip test removed in UNIT_TESTS build: JSON helpers compiled out.)

    return 0; // All assertions passed.
}
#endif // UNIT_TESTS
