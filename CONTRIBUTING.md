# Contributing

Welcome! This repo contains **VCV Rack 2** modules by **HugginsIndustries**. The goal: keep setup simple, builds reproducible, and contributions easy.

## TL;DR (Windows + MSYS2)

~~~bash
# 1) Install MSYS2, open a MINGW64 shell, then:
pacman -S --needed base-devel make zip unzip mingw-w64-x86_64-toolchain

# 2) Set environment vars (PowerShell, one-time)
setx RACK_SDK "C:\dev\Rack-SDK"
setx RACK_USER_DIR "C:\Users\YOURNAME\AppData\Local\Rack2"
setx RACK_EXE "C:\Program Files\VCV\Rack2Free\Rack.exe"

# 3) Clone and build (in MSYS2 MINGW64 shell)
git clone https://github.com/HugginsIndustries/vcv-rack-modules.git
cd vcv-rack-modules
make -j"$(nproc)"
make install RACK_USER_DIR="$RACK_USER_DIR"

# 4) Run Rack
"$RACK_EXE"
~~~

VS Code users: open the folder and hit **Ctrl+Shift+B**. The default task is **Clean → Build & Install**.

---

## Prerequisites

- **VCV Rack 2 SDK** (download and unzip somewhere like `C:\dev\Rack-SDK`)
- **MSYS2** with MINGW64 environment (C/C++ toolchain)
  ~~~bash
  pacman -S --needed base-devel make zip unzip mingw-w64-x86_64-toolchain
  ~~~
- **Git** (included in MSYS2; you can use VS Code’s Git too)
- (Optional) **VS Code** with:
  - C/C++ (ms-vscode.cpptools)
  - Makefile Tools (ms-vscode.makefile-tools)
  - GitLens (eamodio.gitlens)
  - DotENV (mikestead.dotenv)

---

## Environment variables

The build and VS Code tasks read these variables:

- `RACK_SDK` – path to the Rack SDK (e.g., `C:\dev\Rack-SDK`)
- `RACK_USER_DIR` – Rack’s user directory (e.g., `C:\Users\YOURNAME\AppData\Local\Rack2`)
- `RACK_EXE` – Rack executable (e.g., `C:\Program Files\VCV\Rack2Free\Rack.exe`)

### Option A: Set system-wide (PowerShell, one-time)

~~~powershell
setx RACK_SDK "C:\dev\Rack-SDK"
setx RACK_USER_DIR "C:\Users\YOURNAME\AppData\Local\Rack2"
setx RACK_EXE "C:\Program Files\VCV\Rack2Free\Rack.exe"
~~~

Restart VS Code afterwards so it picks up the new environment.

### Option B: Set in MSYS2 shell profile

~~~bash
# ~/.bashrc (MSYS2)
export RACK_SDK=/c/dev/Rack-SDK
export RACK_USER_DIR=/c/Users/YOURNAME/AppData/Local/Rack2
export RACK_EXE="/c/Program Files/VCV/Rack2Free/Rack.exe"
~~~

### `.env.example`

The repo includes an example for documentation:

RACK_SDK=/c/dev/Rack-SDK
RACK_USER_DIR=/c/Users/YOURNAME/AppData/Local/Rack2
RACK_EXE=/c/Program Files/VCV/Rack2Free/Rack.exe

Create your own `.env` if you like—but it’s **ignored** by Git.

---

## Building & Installing

### MSYS2 (command line)

~~~bash
make -j"$(nproc)"
make install RACK_USER_DIR="$RACK_USER_DIR"
~~~

Artifacts go to `build/`. The installed plugin lands in `$RACK_USER_DIR/plugins`.

### VS Code tasks (recommended)

- **Clean → Build & Install** (default build task)
- **Build → Install → Run** (also launches Rack)
- **Generate module.cpp from SVG** (calls `helper.py`)

All tasks are defined in `.vscode/tasks.json` and are **path-agnostic** (they use env vars).

---

## Running Rack

- MSYS2 shell:
  ~~~bash
  "$RACK_EXE"
  ~~~
- VS Code task: **Run VCV Rack**

---

## Coding guidelines

- **C++17** (use `<algorithm>`, `<array>`, `<optional>`, etc.).
- Stay modular; avoid global state in DSP paths.
- Keep parameters labeled via `configParam()` / `configInput()` / `configOutput()` for hover hints.
- Avoid hard-coded absolute paths—use env vars or Rack API helpers.
- Commit messages: short, imperative: PolySlewOffset16: normalize +1 shape; strengthen -1 log curve

---

## Git workflow

- **Small, focused commits**; prefer `git add -p` to stage meaningful hunks.
- **Branching**:
- `main` = stable
- feature branches: `feat/<name>`; fixes: `fix/<name>`
- **Pull Requests**:
- Describe the change and how to test it.
- Include screenshots/gifs for UI changes when possible.

---

## Versioning & Releases

1. Bump `version` in `plugin.json` to `2.x.y`.
2. Commit: `git commit -m "Bump to 2.x.y"`.
3. Tag: `git tag v2.x.y`.
4. Push: `git push && git push --tags`.
5. Create a GitHub Release and attach the ZIP from `make dist` or the CI artifact.

---

## Continuous Integration (GitHub Actions)

- Workflow lives at `.github/workflows/build.yml`.
- It builds on Windows using MSYS2 and uploads the `dist/*.zip`.
- Configure repo variable `RACK_SDK_URL`:
- GitHub → **Settings → Secrets and variables → Actions → Variables**
- Name: `RACK_SDK_URL`
- Value: Windows Rack SDK download URL for your target SDK version.

To run on demand: **Actions → build → Run workflow**.

---

## Troubleshooting

- **“make: *** No rule to make target …”**  
Check `RACK_SDK` is set and points to a valid SDK folder. Reopen VS Code after changing env vars.

- **Weird compile errors / IntelliSense red squiggles**  
Run **“Clean → Build & Install”** to force a full rebuild. Ensure Makefile Tools is active (status bar shows your makefile).

- **Rack can’t find the plugin**  
Confirm `plugin.json.version` starts with `2.` and you installed to the correct `RACK_USER_DIR`. Restart Rack after installing.

- **LF/CRLF churn**  
The repo normalizes line endings via `.gitattributes`. If you changed this file, run:
~~~bash
git add --renormalize .
git commit -m "Normalize line endings"
~~~

---

## How to contribute

1. Fork the repo (or create a feature branch if you’re a collaborator).
2. Make changes with small, reviewable commits.
3. Build & test locally (try a few patches in Rack).
4. Open a PR with a clear description, test steps, and screenshots if UI changed.

Thanks for helping make weird sounds weirder, faster, and with fewer segfaults.