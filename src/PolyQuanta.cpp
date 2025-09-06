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
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>

// -----------------------------------------------------------------------------
// Inlined helpers
// -----------------------------------------------------------------------------

namespace hi { namespace consts {
// Global reusable constants — C++11 friendly
static constexpr float MAX_VOLT_CLAMP = 10.f;   // Output clamp (±10 V typical)
static constexpr float LED_SCALE_V    = 10.f;   // LED normalization divisor
static constexpr float MIN_SEC        = 1e-4f;  // ~0.1 ms → treat as "no slew"
static constexpr float MAX_SEC        = 10.f;   // 10 s max
static constexpr float EPS_ERR        = 1e-4f;  // tiny error epsilon for early-out and guards
static constexpr float RATE_EPS       = 1e-3f;  // minimal rate change to update SlewLimiter
}} // namespace hi::consts

namespace hi { namespace dsp { namespace clip {
// Hard clamp to ±maxV
static inline float hard(float v, float maxV) {
    if (v >  maxV) return  maxV;
    if (v < -maxV) return -maxV;
    return v;
}
// Soft clip with 1 V knee approaching ±maxV without compressing interior range.
// Behavior: linear pass-through until |v| exceeds (maxV - knee). Within the last
// knee volts, apply a smooth cosine easing to reach exactly ±maxV. Anything
// beyond ±maxV hard-clips. This preserves precise offsets (e.g. +10 V stays +10 V)
// while still avoiding a sharp corner at the ceiling when soft clipping is chosen.
static inline float soft(float v, float maxV) {
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
static inline float voltsToSemitones(float v) { return v * 12.f; }
static inline float semitonesToVolts(float s) { return s / 12.f; }
// Shape helpers: map shape in [-1,1] to expo/log-ish multiplier parameters.
struct ShapeParams { float k = 0.f; float c = 1.f; bool negative = false; };
static inline ShapeParams makeShape(float shape, float kPos = 6.f, float kNeg = 8.f) {
    ShapeParams p;
    if (std::fabs(shape) < 1e-6f) { p.k = 0.f; p.c = 1.f; p.negative = false; return p; }
    if (shape < 0.f) { p.k = kNeg * (-shape); p.c = (1.f - std::exp(-p.k)) / p.k; p.negative = true; }
    else { p.k = kPos * shape; p.c = 1.f + 0.5f * p.k; p.negative = false; }
    return p;
}
// u in [0,1] is normalized error progress. Returns multiplier ≥ EPS.
static inline float shapeMul(float u, const ShapeParams& p, float eps = 1e-6f) {
    if (p.k == 0.f) return 1.f;
    float m = p.negative ? std::exp(p.k * u) : 1.f / (1.f + p.k * u);
    float out = p.c * m;
    return out < eps ? eps : out;
}
}}} // namespace hi::dsp::glide

namespace hi { namespace music {
struct Scale { const char* name; unsigned int mask; };
// 12-EDO scales (bit 0 = root degree)
static constexpr int NUM_SCALES12 = 14;
static inline const Scale* scales12() {
    static const Scale S[] = {
    {"Chromatic",      0xFFFu},
    // Bit 0 = root (0 st), increasing by semitone up to bit 11 = 11 st
    {"Major (Ionian)", (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<11)},
    {"Natural minor",  (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10)},
    {"Harmonic minor", (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<11)},
    {"Melodic minor",  (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<11)},
    {"Pentatonic maj", (1u<<0)|(1u<<2)|(1u<<4)|(1u<<7)|(1u<<9)},
    {"Pentatonic min", (1u<<0)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<10)},
    // Common hexatonic blues: 1 b3 4 b5 5 b7 (plus root)
    {"Blues",          (1u<<0)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<10)},
    {"Dorian",         (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<10)},
    {"Mixolydian",     (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<10)},
    {"Phrygian",       (1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10)},
    {"Lydian",         (1u<<0)|(1u<<2)|(1u<<4)|(1u<<6)|(1u<<7)|(1u<<9)|(1u<<11)},
    {"Locrian",        (1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<8)|(1u<<10)},
    {"Whole tone",     (1u<<0)|(1u<<2)|(1u<<4)|(1u<<6)|(1u<<8)|(1u<<10)}
    };
    return S;
}
// 24-EDO preset scales (bit 0 = root; masks are musical approximations)
static constexpr int NUM_SCALES24 = 7;
static inline const Scale* scales24() {
    static const Scale S[] = {
        {"Quarter-tone Major", (
            (1u<<0)  | (1u<<4)  | (1u<<8)  | (1u<<10) | (1u<<14) | (1u<<18) | (1u<<22)
        )},
        {"Chromatic Blues (24)", (
            (1u<<0)  | (1u<<6)  | (1u<<10) | (1u<<12) | (1u<<14) | (1u<<20)
        )},
        {"Quarter-tone Maqam (Rast)", (
            (1u<<0)  | (1u<<4)  | (1u<<7)  | (1u<<10) | (1u<<14) | (1u<<18) | (1u<<21)
        )},
        {"Neutral 3rd Pentatonic (Maj)", (
            (1u<<0)  | (1u<<4)  | (1u<<7)  | (1u<<14) | (1u<<18)
        )},
        {"Neutral 3rd Pentatonic (Min)", (
            (1u<<0)  | (1u<<7)  | (1u<<10) | (1u<<14) | (1u<<20)
        )},
        {"Porcupine", (
            (1u<<0)  | (1u<<3)  | (1u<<6)  | (1u<<10) | (1u<<13) | (1u<<16) | (1u<<20) | (1u<<23)
        )},
        {"Quarter-tone Whole-tone", (
            (1u<<0)  | (1u<<4)  | (1u<<8)  | (1u<<12) | (1u<<16) | (1u<<20)
        )}
    };
    return S;
}
}} // namespace hi::music

namespace hi { namespace music { namespace edo {
// Curated EDO presets grouped by usefulness
static inline const std::vector<int>& near12() { static const std::vector<int> v{10,14,16}; return v; }
static inline const std::vector<int>& diatonicFavs() { static const std::vector<int> v{19,31,22,17,13}; return v; }
static inline const std::vector<int>& microFamilies() { static const std::vector<int> v{18,36,48,72}; return v; }
static inline const std::vector<int>& jiAccurate() { static const std::vector<int> v{41,53}; return v; }
static inline const std::vector<int>& extras() { static const std::vector<int> v{11,20,26,34}; return v; }
static inline std::vector<int> allRecommended() {
    std::vector<int> all; all.reserve(32);
    auto add=[&](const std::vector<int>& g){ all.insert(all.end(), g.begin(), g.end()); };
    add(near12()); add(diatonicFavs()); add(microFamilies()); add(jiAccurate()); add(extras());
    std::vector<int> out; out.reserve(all.size());
    for (int n : all) { if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n); }
    return out;
}
}}} // namespace hi::music::edo

namespace hi { namespace music { namespace tets {
struct Tet { const char* name; int steps; float periodOct; };
static inline const std::vector<Tet>& carlos() {
    static const std::vector<Tet> v{
        {"Carlos Alpha", 9,  std::log2(3.f/2.f)},
        {"Carlos Beta",  11, std::log2(3.f/2.f)},
        {"Carlos Gamma", 20, std::log2(3.f/2.f)}
    };
    return v;
}
}}} // namespace hi::music::tets

namespace hi { namespace dsp { namespace range {
enum class Mode { Clip = 0, Scale = 1 };
// Map a UI index to a half-range (±limit) in volts.
static inline float clipLimitFromIndex(int idx) {
    switch (idx) {
        case 0: return 10.f; case 1: return 7.5f; case 2: return 5.f;
        case 3: return 2.5f; case 4: return 1.f;  case 5: return 0.5f;
    }
    return 10.f;
}
// Apply pre-quant range handling around 0V only.
static inline float apply(float v, Mode mode, float clipLimit, bool soft) {
    if (mode == Mode::Clip) {
        return soft ? clip::soft(v, clipLimit) : rack::clamp(v, -clipLimit, clipLimit);
    }
    float s = clipLimit / hi::consts::MAX_VOLT_CLAMP; // Scale mode
    float vs = v * s;
    return rack::clamp(vs, -clipLimit, clipLimit);
}
}}} // namespace hi::dsp::range

namespace hi { namespace dsp { namespace strum {
enum class Mode { Up = 0, Down = 1, Random = 2 };
enum class Type { TimeStretch = 0, StartDelay = 1 };
// Assign per-channel delays (seconds) given ms spread, voice count, and mode.
static inline void assign(float spreadMs, int N, Mode mode, float outDelaySec[16]) {
    float base = (spreadMs <= 0.f) ? 0.f : (spreadMs * 0.001f);
    for (int ch = 0; ch < N && ch < 16; ++ch) {
        float d = 0.f;
        switch (mode) {
            case Mode::Up:     d = base * ch; break;
            case Mode::Down:   d = base * (N - 1 - ch); break;
            case Mode::Random: d = base * rack::random::uniform(); break;
        }
        outDelaySec[ch] = d;
    }
}
// Tick countdown timers for StartDelay type.
static inline void tickStartDelays(float dt, int N, float delaysLeft[16]) {
    for (int ch = 0; ch < N && ch < 16; ++ch) {
        if (delaysLeft[ch] > 0.f) {
            delaysLeft[ch] -= dt; if (delaysLeft[ch] < 0.f) delaysLeft[ch] = 0.f;
        }
    }
}
}}} // namespace hi::dsp::strum

namespace hi { namespace dsp { namespace poly {
static inline int processWidth(bool forcePolyOut, bool inputConnected, int inputChannels, int maxCh = 16) {
    int n = forcePolyOut ? maxCh : (inputConnected ? inputChannels : maxCh);
    return std::min(n, maxCh);
}
}}} // namespace hi::dsp::poly

namespace hi { namespace ui {
struct ExpTimeQuantity : rack::engine::ParamQuantity {
    static float knobToSec(float x) {
        const float lmin = std::log10(hi::consts::MIN_SEC);
        const float lmax = std::log10(hi::consts::MAX_SEC);
        float lx = lmin + (lmax - lmin) * rack::clamp(x, 0.f, 1.f);
        return std::pow(10.f, lx);
    }
    static float secToKnob(float sec) {
        sec = rack::clamp(sec, hi::consts::MIN_SEC, hi::consts::MAX_SEC);
        const float lmin = std::log10(hi::consts::MIN_SEC);
        const float lmax = std::log10(hi::consts::MAX_SEC);
        return (std::log10(sec) - lmin) / (lmax - lmin);
    }
    float getDisplayValue() override { return knobToSec(getValue()); }
    void setDisplayValue(float disp) override { setValue(secToKnob(disp)); }
    std::string getDisplayValueString() override {
        float sec = knobToSec(getValue());
        if (sec < 1.f) return rack::string::f("%.1f ms", sec * 1000.f);
        if (sec < 10.f) return rack::string::f("%.2f s", sec);
        return rack::string::f("%.1f s", sec);
    }
    void setDisplayValueString(std::string s) override {
        std::string t = s; for (auto& c : t) c = (char)std::tolower((unsigned char)c);
        while (!t.empty() && std::isspace((unsigned char)t.front())) t.erase(t.begin());
        while (!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
        bool isMs = (t.find('m') != std::string::npos);
        float v = knobToSec(getValue());
        try { v = std::stof(t); } catch (...) {}
        float sec = isMs ? v / 1000.f : v; setValue(secToKnob(sec));
    }
};
}} // namespace hi::ui

namespace hi { namespace ui { namespace menu {
using Menu = rack::ui::Menu; using MenuSeparator = rack::ui::MenuSeparator; using MenuItem = rack::ui::MenuItem;
static inline void addSection(Menu* m, const char* label) { m->addChild(new MenuSeparator); m->addChild(rack::createMenuLabel(label)); }
static inline MenuItem* addBoolPtr(Menu* m, const char* title, bool* ptr) { auto* item = rack::createBoolPtrMenuItem(title, "", ptr); m->addChild(item); return item; }
static inline MenuItem* addBoolPtr(Menu* m, const char* title, bool* ptr, const std::function<bool()>& enabled) { auto* item = addBoolPtr(m, title, ptr); if (enabled) item->disabled = !enabled(); return item; }
}}} // namespace hi::ui::menu

namespace hi { namespace ui {
struct ShapeQuantity : rack::engine::ParamQuantity {
    std::string getDisplayValueString() override {
        float v = getValue(); float a = std::fabs(v);
        if (a < 0.02f) return std::string("Linear");
        int pct = (int)std::round(a * 100.f);
        if (v > 0.f) return rack::string::f("Exp %d%%", pct);
        return rack::string::f("Log %d%%", pct);
    }
};
}} // namespace hi::ui

namespace hi { namespace ui {
struct SemitoneVoltQuantity : rack::engine::ParamQuantity {
    const int* quantizeOffsetModePtr = nullptr; // 1=semitones, 2=cents
    const int* edoPtr = nullptr;
    std::string getDisplayValueString() override {
        float v = getValue();
        int mode = (quantizeOffsetModePtr ? *quantizeOffsetModePtr : 0);
        if (mode == 1) { // semitones in current EDO
            int N = (edoPtr && *edoPtr > 0) ? *edoPtr : 12;
            int st = (int)std::round(v * (float)N);
            return rack::string::f("%d st", st);
        } else if (mode == 2) { // cents (1/1200 V)
            int ct = (int)std::round(v * 1200.f);
            return rack::string::f("%d ct", ct);
        }
        return rack::string::f("%.2f V", v);
    }
    void setDisplayValueString(std::string s) override {
        std::string t = s; for (auto& c : t) c = (char)std::tolower((unsigned char)c);
        while (!t.empty() && std::isspace((unsigned char)t.front())) t.erase(t.begin());
        while (!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
        bool hasV = (t.find('v')!=std::string::npos);
        bool hasSt = (t.find("st")!=std::string::npos)||(t.find("semi")!=std::string::npos);
        bool hasCt = (t.find("ct")!=std::string::npos)||(t.find("cent")!=std::string::npos);
        float x=0.f; try { x = std::stof(t); } catch(...) {}
        int mode = (quantizeOffsetModePtr ? *quantizeOffsetModePtr : 0);
        bool semis = (mode==1)&&!hasV; if (hasSt) semis=true;
        bool cents = (mode==2)&&!hasV; if (hasCt) cents=true;
        if (semis) {
            int N = (edoPtr && *edoPtr > 0) ? *edoPtr : 12;
            float volts = x / (float)N;
            setValue(rack::clamp(volts, -10.f, 10.f));
        } else if (cents) {
            float volts = x / 1200.f;
            setValue(rack::clamp(volts, -10.f, 10.f));
        } else {
            setValue(rack::clamp(x, -10.f, 10.f));
        }
    }
};
}} // namespace hi::ui

namespace hi { namespace ui { namespace led {
static inline void setBipolar(rack::engine::Light& g, rack::engine::Light& r, float val, float dt) {
    float gs = rack::clamp( val / hi::consts::LED_SCALE_V, 0.f, 1.f);
    float rs = rack::clamp(-val / hi::consts::LED_SCALE_V, 0.f, 1.f);
    g.setBrightnessSmooth(gs, dt); r.setBrightnessSmooth(rs, dt);
}
}}} // namespace hi::ui::led

namespace hi { namespace ui { namespace overlay {
enum class Kind { Knob, Jack, Led, Button, Switch, Screw };
struct Marker { Kind kind; float xMM; float yMM; float rMM; };
inline const char* cls(Kind k) {
    switch (k) { case Kind::Knob: return "knob"; case Kind::Jack: return "jack"; case Kind::Led: return "led";
        case Kind::Button: return "btn"; case Kind::Switch: return "sw"; case Kind::Screw: return "screw"; }
    return "mark";
}
static inline bool exportOverlay(const std::string& moduleName, float wMM, float hMM, const std::vector<Marker>& marks, const std::string& outPath = "") {
    std::string dir = rack::asset::user(rack::string::f("%s/overlays", pluginInstance->slug.c_str()));
    rack::system::createDirectories(dir);
    std::string path = outPath.empty() ? (dir + "/" + moduleName + "-overlay.svg") : outPath;
    std::ofstream f(path, std::ios::binary); if (!f) return false;
    f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    f << rack::string::f("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.3fmm\" height=\"%.3fmm\" viewBox=\"0 0 %.3f %.3f\">\n", wMM, hMM, wMM, hMM);
    f << "  <defs>\n"
         "    <style><![CDATA[\n"
         "      .outline{fill:none;stroke:#888;stroke-width:0.3}\n"
         "      .knob{fill:none;stroke:#ff9800;stroke-width:0.3}\n"
         "      .jack{fill:none;stroke:#3f51b5;stroke-width:0.3}\n"
         "      .led{fill:none;stroke:#4caf50;stroke-width:0.25}\n"
         "      .sw{fill:none;stroke:#9c27b0;stroke-width:0.25}\n"
         "      .btn{fill:none;stroke:#795548;stroke-width:0.3}\n"
         "      .screw{fill:none;stroke:#607d8b;stroke-width:0.25}\n"
         "      .x{stroke:#999;stroke-width:0.2;stroke-dasharray:0.6,0.6}\n"
         "    ]]></style>\n"
         "  </defs>\n";
    f << rack::string::f("  <rect class=\"outline\" x=\"0\" y=\"0\" width=\"%.3f\" height=\"%.3f\"/>\n", wMM, hMM);
    auto cross = [&](float x, float y){ const float c = 2.5f; f << rack::string::f("  <path class=\"x\" d=\"M %.3f %.3f H %.3f M %.3f %.3f V %.3f\"/>\n", x - c, y, x + c, x, y - c, y + c); };
    auto circle = [&](const char* cls, float x, float y, float r){ f << rack::string::f("  <circle class=\"%s\" cx=\"%.3f\" cy=\"%.3f\" r=\"%.3f\"/>\n", cls, x, y, r); };
    for (const auto& m : marks) { circle(cls(m.kind), m.xMM, m.yMM, m.rMM); cross(m.xMM, m.yMM); }
    f << "</svg>\n"; return true;
}
}}} // namespace hi::ui::overlay

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

namespace hi { namespace dsp {
// Quantization config and snapper supporting arbitrary period sizes (EDO/TET)
struct QuantConfig {
    int edo = 12; float periodOct = 1.f; int root = 0; bool useCustom = false; bool customFollowsRoot = true;
    uint32_t customMask12 = 0xFFFu; uint32_t customMask24 = 0xFFFFFFu; int scaleIndex = 0;
    const uint8_t* customMaskGeneric = nullptr; int customMaskLen = 0;
};
static inline float snapEDO(float volts, const QuantConfig& qc, float boundLimit = 10.f, bool boundToLimit = false, int shiftSteps = 0) {
    int N = (qc.edo <= 0) ? 12 : qc.edo; float period = (qc.periodOct > 0.f) ? qc.periodOct : 1.f;
    float fs = volts * (float)N / period; int root = (qc.root + (shiftSteps % N) + N) % N;
    auto isAllowed = [&](int s) -> bool {
        if (N <= 0)
            return true;
        int pc = s % N;
        if (pc < 0)
            pc += N;
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
    bool anyAllowed = false; for (int k = 0; k < N; ++k) { if (isAllowed(k)) { anyAllowed = true; break; } }
    if (!anyAllowed) { int s = (int)std::round(fs); return (s / (float)N) * period; }
    int sMin = boundToLimit ? (int)std::ceil(-boundLimit * (float)N / period) : (std::numeric_limits<int>::min() / 4);
    int sMax = boundToLimit ? (int)std::floor( boundLimit * (float)N / period) : (std::numeric_limits<int>::max() / 4);
    int s0 = (int)std::round(fs); if (s0 < sMin) s0 = sMin; else if (s0 > sMax) s0 = sMax;
    int best = s0; float bestDist = 1e9f;
    for (int d = 0; d <= N; ++d) {
        int c1 = s0 + d, c2 = s0 - d; if (c1 > sMax) c1 = sMax + 1; if (c2 < sMin) c2 = sMin - 1;
        if (isAllowed(c1)) { float dist = std::fabs(fs - (float)c1); if (dist < bestDist) { bestDist = dist; best = c1; if (d == 0) break; } }
        if (d > 0 && isAllowed(c2)) { float dist = std::fabs(fs - (float)c2); if (dist < bestDist) { bestDist = dist; best = c2; } }
        if (bestDist < 0.5f * 1e-4f) break;
    }
    return (best / (float)N) * period;
}
// Return whether pitch-class step s is allowed under qc (root/mask aware)
static inline bool isAllowedStep(int s, const QuantConfig& qc) {
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
static inline int nextAllowedStep(int start, int dir, const QuantConfig& qc) {
    int N = (qc.edo <= 0) ? 12 : qc.edo; if (N <= 0) return start; if (dir == 0) return start; // invalid dir
    // Search outward up to N steps to avoid infinite loop if no allowed
    for (int k = 1; k <= N; ++k) {
        int s = start + dir * k;
        if (isAllowedStep(s, qc)) return s;
    }
    return start;
}
static inline int nearestAllowedStep(int sGuess, float fs, const QuantConfig& qc) {
    int N = (qc.edo <= 0) ? 12 : qc.edo; if (N <= 0) return 0; int s0 = (int)std::round(fs); int best = s0; float bestDist = 1e9f;
    for (int d = 0; d <= N; ++d) {
        int c1 = s0 + d, c2 = s0 - d; if (isAllowedStep(c1, qc)) { float dist = std::fabs(fs - (float)c1); if (dist < bestDist) { bestDist = dist; best = c1; if (d == 0) break; } }
        if (d > 0 && isAllowedStep(c2, qc)) { float dist = std::fabs(fs - (float)c2); if (dist < bestDist) { bestDist = dist; best = c2; } }
        if (bestDist < 1e-6f) break;
    }
    return best;
}
}} // namespace hi::dsp

namespace hi { namespace dsp { namespace polytrans {
enum Phase { TRANS_STABLE = 0, TRANS_FADE_OUT, TRANS_FADE_IN };
struct State { int curProcN = 0; int curOutN = 0; int pendingProcN = 0; int pendingOutN = 0; float polyRamp = 1.f; Phase transPhase = TRANS_STABLE; bool initToTargetsOnSwitch = false; };
}}} // namespace hi::dsp::polytrans
using namespace rack;
using namespace rack::componentlibrary;
namespace hconst = hi::consts;
// Import transition phase enum values for brevity in this file
using namespace hi::dsp::polytrans;

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
    GLOBAL_SLEW_PARAM,
    GLOBAL_SLEW_MODE_PARAM,   // 0=Slew add (time), 1=Attenuverter (gain)
    GLOBAL_OFFSET_PARAM,
    GLOBAL_OFFSET_MODE_PARAM, // 0=Global offset, 1=Range offset (center)
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

    // Randomize scope options
    bool randSlew = true;
    bool randOffset = true;
    bool randShapes = true;
    // Max randomize delta as fraction of full control range (0.1..1.0)
    float randMaxPct = 1.f;

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
        // Per-channel quantize enables
        for (int i = 0; i < 16; ++i) {
            char key[32];
            std::snprintf(key, sizeof(key), "qzEnabled%d", i+1);
            hi::util::jsonh::writeBool(rootJ, key, qzEnabled[i]);
            std::snprintf(key, sizeof(key), "postOctShift%d", i+1);
            json_object_set_new(rootJ, key, json_integer(postOctShift[i]));
        }
    // Quantization meta
    json_object_set_new(rootJ, "quantStrength", json_real(quantStrength));
    json_object_set_new(rootJ, "quantRoundMode", json_integer(quantRoundMode));
    json_object_set_new(rootJ, "stickinessCents", json_real(stickinessCents));
    json_object_set_new(rootJ, "edo", json_integer(edo));
    json_object_set_new(rootJ, "tuningMode", json_integer(tuningMode));
    json_object_set_new(rootJ, "tetSteps", json_integer(tetSteps));
    json_object_set_new(rootJ, "tetPeriodOct", json_real(tetPeriodOct));
    hi::util::jsonh::writeBool(rootJ, "useCustomScale", useCustomScale);
    hi::util::jsonh::writeBool(rootJ, "rememberCustomScale", rememberCustomScale);
    hi::util::jsonh::writeBool(rootJ, "customScaleFollowsRoot", customScaleFollowsRoot);
    json_object_set_new(rootJ, "customMask12", json_integer(customMask12));
    json_object_set_new(rootJ, "customMask24", json_integer(customMask24));
    // Persist generic custom mask if present
    if (!customMaskGeneric.empty()) {
        json_t* arr = json_array();
        for (size_t i = 0; i < customMaskGeneric.size(); ++i) {
            json_array_append_new(arr, json_integer((int)customMaskGeneric[i]));
        }
        json_object_set_new(rootJ, "customMaskGenericN", json_integer((int)customMaskGeneric.size()));
        json_object_set_new(rootJ, "customMaskGeneric", arr);
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
    // Quantization settings
    json_object_set_new(rootJ, "rootNote", json_integer(rootNote));
    json_object_set_new(rootJ, "scaleIndex", json_integer(scaleIndex));
    // Polyphony transition fade settings
    json_object_set_new(rootJ, "polyFadeSec", json_real(polyFadeSec));
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
        for (int i = 0; i < 16; ++i) {
            char key[32];
            std::snprintf(key, sizeof(key), "qzEnabled%d", i+1);
            qzEnabled[i] = hi::util::jsonh::readBool(rootJ, key, qzEnabled[i]);
            std::snprintf(key, sizeof(key), "postOctShift%d", i+1);
            if (auto* jv = json_object_get(rootJ, key)) postOctShift[i] = (int)json_integer_value(jv);
        }
    if (auto* j = json_object_get(rootJ, "quantStrength")) quantStrength = (float)json_number_value(j);
    if (auto* j = json_object_get(rootJ, "quantRoundMode")) quantRoundMode = (int)json_integer_value(j);
    if (auto* j = json_object_get(rootJ, "stickinessCents")) stickinessCents = (float)json_number_value(j);
    if (auto* j = json_object_get(rootJ, "edo")) edo = (int)json_integer_value(j);
    if (auto* j = json_object_get(rootJ, "tuningMode")) tuningMode = (int)json_integer_value(j);
    if (auto* j = json_object_get(rootJ, "tetSteps")) tetSteps = (int)json_integer_value(j);
    if (auto* j = json_object_get(rootJ, "tetPeriodOct")) tetPeriodOct = (float)json_number_value(j);
    useCustomScale = hi::util::jsonh::readBool(rootJ, "useCustomScale", useCustomScale);
    rememberCustomScale = hi::util::jsonh::readBool(rootJ, "rememberCustomScale", rememberCustomScale);
    customScaleFollowsRoot = hi::util::jsonh::readBool(rootJ, "customScaleFollowsRoot", customScaleFollowsRoot);
    if (auto* j = json_object_get(rootJ, "customMask12")) customMask12 = (uint32_t)json_integer_value(j);
    if (auto* j = json_object_get(rootJ, "customMask24")) customMask24 = (uint32_t)json_integer_value(j);
    // Restore generic custom mask if available
        customMaskGeneric.clear();
        if (auto* arr = json_object_get(rootJ, "customMaskGeneric")) {
            if (json_is_array(arr)) {
                size_t len = json_array_size(arr);
                customMaskGeneric.resize(len);
                for (size_t i = 0; i < len; ++i) {
                    json_t* v = json_array_get(arr, i);
                    customMaskGeneric[i] = (uint8_t)((int)json_integer_value(v) ? 1 : 0);
                }
            }
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
    if (auto* j = json_object_get(rootJ, "rootNote")) rootNote = (int)json_integer_value(j);
    if (auto* j = json_object_get(rootJ, "scaleIndex")) scaleIndex = (int)json_integer_value(j);
    if (auto* j = json_object_get(rootJ, "polyFadeSec")) polyFadeSec = (float)json_number_value(j);
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
        if (rndBtnTrig.process(params[RND_PARAM].getValue() > 0.5f) ||
            rndGateTrig.process(inputs[RND_TRIG_INPUT].getVoltage())) {
            doRandomize();
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
                if (modeChanged) { stepNorm[c] = hconst::EPS_ERR; stepSign[c] = sign; }
                if (sign != stepSign[c] || aerrN > stepNorm[c]) {
                    stepSign[c] = sign;
                    stepNorm[c] = std::max(aerrN, hconst::EPS_ERR);
                }
            }

            float yRaw = target;
            bool inStartDelay = (strumEnabled && strumType == 1 && strumDelayLeft[c] > 0.f);
            if (inStartDelay) {
                // Hold output until delay elapses
                hi::dsp::strum::tickStartDelays((float)args.sampleTime, polyTrans.curProcN, strumDelayLeft);
                yRaw = yPrev; // hold
            } else {
            if (!noSlew) {
                float baseRateN = stepNorm[c] / sec;      // semitones/s or V/s
                float baseRateV = pitchSafeGlide ? hi::dsp::glide::semitonesToVolts(baseRateN) : baseRateN;
                float u = clamp(aerrN / stepNorm[c], 0.f, 1.f);

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
            // Octave shift pre-quant
            // Then apply Range offset before quantizer to shift whole window up/down
            float yBase = yPre + rangeOffset + (float)postOctShift[c] * 1.f;
            float y = yBase;
            if (qzEnabled[c]) {
                // Quantize relative to range offset so bounds follow the shifted window
                float yRel = yBase - rangeOffset;
                // Build current QuantConfig for hysteresis decision
                hi::dsp::QuantConfig qc;
                if (tuningMode == 0) { qc.edo = (edo <= 0) ? 12 : edo; qc.periodOct = 1.f; }
                else { qc.edo = tetSteps > 0 ? tetSteps : 9; qc.periodOct = (tetPeriodOct > 0.f) ? tetPeriodOct : std::log2(3.f/2.f); }
                qc.root = rootNote; qc.useCustom = useCustomScale; qc.customFollowsRoot = customScaleFollowsRoot;
                qc.customMask12 = customMask12; qc.customMask24 = customMask24; qc.scaleIndex = scaleIndex;
                if (qc.useCustom && (qc.edo!=12 && qc.edo!=24)) {
                    if ((int)customMaskGeneric.size()==qc.edo) { qc.customMaskGeneric=customMaskGeneric.data(); qc.customMaskLen=(int)customMaskGeneric.size(); }
                }
                int N = qc.edo; float period = qc.periodOct;
                // Detect config changes requiring re-init
                bool cfgChanged = (prevRootNote!=rootNote || prevScaleIndex!=scaleIndex || prevEdo!=qc.edo || prevTetSteps!=tetSteps || prevTetPeriodOct!=qc.periodOct || prevTuningMode!=tuningMode || prevUseCustomScale!=useCustomScale || prevCustomFollowsRoot!=customScaleFollowsRoot || prevCustomMask12!=customMask12 || prevCustomMask24!=customMask24);
                if (cfgChanged) {
                    for (int k=0;k<16;++k) latchedInit[k]=false;
                    prevRootNote=rootNote; prevScaleIndex=scaleIndex; prevEdo=qc.edo; prevTetSteps=tetSteps; prevTetPeriodOct=qc.periodOct; prevTuningMode=tuningMode; prevUseCustomScale=useCustomScale; prevCustomFollowsRoot=customScaleFollowsRoot; prevCustomMask12=customMask12; prevCustomMask24=customMask24;
                }
                float fs = yRel * (float)N / period; // fractional step
                // Initialize latch to nearest allowed
                if (!latchedInit[c]) { latchedStep[c] = nearestAllowedStep( (int)std::round(fs), fs, qc ); latchedInit[c]=true; }
                // Ensure still allowed (mask may have changed live)
                if (!isAllowedStep(latchedStep[c], qc)) { latchedStep[c] = nearestAllowedStep(latchedStep[c], fs, qc); }
                // Hysteresis clamp sizes
                float dV = period / (float)N;
                float stepCents = 1200.f * dV;
                float Hc = rack::clamp(stickinessCents, 0.f, 20.f);
                float maxAllowed = 0.4f * stepCents; if (Hc > maxAllowed) Hc = maxAllowed; // dynamic clamp
                float H_V = Hc / 1200.f; // in volts
                // Neighbor steps
                int upStep = nextAllowedStep(latchedStep[c], +1, qc);
                int dnStep = nextAllowedStep(latchedStep[c], -1, qc);
                float center = (latchedStep[c] / (float)N) * period;
                float vUp = (upStep / (float)N) * period;
                float vDn = (dnStep / (float)N) * period;
                // Midpoints
                float midUp = 0.5f*(center + vUp);
                float midDn = 0.5f*(center + vDn);
                float T_up = midUp + H_V;
                float T_down = midDn - H_V;
                if (yRel >= T_up && upStep != latchedStep[c]) latchedStep[c] = upStep;
                else if (yRel <= T_down && dnStep != latchedStep[c]) latchedStep[c] = dnStep;
                float yqRel = (latchedStep[c] / (float)N) * period; // snapped from latch
                // Determine rounding adjustment based on mode
                if (quantRoundMode != 1) { // modes other than Nearest may bias
                    // Convert to EDO step domain to apply directional rounding
                    // We reconstruct step value by multiplying volts by EDO (assuming 1V/oct standard mapping)
                    // snapEDO already returned nearest allowed note; we need raw pre-snap position for bias.
                    // Approximate raw semitone value relative to root: yRel * 12
                    float rawSemi = yRel * 12.f;
                    float snappedSemi = yqRel * 12.f;
                    float diff = rawSemi - snappedSemi;
                    if (quantRoundMode == 0) { // Directional Snap
                        float prev = prevYRel[c];
                        float dir = (yRel > prev + 1e-6f) ? 1.f : (yRel < prev - 1e-6f ? -1.f : 0.f);
                        if (dir > 0.f && diff > 0.f) {
                            // raw above snapped (snapped is below); move up one scale step if possible by adding step size
                            // We attempt a small positive nudge: ask quantizer for value slightly above current raw to force next
                            float nudged = quantizeToScale(yqRel + (1.f/12.f)*0.51f, 0, clipLimit, true);
                            if (nudged > yqRel + 1e-5f) yqRel = nudged;
                        } else if (dir < 0.f && diff < 0.f) {
                            float nudged = quantizeToScale(yqRel - (1.f/12.f)*0.51f, 0, clipLimit, true);
                            if (nudged < yqRel - 1e-5f) yqRel = nudged;
                        }
                    } else if (quantRoundMode == 2) { // Up
                        if (rawSemi > snappedSemi + 1e-5f) {
                            float nudged = quantizeToScale(yqRel + (1.f/12.f)*0.51f, 0, clipLimit, true);
                            if (nudged > yqRel + 1e-5f) yqRel = nudged;
                        }
                    } else if (quantRoundMode == 3) { // Down
                        if (rawSemi < snappedSemi - 1e-5f) {
                            float nudged = quantizeToScale(yqRel - (1.f/12.f)*0.51f, 0, clipLimit, true);
                            if (nudged < yqRel - 1e-5f) yqRel = nudged;
                        }
                    }
                }
                float yq = yqRel + rangeOffset;
                float t = clamp(quantStrength, 0.f, 1.f);
                y = yBase + (yq - yBase) * t;
                prevYRel[c] = yRel;
            } else {
                prevYRel[c] = yBase - rangeOffset;
            }
            // Post safety clip at ±10 V, respecting softClipOut choice
            if (softClipOut) y = hi::dsp::clip::soft(y, hconst::MAX_VOLT_CLAMP);
            else             y = clamp(y, -hconst::MAX_VOLT_CLAMP, hconst::MAX_VOLT_CLAMP);
            outVals[c] = y;
            lastOut[c] = y;

            hi::ui::led::setBipolar(lights[CH_LIGHT + 2*c + 0], lights[CH_LIGHT + 2*c + 1], y, args.sampleTime);
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
    }
};

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
    const float yInOutMM   = 111.743f; // IN/OUT jacks Y
    const float yTrigMM    = 121.743f; // Randomize trigger jack Y
    const float yBtnMM     = 106.000f; // Randomize button Y
    const float dxPortsMM  = 14.985f;  // Horizontal offset from center to IN/OUT jacks

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
    // Export an SVG overlay of current component layout (centers + crosshairs)
        menu->addChild(rack::createMenuItem("Export layout SVG (user folder)", "", [this]{
            using hi::ui::overlay::Marker; using hi::ui::overlay::Kind; using hi::ui::overlay::exportOverlay;
            const float pxPerMM = RACK_GRID_WIDTH / 5.08f;
            const float wMM = box.size.x / pxPerMM;
            const float hMM = RACK_GRID_HEIGHT / pxPerMM;
            const float cxMM = wMM * 0.5f;

            // Placement constants (mirrors those in constructor)
            const float yShapeMM   = 17.5f;   // Rise/Fall shape row
            const float yGlobalMM  = 27.8f;   // Global Slew/Offset row
            const float dxColShapesMM  = 17.5f;
            const float dxColGlobalsMM = 19.5f;
            const float dxToggleMM = 7.0f;
            const float yRow0MM    = 41.308f;
            const float rowDyMM    = 8.252f;
            const float ledDxMM    = 1.2f;
            const float knobDx1MM  = 8.0f;
            const float knobDx2MM  = 19.0f;
            const float yInOutMM   = 111.743f;
            const float yTrigMM    = 121.743f;
            const float yBtnMM     = 106.000f;
            const float dxPortsMM  = 14.985f;

            // Screw marker X from panel edges
            const float screwLeftXMM  = RACK_GRID_WIDTH / pxPerMM;
            const float screwRightXMM = (box.size.x - 2 * RACK_GRID_WIDTH) / pxPerMM;
            const float screwTopYMM   = 0.f;
            const float screwBotYMM   = (RACK_GRID_HEIGHT - RACK_GRID_WIDTH) / pxPerMM;

            // Marker sizes (mm)
            const float rKnob   = 3.2f;  // trimpot marker
            const float rJack   = 3.25f; // PJ301 center marker
            const float rLed    = 0.9f;
            const float rBtn    = 2.5f;
            const float rToggle = 1.6f;
            const float rScrew  = 1.6f;

            std::vector<Marker> marks;
            // Screws
            marks.push_back({Kind::Screw, screwLeftXMM,  screwTopYMM, rScrew});
            marks.push_back({Kind::Screw, screwRightXMM, screwTopYMM, rScrew});
            marks.push_back({Kind::Screw, screwLeftXMM,  screwBotYMM, rScrew});
            marks.push_back({Kind::Screw, screwRightXMM, screwBotYMM, rScrew});
            // Global shape (Rise/Fall)
            marks.push_back({Kind::Knob, cxMM - dxColShapesMM, yShapeMM, rKnob});
            marks.push_back({Kind::Knob, cxMM + dxColShapesMM, yShapeMM, rKnob});
            // Global Slew/Offset + toggles
            marks.push_back({Kind::Knob,   cxMM - dxColGlobalsMM, yGlobalMM, rKnob});
            marks.push_back({Kind::Knob,   cxMM + dxColGlobalsMM, yGlobalMM, rKnob});
            marks.push_back({Kind::Switch, cxMM - dxColGlobalsMM - dxToggleMM, yGlobalMM, rToggle});
            marks.push_back({Kind::Switch, cxMM + dxColGlobalsMM + dxToggleMM, yGlobalMM, rToggle});
            // 8 rows of channel controls
            for (int row = 0; row < 8; ++row) {
                float y = yRow0MM + row * rowDyMM;
                // Left: LED, Slew L, Slew R
                marks.push_back({Kind::Led,  cxMM - ledDxMM, y, rLed});
                marks.push_back({Kind::Knob, cxMM - ledDxMM - knobDx2MM, y, rKnob});
                marks.push_back({Kind::Knob, cxMM - ledDxMM - knobDx1MM, y, rKnob});
                // Right: Offset L, Offset R, LED
                marks.push_back({Kind::Knob, cxMM + ledDxMM + knobDx1MM, y, rKnob});
                marks.push_back({Kind::Knob, cxMM + ledDxMM + knobDx2MM, y, rKnob});
                marks.push_back({Kind::Led,  cxMM + ledDxMM, y, rLed});
            }
            // Ports and button
            marks.push_back({Kind::Jack,  cxMM - dxPortsMM, yInOutMM, rJack});
            marks.push_back({Kind::Jack,  cxMM,             yTrigMM,  rJack});
            marks.push_back({Kind::Button,cxMM,             yBtnMM,   rBtn });
            marks.push_back({Kind::Jack,  cxMM + dxPortsMM, yInOutMM, rJack});

            exportOverlay("PolyQuanta", wMM, hMM, marks);
        }));
    // Randomize
    menu->addChild(rack::createSubmenuItem("Randomize", "", [m](rack::ui::Menu* sm){
        // Scope toggles
        sm->addChild(rack::createSubmenuItem("Scope", "", [m](rack::ui::Menu* sm2){
            sm2->addChild(rack::createBoolPtrMenuItem("Slews", "", &m->randSlew));
            sm2->addChild(rack::createBoolPtrMenuItem("Offsets", "", &m->randOffset));
            sm2->addChild(rack::createBoolPtrMenuItem("Shapes", "", &m->randShapes));
        }));
        // Max percentage
        sm->addChild(rack::createSubmenuItem("Max", "", [m](rack::ui::Menu* sm2){
            for (int pct = 10; pct <= 100; pct += 10) {
                float v = pct / 100.f;
                sm2->addChild(rack::createCheckMenuItem(rack::string::f("%d%%", pct), "", [m,v]{ return std::fabs(m->randMaxPct - v) < 1e-4f; }, [m,v]{ m->randMaxPct = v; }));
            }
        }));
    }));

    // Quantization (musical) settings
    hi::ui::menu::addSection(menu, "Quantization");
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
            menu->addChild(rack::createMenuLabel(rack::string::f(
                "Status: %s %d, Root %s, Scale %s, Strength %d%%, Round %s, Stickiness %.1f¢ (max %.0f¢)",
                (m->tuningMode==0?"EDO":"TET"), steps, rootStr.c_str(), scaleStr.c_str(), pct,
                roundStr, m->stickinessCents, maxStick)));
        }
        // Tuning system selector
        menu->addChild(rack::createSubmenuItem("Tuning system", "", [m](rack::ui::Menu* sm){
            sm->addChild(rack::createCheckMenuItem("EDO (octave)", "", [m]{ return m->tuningMode==0; }, [m]{ m->tuningMode=0; }));
            sm->addChild(rack::createCheckMenuItem("TET (non-octave)", "", [m]{ return m->tuningMode==1; }, [m]{ m->tuningMode=1; }));
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
                    menuDest->addChild(rack::createCheckMenuItem(label, "", [m,n]{ return m->rootNote == n; }, [m,n]{ m->rootNote = n; }));
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
            }));
            sm->addChild(rack::createCheckMenuItem("Remember custom scale", "", [m]{ return m->rememberCustomScale; }, [m]{ m->rememberCustomScale = !m->rememberCustomScale; }));
            sm->addChild(rack::createCheckMenuItem("Custom scales follow root", "", [m]{ return m->customScaleFollowsRoot; }, [m]{ m->customScaleFollowsRoot = !m->customScaleFollowsRoot; }));
            if (m->tuningMode==0 && m->edo == 12 && !m->useCustomScale) {
                for (int i = 0; i < hi::music::NUM_SCALES12; ++i) {
                    sm->addChild(rack::createCheckMenuItem(hi::music::scales12()[i].name, "", [m,i]{ return m->scaleIndex == i; }, [m,i]{ m->scaleIndex = i; }));
                }
            } else if (m->tuningMode==0 && m->edo == 24 && !m->useCustomScale) {
                for (int i = 0; i < hi::music::NUM_SCALES24; ++i) {
                    sm->addChild(rack::createCheckMenuItem(hi::music::scales24()[i].name, "", [m,i]{ return m->scaleIndex == i; }, [m,i]{ m->scaleIndex = i; }));
                }
            } else {
                // Custom scale editing helpers
                sm->addChild(rack::createMenuItem("Select All Notes", "", [m]{
                    int N = std::max(1, (m->tuningMode==0 ? m->edo : m->tetSteps));
                    if (N == 12) m->customMask12 = 0xFFFu;
                    else if (N == 24) m->customMask24 = 0xFFFFFFu;
                    else m->customMaskGeneric.assign((size_t)N, 1);
                }));
                sm->addChild(rack::createMenuItem("Clear All Notes", "", [m]{
                    int N = std::max(1, (m->tuningMode==0 ? m->edo : m->tetSteps));
                    if (N == 12) m->customMask12 = 0u;
                    else if (N == 24) m->customMask24 = 0u;
                    else m->customMaskGeneric.assign((size_t)N, 0);
                }));
                sm->addChild(rack::createMenuItem("Invert Selection", "", [m]{
                    int N = std::max(1, (m->tuningMode==0 ? m->edo : m->tetSteps));
                    if (N == 12) m->customMask12 = (~m->customMask12) & 0xFFFu;
                    else if (N == 24) m->customMask24 = (~m->customMask24) & 0xFFFFFFu;
                    else {
                        if ((int)m->customMaskGeneric.size() != N) m->customMaskGeneric.assign((size_t)N, 0);
                        for (int i = 0; i < N; ++i) m->customMaskGeneric[(size_t)i] = m->customMaskGeneric[(size_t)i] ? 0 : 1;
                    }
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
    // Glide and strum controls
        hi::ui::menu::addBoolPtr(menu, "Pitch‑safe glide (1 V/oct)", &m->pitchSafeGlide);
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