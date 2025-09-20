/**
 * @file plugin.cpp
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
 * This is the plugin.cpp file for the FUNmodules collection.
 * It registers the modules with VCV Rack 2.
 **/

#include "plugin.hpp"


Plugin* pluginInstance;


void init(Plugin* p) {
	pluginInstance = p;

	// Add modules here
	// p->addModel(modelMyModule);
	p->addModel(modelPolyQuanta);
	//p->addModel(modelPolyQuantaXL); // future expansion
	// Any other plugin initialization may go here.
	// As an alternative, consider lazy-loading assets and lookup tables when your module is created to reduce startup times of Rack.
}
