# HugginsIndustries VCV Rack Modules

Experimental modules for VCV Rack 2.

## Build (Windows / MSYS2)
```bash
# in MSYS2 MINGW64
export RACK_DIR=<RACK-SDK folder>
export RACK_SDK=<RACK-SDK folder>
make -j$(nproc)
make install RACK_USER_DIR="/c/Users/<USERNAME>/AppData/Local/Rack2"