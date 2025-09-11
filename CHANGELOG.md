# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
### Added
- **Per-channel slew enable/disable toggles**: Each channel's slew knob context menu now includes "Slew processing enabled" toggle to bypass slew processing for that channel, even when Global slew add > 0 ms.
- **Batch slew enable/disable actions**: Global Slew knob context menu includes "Batch: Slew enable/disable" submenu with ALL/EVEN/ODD ON/OFF actions for quick channel group management.
- **`PROMPT.md`**: meta prompt for crafting prompts for the Cascade AI code platform in Windsurf. Includes project context and instructions for consistent prompt crafting. (WIP)
- **`scripts/win_msys_build.cmd`**: batch script for building the project on Windows using MSYS2/MINGW64.
- **`scripts/win_msys_core_tests.cmd`**: batch script for running the core unit tests on Windows using MSYS2/MINGW64.

### Changed
- _None._

### Fixed
- _None._

### Removed
- **Sync glides across channels** feature: eliminated cross-channel glide synchronization to simplify DSP and reduce complexity. Each channel now handles glides independently (as if sync was always disabled).
- **Glide normalization** feature: removed glide normalization enable toggle, mode selection (Volts-linear, Cent-linear, Step-safe), related DSP branches, JSON serialization, and UI menu entries. Module now uses equal-time glide behavior for all cases (preserves existing default behavior).

### Deprecated
- _None._

### Security
- _None._

### Docs
- _None._


## [v2.0.1] — 2025-09-09 ([diff][v2.0.1-diff])
### Added
- Headless core tests for quantizer stability:
  - `PeakHold_NoChatter`
  - `DirSnap_NoSkip_AfterFlip`
  - `DirSnap_VeryLowFreq_PeakHold`
- Directional Snap internal state for robust direction detection (`lastFs[]`, `lastDir[]`).

### Changed
- Direction hysteresis for Directional Snap widened to `Hd = max(0.75·Hs, 0.02 steps)`.
- Directional target selection (Directional Snap only) now uses `nextAllowedStep(latched, ±1)` instead of nearest-to-`fs`.  
  *No UI/param changes; Nearest/Up/Down paths unchanged; negligible CPU impact.*

### Fixed
- **Scale leakage eliminated**: root-relative mask enforcement (honors `customFollowsRoot`) prevents chromatic notes when a scale is active.
- **Boundary flicker removed** under slow LFO: center-anchored Schmitt hysteresis around the latched step stabilizes edges.
- Directional Snap crest chatter substantially reduced; a rare <~0.1 Hz “peak jump” may still appear (tracked in a follow-up issue).

### Removed
- _None._

### Deprecated
- _None._

### Security
- _None._

### Docs
- Inline comments clarify the **candidate → target → latch** flow and mask semantics in `PolyQuanta.cpp` / `PolyQuantaCore.cpp`.
- Refresh **README.md**: new nested Contents and expanded **Modules → PolyQuanta** section (highlights, typical uses). Also clarifies quick-start, Windsurf/VS Code tasks, and terminal equivalents.
- Update **CONTRIBUTING.md**: streamlined **Windows/MSYS2** setup, `RACK_DIR`/PATH env notes, “Build & Install” mapping to `make` targets, and troubleshooting. Adds editor setup: **clangd** config (`settings.json`), `compile_commands.json` generation, and recommended extensions.
- Housekeeping: revise **tasks.json** (problemMatcher fix and terminal compatibility), recommend extensions in **.vscode/extensions.json**, and tidy **.gitignore** / **.gitattributes** for build artifacts and binary assets.


## [v2.0.0] — 2025-09-08
### Added
- Glide normalization master enable with modes: Volts-linear, Cent-linear (1 V/oct), Step-safe (EDO/TET period).
- Signal chain position switch (Pre quantize → Slew vs Post Slew → Quantize) for pitch stability during long glides.
- Headless core unit tests (glide normalization ratios, quantizer boundaries, mask parity, rounding & hysteresis).

### Changed
- Core refactor scaffolding: extracted `PanelExport`, `ScaleDefs`, `EdoTetPresets`, and `PolyQuantaCore` into dedicated core/ files for separation of UI and DSP concerns.

### Fixed
- Generic EDO custom mask handling (byte-per-step parity now accurate and matches API).
- (If applicable) Clock multiplier/divider scheduling robustness (edge timing stabilization) — include precise note when commit present.

### Removed
- Legacy standalone pitch-safe toggle in favor of unified normalization enable + mode submenu.
- Offset module (was just a test module)

### Deprecated
- Legacy implicit semitone glide key (`pitchSafe`) retained only for JSON backward compatibility (no longer written).

### Security
- _None._

### Docs
- Introduced this CHANGELOG following Keep a Changelog format. 

[Unreleased]: https://github.com/HugginsIndustries/FUNmodules/compare/v2.0.1...HEAD
[v2.0.1]:     https://github.com/HugginsIndustries/FUNmodules/releases/tag/v2.0.1
[v2.0.1-diff]:https://github.com/HugginsIndustries/FUNmodules/compare/v2.0.0...v2.0.1
[v2.0.0]:     https://github.com/HugginsIndustries/FUNmodules/releases/tag/v2.0.0