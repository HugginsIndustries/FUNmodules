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
