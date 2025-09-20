/**
 * @file TemplateModule.cpp
 * @author Your Name (your.email@example.com)
 * @brief TemplateModule — A blank VCV Rack 2 module template
 * @version 1.0.0
 * @date 2025-01-XX
 * @license GPL-3.0-or-later
 * @sdk VCV Rack 2.6.4
 * @ingroup FUNmodules
 * 
 * @copyright
 * Copyright (c) 2025 Your Name
 * 
 * @details
 * # Purpose & Design
 * This is a blank template for creating new VCV Rack 2 modules. It includes:
 * - Complete module structure with parameter, input, output, and light enums
 * - Basic DSP processing framework
 * - Widget layout system with proper coordinate management
 * - Context menu system for advanced controls
 * - JSON state persistence
 * - Proper VCV Rack 2 integration
 *
 * ## Usage
 * 1. Copy this template to your new module directory
 * 2. Rename TemplateModule to your module name
 * 3. Update the file header with your information
 * 4. Modify parameters, inputs, outputs, and lights as needed
 * 5. Implement your DSP logic in the process() method
 * 6. Update the widget layout in the constructor
 * 7. Add your module to plugin.cpp and plugin.hpp
 *
 * ## Signal Flow
 * `Input → [Your DSP Processing] → Output`
 *
 * # Ports
 * **Inputs**
 * - `IN_INPUT` — Main input signal
 *
 * **Outputs**
 * - `OUT_OUTPUT` — Main output signal
 *
 * **Params**
 * - `KNOB_PARAM` — Main control knob
 * - `BUTTON_PARAM` — Momentary button
 *
 * **Lights**
 * - `LED_LIGHT` — Status indicator LED
 */

#include "TemplateModule.hpp"

using namespace rack;

// -----------------------------------------------------------------------------
// PARAMETER QUANTITY CLASSES - Custom parameter behaviors
// -----------------------------------------------------------------------------

/**
 * @brief Custom parameter quantity for specialized knob behaviors
 * @note Add custom parameter quantities here as needed for your module
 */
struct TemplateQuantity : ParamQuantity {
    // Override methods here for custom parameter behavior
    // Example: custom display formatting, snap modes, etc.
};

// -----------------------------------------------------------------------------
// MODULE CLASS - Main DSP processing and state management
// -----------------------------------------------------------------------------

struct TemplateModule : Module {
    // Parameter enumeration - defines all knobs, buttons, and controls
    enum ParamId {
        // Main control parameters
        KNOB_PARAM,             // Primary control knob
        BUTTON_PARAM,           // Momentary button
        
        PARAMS_LEN              // Total count of parameters
    };
    
    // Input enumeration - defines all input jacks
    enum InputId { 
        IN_INPUT,               // Main input signal
        INPUTS_LEN              // Total count of inputs
    };
    
    // Output enumeration - defines all output jacks
    enum OutputId { 
        OUT_OUTPUT,             // Main output signal
        OUTPUTS_LEN             // Total count of outputs
    };

    // Light enumeration - defines all LED indicators
    enum LightId { 
        LED_LIGHT,              // Status indicator LED
        LIGHTS_LEN              // Total count of lights
    };

    // ═══════════════════════════════════════════════════════════════════════════
    // DSP STATE VARIABLES - Runtime processing state for audio computation
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Add your DSP state variables here
    // Examples:
    // dsp::SlewLimiter slewLimiter;
    // float lastValue = 0.f;
    // dsp::SchmittTrigger trigger;
    
    // ═══════════════════════════════════════════════════════════════════════════
    // MODULE CONFIGURATION OPTIONS - User-configurable behavior settings
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Add your module configuration options here
    // Examples:
    // bool someOption = false;
    // int someMode = 0;
    // float someValue = 1.f;

    // ═══════════════════════════════════════════════════════════════════════════
    // CONSTRUCTOR - Initialize module and configure parameters
    // ═══════════════════════════════════════════════════════════════════════════
    
    TemplateModule() {
        // Configure VCV Rack module with total counts of each component type
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Configure parameters
        configParam(KNOB_PARAM, 0.f, 1.f, 0.5f, "Main knob", "");
        configButton(BUTTON_PARAM, "Button");
        
        // Configure inputs
        configInput(IN_INPUT, "Input");
        
        // Configure outputs  
        configOutput(OUT_OUTPUT, "Output");
        
        // Configure lights
        configLight(LED_LIGHT, "Status LED");
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // JSON STATE PERSISTENCE - Save and restore module state
    // ═══════════════════════════════════════════════════════════════════════════
    
    /**
     * @brief Save module state to JSON for patch persistence
     * @param root JSON object to save state to
     */
    json_t* dataToJson() override {
        json_t* root = json_object();
        
        // Save your module state here
        // Examples:
        // json_object_set_new(root, "someOption", json_boolean(someOption));
        // json_object_set_new(root, "someMode", json_integer(someMode));
        // json_object_set_new(root, "someValue", json_real(someValue));
        
        return root;
    }

    /**
     * @brief Load module state from JSON for patch restoration
     * @param root JSON object containing saved state
     */
    void dataFromJson(json_t* root) override {
        // Load your module state here
        // Examples:
        // json_t* someOptionJ = json_object_get(root, "someOption");
        // if (someOptionJ) someOption = json_boolean_value(someOptionJ);
        // 
        // json_t* someModeJ = json_object_get(root, "someMode");
        // if (someModeJ) someMode = json_integer_value(someModeJ);
        // 
        // json_t* someValueJ = json_object_get(root, "someValue");
        // if (someValueJ) someValue = json_real_value(someValueJ);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // DSP PROCESSING - Main audio processing function
    // ═══════════════════════════════════════════════════════════════════════════
    
    /**
     * @brief Main DSP processing function - called every audio sample
     * @param args Processing arguments containing sample rate, etc.
     */
    void process(const ProcessArgs& args) override {
        // Get input signal
        float input = inputs[IN_INPUT].getVoltage();
        
        // Get parameter values
        float knobValue = params[KNOB_PARAM].getValue();
        bool buttonPressed = params[BUTTON_PARAM].getValue() > 0.f;
        
        // Your DSP processing goes here
        float output = input * knobValue;  // Simple example: multiply by knob value
        
        // Apply button effect if pressed
        if (buttonPressed) {
            output *= 2.f;  // Example: double the signal when button is pressed
        }
        
        // Set output signal
        outputs[OUT_OUTPUT].setVoltage(output);
        
        // Update lights
        lights[LED_LIGHT].setBrightness(buttonPressed ? 1.f : 0.f);
    }
};

// -----------------------------------------------------------------------------
// WIDGET CLASS - User interface layout and interaction
// -----------------------------------------------------------------------------

struct TemplateModuleWidget : ModuleWidget {
    /**
     * @brief Constructor - builds the complete user interface layout
     * @param module Pointer to the TemplateModule instance (can be null for library browser)
     */
    TemplateModuleWidget(TemplateModule* module) {
        setModule(module);
        
        // Load panel graphics (replace with your panel SVG)
        setPanel(createPanel(asset::plugin(pluginInstance, "res/TemplateModule.svg")));

        // ───────────────────────────────────────────────────────────────────────────────────────
        // Panel Hardware - Corner Screws
        // ───────────────────────────────────────────────────────────────────────────────────────
        addChild(createWidget<ScrewBlack>(Vec(0, 0)));                                      // Top-left
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, 0)));           // Top-right
        addChild(createWidget<ScrewBlack>(Vec(0, box.size.y - RACK_GRID_WIDTH)));           // Bottom-left
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, box.size.y - RACK_GRID_WIDTH))); // Bottom-right

        // ───────────────────────────────────────────────────────────────────────────────────────
        // UI Layout Coordinate System and Placement Constants
        // ───────────────────────────────────────────────────────────────────────────────────────
        // All component positions are defined in millimeters for consistency with panel SVGs.
        // The coordinate system is centered horizontally (cxMM) and scales automatically with
        // panel width changes, requiring no code edits when changing HP size.
        // ───────────────────────────────────────────────────────────────────────────────────────
        
        // Coordinate system conversion (1 HP = 5.08 mm in Eurorack standard)
        const float pxPerMM = RACK_GRID_WIDTH / 5.08f;                          // Pixels per millimeter conversion
        const float cxMM = (box.size.x * 0.5f) / pxPerMM;                       // Panel center X in millimeters
        
        // Layout constants (adjust these for your module)
        const float yKnobMM = 30.0f;                                            // Knob Y position
        const float yButtonMM = 50.0f;                                          // Button Y position
        const float yInputMM = 80.0f;                                           // Input jack Y position
        const float yOutputMM = 100.0f;                                         // Output jack Y position
        const float yLEDMM = 60.0f;                                             // LED Y position
        const float dxLEDMM = 10.0f;                                            // LED X offset from center

        // ───────────────────────────────────────────────────────────────────────────────────────
        // Control Elements - Knobs, Buttons, Switches
        // ───────────────────────────────────────────────────────────────────────────────────────
        
        // Main control knob
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(cxMM, yKnobMM)), module, TemplateModule::KNOB_PARAM));
        
        // Button
        addParam(createParamCentered<LEDButton>(mm2px(Vec(cxMM, yButtonMM)), module, TemplateModule::BUTTON_PARAM));

        // ───────────────────────────────────────────────────────────────────────────────────────
        // Input/Output Jacks
        // ───────────────────────────────────────────────────────────────────────────────────────
        
        // Input jack
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cxMM, yInputMM)), module, TemplateModule::IN_INPUT));
        
        // Output jack
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(cxMM, yOutputMM)), module, TemplateModule::OUT_OUTPUT));

        // ───────────────────────────────────────────────────────────────────────────────────────
        // Status Lights
        // ───────────────────────────────────────────────────────────────────────────────────────
        
        // Status LED
        addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(cxMM + dxLEDMM, yLEDMM)), module, TemplateModule::LED_LIGHT));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // CONTEXT MENU - Right-click menu for advanced controls
    // ═══════════════════════════════════════════════════════════════════════════
    
    /**
     * @brief Add context menu items for advanced module controls
     * @param menu Pointer to the context menu being built
     */
    void appendContextMenu(Menu* menu) override {
        TemplateModule* module = dynamic_cast<TemplateModule*>(this->module);
        if (!module) return;

        // Add separator
        menu->addChild(new MenuSeparator);

        // Add your context menu items here
        // Examples:
        // menu->addChild(createBoolMenuItem("Some Option", "", [=]() { return module->someOption; }, [=](bool val) { module->someOption = val; }));
        // 
        // menu->addChild(createIndexSubmenuItem("Some Mode", "", [=]() { return module->someMode; }, [=](int val) { module->someMode = val; }, {
        //     "Mode 1",
        //     "Mode 2", 
        //     "Mode 3"
        // }));
    }
};

// -----------------------------------------------------------------------------
// MODULE REGISTRATION - Register module with VCV Rack
// -----------------------------------------------------------------------------

Model* modelTemplateModule = createModel<TemplateModule, TemplateModuleWidget>("TemplateModule");
