# AGENTS.md — FUNmodules (VCV Rack 2)

This document is the single source of truth for how AI agents (Codex) should operate in this repo.

---

## 1) Repo Conventions for Agents

* Keep diffs **minimal**; preserve existing comments/formatting.
* Add brief comments to all changed code explaining the functionality.
* Don’t change param ranges, strings, menu text, or DSP outside the task scope.
* Quantizer invariants must hold:

  * Root-relative scale masks (no chromatic leakage)
  * Center-anchored Schmitt hysteresis (stable step latching)
  * Directional Snap from the **latched** step (one allowed degree per step)

After any code edit, always:

1. `make quick`
2. `./build/core_tests`
3. Summarize change + verification results
4. Add an entry to the changelog under the Unreleased section (CHANGELOG.md).

---

## 2) TL;DR bootstrap (Windows + MSYS2, Local runner)

Codex: run this **at the start of a session** to ensure a clean env:

```powershell
# Ensure correct SDK env for Rack 2 (forward slashes!)
$env:RACK_SDK = 'C:/dev/Rack-SDK'
$env:RACK_DIR =  $env:RACK_SDK

# Use MSYS2 make explicitly (fail fast if missing)
& 'C:/msys64/usr/bin/make.exe' -v | Out-Null
```

**Never** use a container for builds in this repo. Use **Local** runner only.

---

## 3) Environment & Tools

* **OS:** Windows 10/11
* **Shells:** PowerShell 7+ (headless) and MSYS2 `bash.exe` at `C:\msys64\usr\bin\bash.exe`
* **Toolchain (pin paths):**

  * `make` → `C:\msys64\usr\bin\make.exe`
  * `sh`/`bash` → `C:\msys64\usr\bin\{sh,bash}.exe`
* **Required env vars (forward slashes):**

  * `RACK_SDK = C:/dev/Rack-SDK`
  * `RACK_DIR = C:/dev/Rack-SDK`
* **Quick checks:**

  ```powershell
  $env:RACK_SDK; $env:RACK_DIR
  "C:\msys64\usr\bin\bash.exe" -lc "test -f C:/dev/Rack-SDK/plugin.mk && echo OK || echo MISSING"
  ```

---

## 4) Agent Run Mode & Approvals

* **Access:** Full Access allowed; keep writes inside repo.

---

## 5) Build & Test

**Fast dev build**

```powershell
& 'C:/msys64/usr/bin/make.exe' -j $env:NUMBER_OF_PROCESSORS quick
```

**Full build**

```powershell
& 'C:/msys64/usr/bin/make.exe' -j $env:NUMBER_OF_PROCESSORS all
```

**Core tests** (two routes):

1. **Standalone core test binary** (preferred for headless):

```powershell
"C:\msys64\usr\bin\bash.exe" -lc "make -C tests"
./build/core_tests
```

2. **Rack-aware target** (requires Rack-SDK):

```powershell
& 'C:/msys64/usr/bin/make.exe' core_tests
./build/core_tests
```

**Success:** output includes `All core tests passed.`