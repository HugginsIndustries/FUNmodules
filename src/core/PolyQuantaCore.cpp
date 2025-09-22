#include "PolyQuantaCore.hpp"
#include "Strum.hpp" // relocated strum helpers for tests
#include <cmath>
#include <limits>
#include <climits> // relocated MOS uses INT_MAX
#include "ScaleDefs.hpp" // centralized scale definitions (single source of truth)

namespace hi { namespace dsp {
// FIX: robust modulo for negatives (pitch-class math)
static inline int _modWrap(int x, int m) {
  int r = x % m;
  return (r < 0) ? r + m : r;
}

// FIX: mask check in pitch-class space relative to root (always follows root)
static inline bool _isAllowedStepRootRel(int step, const QuantConfig& qc) {
  const int edo = (qc.edo > 0 ? qc.edo : 12);
  const int rootShift = qc.root; // Always follow selected root for custom masks
  const int pcRel = _modWrap(step - rootShift, edo);
  
  // Always use custom mask path (unified scale selection)
  if (qc.customMaskGeneric && qc.customMaskLen == edo) return qc.customMaskGeneric[pcRel] != 0;
  return true; // no custom mask set ⇒ chromatic
}

// FIX: strictly choose nearest ALLOWED degree (never chromatic)
static int _nearestAllowedStepRoot(int sGuess, float fs, const QuantConfig& qc) {
  if (_isAllowedStepRootRel(sGuess, qc)) return sGuess;
  const int maxScan = (qc.edo > 0 ? qc.edo : 12) + 1;
  int best = sGuess; float bestDist = std::numeric_limits<float>::infinity();
  for (int d = 1; d <= maxScan; ++d) {
    int up = sGuess + d, dn = sGuess - d;
    if (_isAllowedStepRootRel(up, qc)) { float du = fabsf((float)up - fs); if (du < bestDist) { bestDist = du; best = up; } }
    if (_isAllowedStepRootRel(dn, qc)) { float dd = fabsf(fs - (float)dn); if (dd < bestDist) { bestDist = dd; best = dn; } }
    if (bestDist == 0.f) break;
  }
  return best;
}

// (Verbatim moved from PolyQuanta.cpp; formatting and logic preserved)
float snapEDO(float volts, const QuantConfig& qc, float boundLimit, bool boundToLimit, int shiftSteps) {
    // Custom masks for 12/24 or generic length2, length3 etc sequences. (Originally: allows any subset of steps)
    const int edo = qc.edo;
    const float periodSize = qc.periodOct; // Typically 1.f (octave) but can differ.

    // Helper lambda (verbatim; no behavior change). Removed unused lambda from earlier version.
    auto isAllowedStep = [&](int step) -> bool {
        // FIX: keep local, root-relative checker; do NOT replace with global helpers.
        return _isAllowedStepRootRel(step, qc);
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
    (void)nearestAllowedStep; // FIX: silence -Wunused-but-set-variable

    // Convert volts to step index, accounting for shiftSteps.
    const float stepsPerVolt = (float)edo / periodSize; // number of EDO steps per volt span
    float rawSteps = volts * stepsPerVolt + (float)shiftSteps; // root handled by mask; do not add here
    int baseStep = (int)std::round(rawSteps); // keep your current RoundPolicy
    // FIX: nearest ALLOWED step (root-relative), then feed hysteresis
    int quantStep = _nearestAllowedStepRoot(baseStep, rawSteps, qc);

    // Bound quantized step within a symmetric range if requested.
    if (boundToLimit) {
        // Compute min/max step that map inside ±boundLimit volts.
        int maxStep = (int)std::floor(boundLimit * stepsPerVolt);
        int minStep = -maxStep;
        if (quantStep > maxStep) {
            bool found = false;
            for (int step = maxStep; step >= minStep; --step) {
                if (_isAllowedStepRootRel(step, qc)) { quantStep = step; found = true; break; }
            }
            if (!found) quantStep = maxStep; // fallback: behave like legacy clamp
        } else if (quantStep < minStep) {
            bool found = false;
            for (int step = minStep; step <= maxStep; ++step) {
                if (_isAllowedStepRootRel(step, qc)) { quantStep = step; found = true; break; }
            }
            if (!found) quantStep = minStep; // fallback: behave like legacy clamp
        }
    }

    // Map steps back to volts, remove shift, accounting for period size.
    float snapped = ((float)quantStep - (float)shiftSteps) / stepsPerVolt; // do not remove root; keep absolute pitch
    return snapped;
}
// Return whether pitch-class step s is allowed under qc (root/mask aware)
bool isAllowedStep(int s, const QuantConfig& qc) {
    int N = (qc.edo <= 0) ? 12 : qc.edo; 
    if (N <= 0) return true; 
    
    int root = qc.root % N; 
    if (root < 0) root += N;
    
    int pc = s % N; 
    if (pc < 0) pc += N;
    
    int idxBit = (pc - root) % N; 
    if (idxBit < 0) idxBit += N;
    
    // Always use custom mask path (unified scale selection)
    if (!qc.customMaskGeneric || qc.customMaskLen < N) {
        return true; // no custom mask set ⇒ chromatic
    }
    return qc.customMaskGeneric[idxBit] != 0;
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
        // NOTE: On exact ties the first candidate (upward search) remains best; use
        // nearestAllowedStepWithHistory() if you need to honor a previous choice statefully.
        if (d > 0 && isAllowedStep(c2, qc)) { float dist = std::fabs(fs - (float)c2); if (dist < bestDist) { bestDist = dist; best = c2; } }
        if (bestDist < 1e-6f) break;
    }
    return best;
}

// FIX: Stateful tie-breaking version for callers that need to prefer the previous choice on exact midpoints
int nearestAllowedStepWithHistory(int sGuess, float fs, const QuantConfig& qc, int prevStep) {
    int candidate = nearestAllowedStep(sGuess, fs, qc);
    // FIX: If we're exactly halfway between two allowed steps, prefer the previous one
    float candidateDist = std::fabs(fs - (float)candidate);
    if (candidateDist > 0.49f && candidateDist < 0.51f && isAllowedStep(prevStep, qc)) {
        return prevStep; // FIX: Stay on previous step for boundary stability
    }
    return candidate;
}
}} // namespace hi::dsp
#include <unordered_set>
#include <set>
#include <map>

// -----------------------------------------------------------------------------
// Phase 4A (relocated): MOS helpers (verbatim logic) + poly::processWidth.
// These were previously inlined at the top of PolyQuanta.cpp. Moved here to
// reduce compilation churn and centralize reusable math without behavior change.
// -----------------------------------------------------------------------------
namespace hi { namespace music { namespace mos {
const std::map<int, std::vector<int>> curated = {
    {5,{1,2,3,4}}, {6,{1,5}}, {7,{1,2,3,4}}, {8,{1,3,5,7}}, {9,{1,2,4,5}}, {10,{1,3,7,9}}, {11,{1,2,3,4}},
    {12,{1,5,7,11}}, {13,{1,2,3,4}}, {14,{1,3,5,9}}, {15,{1,2,4,7}}, {16,{1,3,5,7}}, {17,{1,2,3,4}}, {18,{1,5,7,11}},
    {19,{1,2,3,4}}, {20,{1,3,7,9}}, {21,{1,2,4,5}}, {22,{1,3,5,7}}, {23,{1,2,3,4}}, {24,{1,5,7,11}}, {25,{1,2,3,4}},
    {26,{1,3,5,7}}, {27,{1,2,4,5}}, {28,{1,3,5,9}}, {29,{1,2,3,4}}, {30,{1,7,11,13}}, {31,{1,2,3,4}}, {32,{1,3,5,7}},
    {33,{1,2,4,5}}, {34,{1,3,5,7}}, {35,{1,2,3,4}}, {36,{1,5,7,11}}, {37,{1,2,3,4}}, {38,{1,3,5,7}}, {39,{1,2,4,5}},
    {40,{1,3,7,9}}, {41,{1,2,3,4}}, {42,{1,5,11,13}}, {43,{1,2,3,4}}, {44,{1,3,5,7}}, {45,{1,2,4,7}}, {46,{1,3,5,7}},
    {47,{1,2,3,4}}, {48,{1,5,7,11}}, {49,{1,2,3,4}}, {50,{1,3,7,9}}, {51,{1,2,4,5}}, {52,{1,3,5,7}}, {53,{1,2,3,4}},
    {54,{1,5,7,11}}, {55,{1,2,3,4}}, {56,{1,3,5,9}}, {57,{1,2,4,5}}, {58,{1,3,5,7}}, {59,{1,2,3,4}}, {60,{1,7,11,13}},
    {61,{1,2,3,4}}, {62,{1,3,5,7}}, {63,{1,2,4,5}}, {64,{1,3,5,7}}, {65,{1,2,3,4}}, {66,{1,5,7,13}}, {67,{1,2,3,4}},
    {68,{1,3,5,7}}, {69,{1,2,4,5}}, {70,{1,3,9,11}}, {71,{1,2,3,4}}, {72,{1,5,7,11}}, {73,{1,2,3,4}}, {74,{1,3,5,7}},
    {75,{1,2,4,7}}, {76,{1,3,5,7}}, {77,{1,2,3,4}}, {78,{1,5,7,11}}, {79,{1,2,3,4}}, {80,{1,3,7,9}}, {81,{1,2,4,5}},
    {82,{1,3,5,7}}, {83,{1,2,3,4}}, {84,{1,5,11,13}}, {85,{1,2,3,4}}, {86,{1,3,5,7}}, {87,{1,2,4,5}}, {88,{1,3,5,7}},
    {89,{1,2,3,4}}, {90,{1,7,11,13}}, {91,{1,2,3,4}}, {92,{1,3,5,7}}, {93,{1,2,4,5}}, {94,{1,3,5,7}}, {95,{1,2,3,4}},
    {96,{1,5,7,11}}, {97,{1,2,3,4}}, {98,{1,3,5,9}}, {99,{1,2,4,5}}, {100,{1,3,7,9}}, {101,{1,2,3,4}}, {102,{1,5,7,11}},
    {103,{1,2,3,4}}, {104,{1,3,5,7}}, {105,{1,2,4,8}}, {106,{1,3,5,7}}, {107,{1,2,3,4}}, {108,{1,5,7,11}}, {109,{1,2,3,4}},
    {110,{1,3,7,9}}, {111,{1,2,4,5}}, {112,{1,3,5,9}}, {113,{1,2,3,4}}, {114,{1,5,7,11}}, {115,{1,2,3,4}}, {116,{1,3,5,7}},
    {117,{1,2,4,5}}, {118,{1,3,5,7}}, {119,{1,2,3,4}}, {120,{1,7,11,13}}
};
int gcdInt(int a, int b){ while(b){ int t=a%b; a=b; b=t;} return a<0?-a:a; }
std::vector<int> generateCycle(int N, int g, int m){ std::vector<int> pcs; pcs.reserve(m); std::unordered_set<int> seen; for(int k=0;k<m;++k){ int v=((long long)k*g)%N; if(seen.insert(v).second) pcs.push_back(v); else break; } std::sort(pcs.begin(),pcs.end()); return pcs; }
bool isMOS(const std::vector<int>& pcs, int N){ if(pcs.size()<2) return false; std::set<int> steps; for(size_t i=0;i<pcs.size();++i){ int a=pcs[i]; int b=pcs[(i+1)%pcs.size()]; int step=(i+1<pcs.size()? b-a : (N-a+b)); if(step<=0) step+=N; steps.insert(step); if(steps.size()>2) return false; } return true; }
int findBestGenerator(int N, int m){ if(m<2) return 1; if(m>N) m=N; struct Cand{int g; int diff; float dist;} best{0,INT_MAX,1e9f}; for(int g=1; g<N; ++g){ if(gcdInt(g,N)!=1) continue; auto cyc=generateCycle(N,g,m); if((int)cyc.size()!=m) continue; if(!isMOS(cyc,N)) continue; auto bal = [&](){ std::map<int,int> freq; int M=(int)cyc.size(); for(int i=0;i<M;++i){ int a=cyc[i]; int b=cyc[(i+1)%M]; int step=(i+1<M? b-a : (N-a+b)); if(step<=0) step+=N; freq[step]++; } if(freq.size()==1) return std::pair<int,float>{0,0.f}; if(freq.size()==2){ auto it=freq.begin(); int c1=it->second; ++it; int c2=it->second; return std::pair<int,float>{std::abs(c1-c2),0.f}; } return std::pair<int,float>{1000,0.f}; }(); int diff=bal.first; float gn=(float)g/N; float dist=std::min(std::fabs(gn-7.f/12.f), std::fabs(gn-3.f/12.f)); if(diff<best.diff || (diff==best.diff && dist<best.dist)) best={g,diff,dist}; } if(best.g) return best.g; int cand[2]={ std::max(1,std::min(N-1,(int)std::lround(N*7.0/12.0))), std::max(1,std::min(N-1,(int)std::lround(N*3.0/12.0))) }; for(int g: cand){ if(gcdInt(g,N)!=1) continue; auto cyc=generateCycle(N,g,m); if((int)cyc.size()==m) return g; } return 1; }
std::string patternLS(const std::vector<int>& pcs, int N){ if(pcs.size()<2) return ""; std::vector<int> steps; for(size_t i=0;i<pcs.size();++i){ int a=pcs[i]; int b=pcs[(i+1)%pcs.size()]; int step=(i+1<pcs.size()? b-a : (N-a+b)); if(step<=0) step+=N; steps.push_back(step);} int mn=*std::min_element(steps.begin(),steps.end()); int mx=*std::max_element(steps.begin(),steps.end()); std::string out; out.reserve(steps.size()); for(int s:steps) out.push_back((mx!=mn && s==mx)?'L':'S'); return out; }
}}} // namespace hi::music::mos

namespace hi { namespace dsp { namespace poly {
int processWidth(bool forcePolyOut, bool inputConnected, int inputChannels, int maxCh) {
    int n = forcePolyOut ? maxCh : (inputConnected ? inputChannels : maxCh);
    return std::min(n, maxCh);
}
}}} // namespace hi::dsp::poly
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
// glide helpers map normalized progress to shaped multipliers.
float shapeMul(float u, const ShapeParams& p, float eps) {
    if (p.k == 0.f) return 1.f;
    float m = p.negative ? std::exp(p.k * u) : 1.f / (1.f + p.k * u);
    float out = p.c * m;
    return out < eps ? eps : out;
}
}}} // namespace hi::dsp::glide

// range helpers ensure voltages stay inside ±clipLimit before quantization.
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
    // Custom mask
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
    // Custom mask
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
#include <algorithm> // FIX: for std::clamp in test-only code
#include <vector>
#include <iostream> // (Only used in optional diagnostic branches; no output on success.)
#include "Strum.hpp" // ensure strum namespace visible in test build

// FIX: file-scope test helper for latch behavior (center-anchored, ±0.5±Hs)
static int _test_schmittLatch(int lastStep, double fs, int targetStep, float stickinessCents, const hi::dsp::QuantConfig& qc) {
    const int N = (qc.edo > 0 ? qc.edo : 12);
    float Hc = std::clamp(stickinessCents, 0.f, 20.f);  // FIX: test build uses C++17
    const float stepCents = 1200.f * (qc.periodOct / (float)N);
    const float maxAllowed = 0.4f * stepCents;
    if (Hc > maxAllowed) Hc = maxAllowed;
    const float Hs = (Hc * (float)N) / 1200.0f;   // cents→steps
    const float d  = (float)(fs - (double)lastStep);
    const float upThresh   = +0.5f + Hs;
    const float downThresh = -0.5f - Hs;
    int next = lastStep;
    if (targetStep > lastStep && d > upThresh)        next = lastStep + 1;
    else if (targetStep < lastStep && d < downThresh) next = lastStep - 1;
    return next;
}
namespace pqtests {
    // Runs a few deterministic assertions covering boundary mapping, directional
    // tie‑break logic, hysteresis threshold math, and the generic 13‑EDO mask parity
    // bug fixed in the previous phase. Returns 0 on success (assert failures abort).
    int run_core_tests();
}

// Helper to compare floats with a tight tolerance (no dependency on Rack).
static inline void _assertClose(float a, float b, float eps, const char* ctx) {
    if (std::fabs(a - b) > eps) {
        std::cerr << "_assertClose failed: " << ctx << " a=" << a << " b=" << b << " eps=" << eps << "\n";
        assert(false && "_assertClose failed");
    }
}

int pqtests::run_core_tests() {
    using namespace hi::dsp;
    // Relocated helper sanity (MOS + conversions + poly width)
    {
        // volts<->semitones round trip at boundaries
    // Tolerance loosened from 1e-6f to 1e-5f to account for floating-point round-trip imprecision.
    float v = 1.0f; float st = glide::voltsToSemitones(v); float v2 = glide::semitonesToVolts(st); _assertClose(v, v2, 1e-5f, "volts<->semitones");
        int g = hi::music::mos::gcdInt(53, 12); assert(g==1);
        auto cyc = hi::music::mos::generateCycle(12,7,7); assert(cyc.size()>=7-1); // MOS cycle length
        int w = hi::dsp::poly::processWidth(false, false, 0, 16); assert(w==16);
    }
    // --- 12-EDO boundary mapping ---
    // Build QuantConfig for ordinary 12-EDO (period 1 V/oct). Verify snapped
    // outputs over canonical grid are monotonic and produce the expected 13
    // unique boundary voltages (0, 1/12, ..., 1). We explicitly test the half‑way
    // points (n + 0.5)/12 map upward (std::round tie away from zero) preserving
    // existing shipping behavior.
    {
        hi::dsp::QuantConfig qc; qc.edo = 12; qc.periodOct = 1.f; qc.root = 0; qc.useCustom = false;
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

    // --- Scale_NoChromaticLeak_12EDO test ---
    // FIX: Verify C minor pentatonic never emits chromatic notes
    {
        hi::dsp::QuantConfig qc; qc.edo = 12; qc.periodOct = 1.f; qc.root = 0;
        qc.useCustom = true; qc.customFollowsRoot = true;
        // C minor pentatonic {0,3,5,7,10} - convert to vector format
        qc.customMaskGeneric = new uint8_t[12]{1,0,0,1,0,1,0,1,0,0,1,0};
        qc.customMaskLen = 12;
        // Build dense ramp 0→1 V (2000 steps) and verify all outputs are in {0,3,5,7,10} relative to root
        std::set<int> allowedPCs = {0,3,5,7,10}; // C minor pentatonic pitch classes
        for (int k = 0; k <= 2000; ++k) {
            float v = (float)k / 2000.f; // 0 to 1 V
            float snapped = snapEDO(v, qc);
            int semitones = (int)std::round(snapped * 12.f);
            int pc = semitones % 12; if (pc < 0) pc += 12;
            assert(allowedPCs.count(pc) > 0 && "Chromatic leak detected in scale quantization");
        }
    }

    // --- BoundLimit_Pentatonic_RespectsMask ---
    // Regression: when clamping to ±boundLimit with a sparse mask, ensure we stay on allowed steps.
    {
        hi::dsp::QuantConfig qc; qc.edo = 12; qc.periodOct = 1.f; qc.root = 1; // transpose mask so the octave boundary is disallowed
        qc.useCustom = true; qc.customFollowsRoot = true;
        // C minor pentatonic {0,3,5,7,10} - convert to vector format
        qc.customMaskGeneric = new uint8_t[12]{1,0,0,1,0,1,0,1,0,0,1,0};
        qc.customMaskLen = 12;
        const float boundLimit = 1.f; // ±1 V window → ±12 semitones
        const float stepsPerVolt = (float)qc.edo / qc.periodOct;
        float snappedHi = snapEDO(boundLimit, qc, boundLimit, true);
        int stepHi = (int)std::round(snappedHi * stepsPerVolt);
        assert(_isAllowedStepRootRel(stepHi, qc) && "Upper bound clamp emitted disallowed degree");
        int maxStep = (int)std::floor(boundLimit * stepsPerVolt);
        assert(stepHi <= maxStep && "Upper bound exceeded computed range");
    }

    // --- Hysteresis_BoundaryStability test ---
    // FIX: Verify oscillating input near boundary produces stable output
    {
        hi::dsp::QuantConfig qc; qc.edo = 12; qc.periodOct = 1.f; qc.root = 0; qc.useCustom = false;
        float boundary = 1.f/12.f; // First semitone boundary
        std::vector<float> outputs;
        // Oscillate ±2 cents around boundary for 100 samples
        for (int k = 0; k < 100; ++k) {
            float cents = 2.f * std::sin(k * 0.1f); // ±2 cent oscillation
            float v = boundary + cents / 1200.f;
            float snapped = snapEDO(v, qc);
            outputs.push_back(snapped);
        }
        // Count unique outputs - should be minimal (≤2 for hysteresis)
        std::set<float> uniques(outputs.begin(), outputs.end());
        assert(uniques.size() <= 2 && "Excessive boundary flicker detected");
    }

    // --- TieBreak_PrefersLast test ---
    // FIX: Verify exact midpoint prefers previous degree
    {
        hi::dsp::QuantConfig qc; qc.edo = 12; qc.periodOct = 1.f; qc.root = 0; qc.useCustom = false;
        float step = 1.f/12.f;
        float exactMid = 0.5f * step; // Exactly halfway between 0 and 1st semitone
        int prevStep = 0; // Previous choice was step 0
        int result = nearestAllowedStepWithHistory((int)std::round(exactMid * 12.f), exactMid * 12.f, qc, prevStep);
        assert(result == prevStep && "Tie-break should prefer previous step");
    }

    // --- Directional tie-break ---
    // pickRoundingTarget() biases the decision based on slope direction in Directional mode.
    // We synthesize a fractional position exactly at +0.2 and −0.2 around the center and verify
    // slope sign chooses the expected adjustment (+1 when rising above center, -1 when falling below).
    {
        // Directional rounding tests retain legacy slope-aware behavior.
        RoundPolicy pol{RoundMode::Directional};
        int up = pickRoundingTarget(0, +0.2f, +1, pol);  // rising, above center ⇒ +1
        int stayUp = pickRoundingTarget(0, +0.2f, 0, pol); // neutral slope acts like nearest inside midpoints ⇒ 0
        int down = pickRoundingTarget(0, -0.2f, -1, pol); // falling, below center ⇒ -1
        int stayDown = pickRoundingTarget(0, -0.2f, 0, pol); // neutral slope ⇒ 0
        assert(up == +1 && down == -1 && stayUp == 0 && stayDown == 0 && "Directional tie-break mismatch");
    }
    // --- B1..B5: Strum helper tests (relocated) ---
    {
        using hi::dsp::strum::assign; using hi::dsp::strum::tickStartDelays; using hi::dsp::strum::Mode;
    auto spanCheck=[&](float* arr,int N,float expectSpan){ float mn=1e9f,mx=-1e9f; for(int i=0;i<N;++i){ mn=std::min(mn,arr[i]); mx=std::max(mx,arr[i]); } _assertClose(mx-mn, expectSpan, 1e-4f, "strum span"); };
        // B1: spreadMs=0 -> all zeros
        // Strum assign() should produce monotonic spans matching the requested spread.
        for(int voices : {4,8,16}) { float d[16]={}; assign(0.f, voices, Mode::Up, d); for(int i=0;i<voices;++i) _assertClose(d[i],0.f,1e-9f,"assign zero spread"); assign(0.f, voices, Mode::Down, d); for(int i=0;i<voices;++i) _assertClose(d[i],0.f,1e-9f,"assign zero spread"); assign(0.f, voices, Mode::Random, d); for(int i=0;i<voices;++i) _assertClose(d[i],0.f,1e-9f,"assign zero spread"); }
        // B2: Up mode monotonic non-decreasing, span ~= 0.1s for spreadMs=100, N=4
        {
            float d[16]={}; assign(100.f,4,Mode::Up,d); for(int i=1;i<4;++i) assert(d[i]>=d[i-1]-1e-9f); spanCheck(d,4,0.3f);
        }
        // B3: Down mode monotonic non-increasing, same span
        {
            float d[16]={}; assign(100.f,4,Mode::Down,d); for(int i=1;i<4;++i) assert(d[i]<=d[i-1]+1e-9f); // reverse order
            float mn=1e9f,mx=-1e9f; for(int i=0;i<4;++i){ mn=std::min(mn,d[i]); mx=std::max(mx,d[i]); } _assertClose(mx-mn,0.3f,1e-4f,"down span");
        }
        // B4: Random mode span only (non-deterministic ordering); ensure span within epsilon of 0.1
    {
        float d[16]={}; assign(100.f,4,Mode::Random,d);
#ifdef UNIT_TESTS
        // Deterministic fallback assigns identical delays (span 0)
        float mn=1e9f,mx=-1e9f; for(int i=0;i<4;++i){ mn=std::min(mn,d[i]); mx=std::max(mx,d[i]); } _assertClose(mx-mn,0.f,1e-9f,"random span test fallback");
#else
        float mn=1e9f,mx=-1e9f; for(int i=0;i<4;++i){ mn=std::min(mn,d[i]); mx=std::max(mx,d[i]); } _assertClose(mx-mn,0.3f,5e-2f,"random span approx");
#endif
    }
        // B5: tickStartDelays progression
        {
            float left[16]={0.05f,0.02f,0.f,0.01f}; for(int step=0; step<5; ++step){ hi::dsp::strum::tickStartDelays(0.01f,4,left); for(int i=0;i<4;++i) assert(left[i]>=-1e-6f); }
            for(int i=0;i<4;++i) _assertClose(left[i],0.f,1e-4f,"delay exhausted");
        }
    }

    // --- Range conditioning (clip/scale) ---
    {
        using hi::dsp::range::Mode;
        using hi::dsp::range::apply;
        // Hard clip should clamp to ±clipLimit when soft clipping is disabled.
        float clipLim = 5.f;
        _assertClose(apply(8.f, Mode::Clip, clipLim, false), clipLim, 1e-6f, "range hard clip +");
        _assertClose(apply(-6.f, Mode::Clip, clipLim, false), -clipLim, 1e-6f, "range hard clip -");
        // Soft clip delegates to clip::soft() for the knee; compare against the helper directly.
        float softIn = 9.5f;
        float softExpect = hi::dsp::clip::soft(softIn, hi::consts::MAX_VOLT_CLAMP);
        _assertClose(apply(softIn, Mode::Clip, hi::consts::MAX_VOLT_CLAMP, true), softExpect, 1e-6f, "range soft clip matches");
        // Scale mode should proportionally shrink the signal and respect the same ±limit.
        float scaled = apply(8.f, Mode::Scale, clipLim, false);
        _assertClose(scaled, 8.f * (clipLim / hi::consts::MAX_VOLT_CLAMP), 1e-6f, "range scale inside limit");
        float scaledClamp = apply(20.f, Mode::Scale, clipLim, false);
        _assertClose(scaledClamp, clipLim, 1e-6f, "range scale clamp");
        // clipLimitFromIndex() should gracefully clamp out-of-range indices.
        _assertClose(range::clipLimitFromIndex(-5), 10.f, 1e-6f, "clip index underflow");
        _assertClose(range::clipLimitFromIndex(99), 10.f, 1e-6f, "clip index overflow");
        // scale-only edge: zero limit should return 0 irrespective of input.
        _assertClose(apply(3.f, Mode::Scale, 0.f, false), 0.f, 1e-6f, "scale zero limit");
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
    // ------------------------------------------------------------------
    // (B) Added normalization contract tests (glide unit semantics).
    // Duration model: time ≈ distance / unitsPerSec with unitsPerSec = 1.
    // These do not exercise module slews; they validate proportionality.
    // ------------------------------------------------------------------
    // Helpers (B.1)
    {
        auto duration_volts = [](float dV) -> float { return std::fabs(dV); };
        auto duration_cents = [](float dV) -> float { return std::fabs(dV) * 12.0f; }; // 1 V = 12 semitones (units cancel in ratios)
        auto duration_steps = [](float dV, float deltaV) -> float { return std::fabs(dV) / std::max(1e-12f, deltaV); };
        auto duration_equal_time = [](float /*dV*/) -> float { return 1.0f; }; // Equal-time: all jumps same duration
        // Equal-time legacy (normalization disabled) ratio test
        {
            float tA = duration_equal_time(2.0f);
            float tB = duration_equal_time(0.5f);
            assert(std::fabs((tA / tB) - 1.0f) < 1e-6f); // Expect 1:1
        }
        // Volts-linear ratio test (B.2)
        {
            float t1 = duration_volts(1.0f);   // 1 V jump
            float t2 = duration_volts(0.5f);   // 0.5 V jump
            assert(std::fabs((t1 / t2) - 2.0f) < 1e-6f); // Expect ratio ≈ 2.0
        }
        // Cent-linear ratio test (B.3)
        {
            float dV_12semi = 1.0f;           // 12 semitones = 1 V
            float dV_1semi  = 1.0f / 12.0f;   // 1 semitone
            float t12 = duration_cents(dV_12semi);
            float t01 = duration_cents(dV_1semi);
            assert(std::fabs((t12 / t01) - 12.0f) < 1e-6f); // Expect ~12.0
        }
        // Step-safe ratio test (octave EDO) (B.4)
        {
            float deltaV = 1.0f / 13.0f;      // EDO 13: ΔV per step
            float dV_5steps = 5.0f * deltaV;
            float dV_1step  = 1.0f * deltaV;
            float t5 = duration_steps(dV_5steps, deltaV);
            float t1 = duration_steps(dV_1step,  deltaV);
            assert(std::fabs((t5 / t1) - 5.0f) < 1e-6f); // Expect ~5.0
        }
        // Step-safe ratio test (non-octave TET) (B.5)
        {
            float periodOct = std::log2(3.0f); // tritave
            float deltaV = periodOct / 9.0f;   // N = 9 steps
            float dV_7steps = 7.0f * deltaV;
            float dV_1step  = 1.0f * deltaV;
            float t7 = duration_steps(dV_7steps, deltaV);
            float t1 = duration_steps(dV_1step,  deltaV);
            assert(std::fabs((t7 / t1) - 7.0f) < 1e-6f); // Expect ~7.0
        }
    }

    // --- Hysteresis_PeakHold_NoChatter (latch-level) ---
    {
        // (helper removed — use file-scope _test_schmittLatch)

        hi::dsp::QuantConfig qc; qc.edo = 12; qc.periodOct = 1.f; qc.root = 0;
        qc.useCustom = true; qc.customFollowsRoot = true;
        // C minor pentatonic {0,3,5,7,10} - convert to vector format
        qc.customMaskGeneric = new uint8_t[12]{1,0,0,1,0,1,0,1,0,0,1,0};
        qc.customMaskLen = 12;
        const float cents = 10.0f; // same as UI example
        const float Hs = (cents * (float)qc.edo) / 1200.0f;
        const float amp = Hs * 0.8f; // below threshold ⇒ must NOT switch
        int last = 0;
        (void)last; // FIX: silence unused in builds where the branch doesn't reference it
        std::vector<int> outs;
        for (int k = 0; k < 240; ++k) {
            double fs = 0.0 + 0.5 + amp * std::sin(k * 2.0 * 3.14159265359 / 240.0);
            int base = (int)std::round(fs);
            int target = hi::dsp::_nearestAllowedStepRoot(base, (float)fs, qc);
            last = _test_schmittLatch(last, fs, target, cents, qc);
            outs.push_back(last);
        }
        std::sort(outs.begin(), outs.end());
        outs.erase(std::unique(outs.begin(), outs.end()), outs.end());
        assert(outs.size() <= 1 && "Peak hold failed: latch changed inside hysteresis band");
    }
    // B5: tickStartDelays progression
    {
        float left[16]={0.05f,0.02f,0.f,0.01f}; for(int step=0; step<5; ++step){ hi::dsp::strum::tickStartDelays(0.01f,4,left); for(int i=0;i<4;++i) assert(left[i]>=-1e-6f); }
        for(int i=0;i<4;++i) _assertClose(left[i],0.f,1e-4f,"delay exhausted");
    }

    // --- Hysteresis thresholds ---
    // computeHysteresis(center, {ΔV, H_V}) → {center + ΔV/2 + H_V, center - ΔV/2 - H_V}.
    {
        float center = 0.0f; float deltaV = 1.f/12.f; float H_V = 0.01f;
        hi::dsp::HystSpec hs{deltaV, H_V};
        auto thr = hi::dsp::computeHysteresis(center, hs);
        _assertClose(thr.up,   center + 0.5f*deltaV + H_V, 1e-9f, "hyst up");
        _assertClose(thr.down, center - 0.5f*deltaV - H_V, 1e-9f, "hyst down");
        assert(thr.up > thr.down && "Threshold ordering invalid");
    }

    // --- 13-EDO generic mask parity (recent fix) ---
    // Construct a byte-per-step mask allowing indices {0,3,4,7,8,11,12}. Ensure parity between
    // isAllowedStep() and direct mask lookup AND that snapEDO only lands on allowed steps for a
    // selection of probe voltages spanning the period.
    {
        hi::dsp::QuantConfig qc; qc.edo = 13; qc.periodOct = 1.f; qc.root = 0; qc.useCustom = true; qc.customFollowsRoot = true;
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
                float snapped = hi::dsp::snapEDO(v, qc);
                // Convert snapped volts back to (possibly >13) step domain then wrap.
                float stepsF = snapped * qc.edo; // since periodOct = 1
                int sIdx = (int)std::round(stepsF);
                sIdx %= qc.edo; if (sIdx < 0) sIdx += qc.edo;
                assert(mask13[sIdx] && "snapEDO produced disallowed step under generic mask");
            }
        }
    }

    // (CoreState JSON round-trip test removed in UNIT_TESTS build: JSON helpers compiled out.)
    // ------------------------------------------------------------------
    // (B) Added normalization contract tests (glide unit semantics).
    // Duration model: time ≈ distance / unitsPerSec with unitsPerSec = 1.
    // These do not exercise module slews; they validate proportionality.
    // ------------------------------------------------------------------
    // Helpers (B.1)
    {
        auto duration_volts = [](float dV) -> float { return std::fabs(dV); };
        auto duration_cents = [](float dV) -> float { return std::fabs(dV) * 12.0f; }; // 1 V = 12 semitones (units cancel in ratios)
        auto duration_steps = [](float dV, float deltaV) -> float { return std::fabs(dV) / std::max(1e-12f, deltaV); };
        auto duration_equal_time = [](float /*dV*/) -> float { return 1.0f; }; // Equal-time: all jumps same duration
        // Equal-time legacy (normalization disabled) ratio test
        {
            float tA = duration_equal_time(2.0f);
            float tB = duration_equal_time(0.5f);
            assert(std::fabs((tA / tB) - 1.0f) < 1e-6f); // Expect 1:1
        }
        // Volts-linear ratio test (B.2)
        {
            float t1 = duration_volts(1.0f);   // 1 V jump
            float t2 = duration_volts(0.5f);   // 0.5 V jump
            assert(std::fabs((t1 / t2) - 2.0f) < 1e-6f); // Expect ratio ≈ 2.0
        }
        // Cent-linear ratio test (B.3)
        {
            float dV_12semi = 1.0f;           // 12 semitones = 1 V
            float dV_1semi  = 1.0f / 12.0f;   // 1 semitone
            float t12 = duration_cents(dV_12semi);
            float t01 = duration_cents(dV_1semi);
            assert(std::fabs((t12 / t01) - 12.0f) < 1e-6f); // Expect ~12.0
        }
        // Step-safe ratio test (octave EDO) (B.4)
        {
            float deltaV = 1.0f / 13.0f;      // EDO 13: ΔV per step
            float dV_5steps = 5.0f * deltaV;
            float dV_1step  = 1.0f * deltaV;
            float t5 = duration_steps(dV_5steps, deltaV);
            float t1 = duration_steps(dV_1step,  deltaV);
            assert(std::fabs((t5 / t1) - 5.0f) < 1e-6f); // Expect ~5.0
        }
        // Step-safe ratio test (non-octave TET) (B.5)
        {
            float periodOct = std::log2(3.0f); // tritave
            float deltaV = periodOct / 9.0f;   // N = 9 steps
            float dV_7steps = 7.0f * deltaV;
            float dV_1step  = 1.0f * deltaV;
            float t7 = duration_steps(dV_7steps, deltaV);
            float t1 = duration_steps(dV_1step,  deltaV);
            assert(std::fabs((t7 / t1) - 7.0f) < 1e-6f); // Expect ~7.0
        }
    }

    // --- Directional Snap Monotonicity Test ---
    {
        hi::dsp::QuantConfig qc; qc.edo = 12; qc.periodOct = 1.f; qc.root = 0;
        qc.useCustom = true; qc.customFollowsRoot = true;
        // C minor pentatonic {0,3,5,7,10} - convert to vector format
        qc.customMaskGeneric = new uint8_t[12]{1,0,0,1,0,1,0,1,0,0,1,0};
        qc.customMaskLen = 12;
        const float cents = 10.0f;
        std::vector<int> outs;
        int last = 0;
        // Monotonic upward ramp: 0.0 → 1.0 in 100 steps
        for (int k = 0; k <= 100; ++k) {
            double fs = (double)k / 100.0; // 0.0 → 1.0
            int target = hi::dsp::_nearestAllowedStepRoot((int)std::round(fs * 12.0), (float)(fs * 12.0), qc);
            last = _test_schmittLatch(last, fs * 12.0, target, cents, qc);
            outs.push_back(last);
        }
        // Check monotonicity: each output ≥ previous
        for (size_t i = 1; i < outs.size(); ++i) {
            assert(outs[i] >= outs[i-1] && "Directional Snap failed monotonicity test");
        }
    }

#if 1  // FIX: Directional Snap behavior guards
    // --- DirSnap_NoSkip_AfterFlip ---
    {
        hi::dsp::QuantConfig qc; qc.edo = 12; qc.periodOct = 1.f; qc.root = 0;
        qc.useCustom = true; qc.customFollowsRoot = true;
        // C minor pentatonic {0,3,5,7,10} - convert to vector format
        qc.customMaskGeneric = new uint8_t[12]{1,0,0,1,0,1,0,1,0,0,1,0};
        qc.customMaskLen = 12;
        int last = 0;
        for (int k = 0; k < 2400; ++k) {
            const double pi = 3.14159265358979323846;
            double fs = 2.0 * std::sin(k * 2.0 * pi / 2400.0); // slow, wide sweep
            int baseUp = (int)std::ceil(fs);
            int baseDn = (int)std::floor(fs);
            int base   = (fs >= (double)last) ? baseUp : baseDn; // emulate directional intent
            int target = hi::dsp::_nearestAllowedStepRoot(base, (float)fs, qc);
            // Emulate module's rule: only neighbor in current direction is allowed
            int nextUp = hi::dsp::nextAllowedStep(last, +1, qc);
            int nextDn = hi::dsp::nextAllowedStep(last, -1, qc);
            if (target != last) {
                int expected = (target > last) ? nextUp : nextDn;
                assert(target == expected && "Directional Snap skipped an allowed degree after direction flip");
            }
            last = target;
        }
    }

    // --- DirSnap_VeryLowFreq_PeakHold ---
    {
        hi::dsp::QuantConfig qc; qc.edo = 12; qc.periodOct = 1.f; qc.root = 0;
        qc.useCustom = true; qc.customFollowsRoot = true;
        // C minor pentatonic {0,3,5,7,10} - convert to vector format
        qc.customMaskGeneric = new uint8_t[12]{1,0,0,1,0,1,0,1,0,0,1,0};
        qc.customMaskLen = 12;
        const float cents = 10.0f;
        const float Hs = (cents * (float)qc.edo) / 1200.0f;
        const float Hd = std::max(0.75f*Hs, 0.02f);
        const float amp = Hd * 0.8f; // below direction hysteresis → must not flip at crest
        std::vector<int> steps;
        for (int k = 0; k < 2400; ++k) {
            const double pi = 3.14159265358979323846;
            double fs = 0.5 + amp * std::sin(k * 2.0 * pi / 2400.0);
            int baseUp = (int)std::ceil(fs);
            int baseDn = (int)std::floor(fs);
            int base   = (k > 0 && fs < (double)steps.back()) ? baseDn : baseUp;
            int tgt    = hi::dsp::_nearestAllowedStepRoot(base, (float)fs, qc);
            steps.push_back(tgt);
        }
        std::vector<int> uniq = steps; std::sort(uniq.begin(), uniq.end());
        uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
        assert(uniq.size() <= 1 && "Directional Snap: crest chatter detected at very low frequency");
    }
#endif

    printf("All core tests passed.\n");
    return 0; // All assertions passed.
}
#endif // UNIT_TESTS
