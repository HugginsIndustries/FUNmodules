#include "EdoTetPresets.hpp"
#include <algorithm> // std::find
#include <cmath>      // std::log2
/*
 * EdoTetPresets.cpp — Definitions for curated EDO and TET preset groups.
 * Moved verbatim from PolyQuanta.cpp (Phase 2D). Do not alter ordering or
 * string contents; they are relied upon by UI and persistence.
 */
namespace hi { namespace music { namespace edo {
// Curated EDO presets grouped by usefulness
static inline const std::vector<int>& _near12_impl() { static const std::vector<int> v{10,14,16}; return v; }
static inline const std::vector<int>& _diatonicFavs_impl() { static const std::vector<int> v{19,31,22,17,13}; return v; }
static inline const std::vector<int>& _microFamilies_impl() { static const std::vector<int> v{18,36,48,72}; return v; }
static inline const std::vector<int>& _jiAccurate_impl() { static const std::vector<int> v{41,53}; return v; }
static inline const std::vector<int>& _extras_impl() { static const std::vector<int> v{11,20,26,34}; return v; }

const std::vector<int>& near12() { return _near12_impl(); }
const std::vector<int>& diatonicFavs() { return _diatonicFavs_impl(); }
const std::vector<int>& microFamilies() { return _microFamilies_impl(); }
const std::vector<int>& jiAccurate() { return _jiAccurate_impl(); }
const std::vector<int>& extras() { return _extras_impl(); }
std::vector<int> allRecommended() {
    std::vector<int> all; all.reserve(32);
    auto add=[&](const std::vector<int>& g){ all.insert(all.end(), g.begin(), g.end()); };
    add(near12()); add(diatonicFavs()); add(microFamilies()); add(jiAccurate()); add(extras());
    std::vector<int> out; out.reserve(all.size());
    for (int n : all) { if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n); }
    return out;
}
}}} // namespace hi::music::edo

namespace hi { namespace music { namespace tets {
static inline const std::vector<Tet>& _carlos_impl() {
    static const std::vector<Tet> v{
        {"Carlos Alpha", 9,  std::log2(3.f/2.f)},
        {"Carlos Beta",  11, std::log2(3.f/2.f)},
        {"Carlos Gamma", 20, std::log2(3.f/2.f)}
    }; return v; }
const std::vector<Tet>& carlos() { return _carlos_impl(); }
}}} // namespace hi::music::tets
