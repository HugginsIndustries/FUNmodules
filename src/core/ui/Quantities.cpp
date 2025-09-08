#include "Quantities.hpp"
#include <algorithm>
#include <cmath>
#include <rack.hpp> // ensure rack::string & clamp available
using namespace rack;
namespace hi { namespace ui {
    std::string ExpTimeQuantity::getDisplayValueString() {
        float sec = knobToSec(getValue());
        if (sec < 1.f) return rack::string::f("%.1f ms", sec * 1000.f);
        if (sec < 10.f) return rack::string::f("%.2f s", sec);
        return rack::string::f("%.1f s", sec);
    }
    void ExpTimeQuantity::setDisplayValueString(std::string s) {
        std::string t = s; for (auto& c : t) c = (char)std::tolower((unsigned char)c);
        while (!t.empty() && std::isspace((unsigned char)t.front())) t.erase(t.begin());
        while (!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
        bool isMs = (t.find('m') != std::string::npos);
        float v = knobToSec(getValue());
        try { v = std::stof(t); } catch (...) {}
        float sec = isMs ? v / 1000.f : v; setValue(secToKnob(sec));
    }
    std::string ShapeQuantity::getDisplayValueString() {
        float v = getValue(); float a = std::fabs(v);
        if (a < 0.02f) return std::string("Linear");
        int pct = (int)std::round(a * 100.f);
        if (v > 0.f) return rack::string::f("Exp %d%%", pct);
        return rack::string::f("Log %d%%", pct);
    }
    std::string SemitoneVoltQuantity::getDisplayValueString() {
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
    void SemitoneVoltQuantity::setDisplayValueString(std::string s) {
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
}} // namespace hi::ui
