#pragma once
/*
 * MenuHelpers.hpp — Header‑only relocation of small menu convenience
 * functions from PolyQuanta.cpp. These wrap Rack UI calls to insert
 * labeled sections and boolean pointer menu items while preserving
 * enable/disable state logic. Implementation is verbatim.
 */
#include "../../plugin.hpp"
namespace hi { namespace ui { namespace menu {
    using Menu = rack::ui::Menu; using MenuSeparator = rack::ui::MenuSeparator; using MenuItem = rack::ui::MenuItem;
    static inline void addSection(Menu* m, const char* label) { m->addChild(new MenuSeparator); m->addChild(rack::createMenuLabel(label)); }
    static inline MenuItem* addBoolPtr(Menu* m, const char* title, bool* ptr) { auto* item = rack::createBoolPtrMenuItem(title, "", ptr); m->addChild(item); return item; }
    static inline MenuItem* addBoolPtr(Menu* m, const char* title, bool* ptr, const std::function<bool()>& enabled) { auto* item = addBoolPtr(m, title, ptr); if (enabled) item->disabled = !enabled(); return item; }
}}} // namespace hi::ui::menu
//───────────────────────────────────────────────────────────────────────────────
// Helper Quantity for menu sliders bound directly to a float*
// Put this before any code that uses `FloatMenuQuantity`.
struct FloatMenuQuantity : rack::Quantity {
    float* ref = nullptr;
    float minV = 0.f, maxV = 1.f, defV = 0.f;
    std::string label, unit;
    int prec = 2;

    FloatMenuQuantity(float* r, float mn, float mx, float df,
                    std::string lab, std::string un = "", int pr = 2)
        : ref(r), minV(mn), maxV(mx), defV(df),
        label(std::move(lab)), unit(std::move(un)), prec(pr) {}
    void  setValue(float v) override {
        if (v < minV) v = minV;
        if (v > maxV) v = maxV;
        *ref = v;
    }
    float getValue() override        { return *ref; }
    float getDefaultValue() override { return defV; }
    float getMinValue() override     { return minV; }
    float getMaxValue() override     { return maxV; }
    std::string getLabel() override  { return label; }
    std::string getUnit() override   { return unit; }
    int   getDisplayPrecision() override { return prec; }
};
//───────────────────────────────────────────────────────────────────────────────