/**
 * @file plugin.hpp
 * @author Christian Huggins (hugginsindustry@gmail.com)
 * @brief Plugin for VCV Rack 2 :)
 * @version 2.0.2
 * @date 2025-09-19
 * @license GPL-3.0-or-later
 * @sdk VCV Rack 2.6.4
 * @ingroup FUNmodules
 * 
 * @copyright
 * Copyright (c) 2025 Huggins Industries
 * 
 * @details
 * This is the plugin.hpp file for the FUNmodules collection.
 * It declares the modules for VCV Rack 2.
 **/

#pragma once
#include <rack.hpp>


using namespace rack;

// Declare the Plugin, defined in plugin.cpp
extern Plugin* pluginInstance;

// Declare each Model, defined in each module source file
// extern Model* modelMyModule;
extern Model* modelPolyQuanta;
//extern Model* modelPolyQuantaXL; // future expansion