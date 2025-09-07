#pragma once
/*
 * EdoTetPresets.hpp — Single source of truth for curated EDO (equal divisions
 * of the octave) and selected TET (alternative period) preset groups used by
 * PolyQuanta. ORDER IS STABLE and MUST NOT CHANGE to preserve UI + JSON/back
 * compatibility. New presets, if ever added, must be appended only.
 *
 * The bodies of these functions / structs were moved verbatim from
 * PolyQuanta.cpp (Phase 2D extraction). Logic, data, strings, and ordering are
 * unchanged.
 */
#include <vector>

namespace hi { namespace music { namespace edo {
// Curated EDO presets grouped by usefulness (declarations)
const std::vector<int>& near12();
const std::vector<int>& diatonicFavs();
const std::vector<int>& microFamilies();
const std::vector<int>& jiAccurate();
const std::vector<int>& extras();
std::vector<int> allRecommended();
}}} // namespace hi::music::edo

namespace hi { namespace music { namespace tets {
struct Tet { const char* name; int steps; float periodOct; };
const std::vector<Tet>& carlos();
}}} // namespace hi::music::tets
