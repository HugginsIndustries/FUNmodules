# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
### Added
- (placeholder)

### Changed
- (placeholder)

### Fixed
- (placeholder)

### Removed
- (placeholder)

### Deprecated
- (placeholder)

### Security
- (placeholder)

### Docs
- (placeholder)

## [v2.0.0] - 2025-09-08
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

### Deprecated
- Legacy implicit semitone glide key (`pitchSafe`) retained only for JSON backward compatibility (no longer written).

### Security
- No security-related changes in this release.

### Docs
- Introduced this CHANGELOG following Keep a Changelog format.

<!--
Link reference section: add real compare links once git tags exist.
If tags exist, uncomment and adapt examples below:
[Unreleased]: https://github.com/HugginsIndustries/vcv-rack-modules/compare/v2.0.0...HEAD
[v2.0.0]: https://github.com/HugginsIndustries/vcv-rack-modules/releases/tag/v2.0.0
-->
