# HugginsIndustries FUNmodules for VCV Rack

Experimental modules for VCV Rack 2.

Have FUN :)

## Changelog
See [CHANGELOG.md](CHANGELOG.md) for release history and unreleased changes.

## Build (Windows / MSYS2)
```bash
# in MSYS2 MINGW64
export RACK_DIR=<RACK-SDK folder>
export RACK_SDK=<RACK-SDK folder>
make -j$(nproc)
make install RACK_USER_DIR="/c/Users/<USERNAME>/AppData/Local/Rack2"