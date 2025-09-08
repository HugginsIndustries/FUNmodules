#pragma once
/*
 * Quantities.hpp — Relocated ParamQuantity subclasses from PolyQuanta.cpp.
 * These UI helpers implement custom display/parse behavior for time, shape,
 * and voltage/semitone/cents presentation. Logic is verbatim to guarantee
 * identical interaction semantics. Added commentary documents intent.
 */
#include "../../plugin.hpp" // project plugin header (provides <rack.hpp> & using namespace rack)
#include "../PolyQuantaCore.hpp" // for hi::consts constants
#include <cmath>
#include <cctype>
#include <string>
namespace hi { namespace ui {
    // Exponential time taper quantity. Raw knob range [0,1] maps to seconds
    // logarithmically between hi::consts::MIN_SEC and hi::consts::MAX_SEC.
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
        std::string getDisplayValueString() override;
        void setDisplayValueString(std::string s) override;
    };

    // Shape quantity: interprets value in [-1,1] and produces human‑readable
    // labels for logarithmic / linear / exponential curves (verbatim logic).
    struct ShapeQuantity : rack::engine::ParamQuantity {
        std::string getDisplayValueString() override;
    };

    // Voltage / semitone / cent aware quantity. Uses module pointers to decide
    // how to format and parse values. Quantize mode 1 = semitones (EDO or TET),
    // mode 2 = cents (1/1200 V). Otherwise show volts. Behavior identical to
    // original implementation.
    struct SemitoneVoltQuantity : rack::engine::ParamQuantity {
        const int* quantizeOffsetModePtr = nullptr; // 1=semitones, 2=cents
        const int* edoPtr = nullptr;
        std::string getDisplayValueString() override;
        void setDisplayValueString(std::string s) override;
    };
}} // namespace hi::ui
