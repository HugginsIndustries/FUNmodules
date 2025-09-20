# VCV Rack 2 Module Template

This template provides a complete starting point for creating new VCV Rack 2 modules, based on the structure and patterns used in the FUNmodules collection (specifically PolyQuanta).

## Files Included

- `TemplateModule.cpp` - Main module implementation with DSP processing
- `TemplateModule.hpp` - Header file with forward declarations
- `plugin_template.cpp` - Plugin registration template (copy to `plugin.cpp`)
- `plugin_template.hpp` - Plugin header template (copy to `plugin.hpp`)
- `README.md` - This documentation file

## Quick Start

1. **Copy the template files** to your new module directory
2. **Rename the files** to match your module name:
   - `TemplateModule.cpp` → `YourModule.cpp`
   - `TemplateModule.hpp` → `YourModule.hpp`
3. **Update the class names** throughout the files:
   - `TemplateModule` → `YourModule`
   - `TemplateModuleWidget` → `YourModuleWidget`
4. **Update the file headers** with your information
5. **Copy and rename plugin files**:
   - `plugin_template.cpp` → `plugin.cpp`
   - `plugin_template.hpp` → `plugin.hpp`
6. **Update the plugin files** to include your module
7. **Create your panel SVG** and place it in `res/YourModule.svg`
8. **Implement your DSP logic** in the `process()` method
9. **Update the widget layout** in the widget constructor

## Template Structure

### Module Class (`TemplateModule`)

The module class contains:

- **Parameter/Input/Output/Light Enums** - Define all UI elements
- **DSP State Variables** - Runtime processing state
- **Configuration Options** - User-configurable settings
- **Constructor** - Parameter configuration
- **JSON Methods** - State persistence (`dataToJson`/`dataFromJson`)
- **Process Method** - Main DSP processing loop

### Widget Class (`TemplateModuleWidget`)

The widget class contains:

- **Constructor** - UI layout and component placement
- **Coordinate System** - Millimeter-based positioning for scalability
- **Context Menu** - Right-click menu for advanced controls

### Key Features

- **Proper VCV Rack 2 Integration** - Uses correct APIs and patterns
- **Scalable UI Layout** - Millimeter-based coordinates that scale with HP size
- **JSON State Persistence** - Saves/loads module state in patches
- **Context Menu System** - Advanced controls via right-click menu
- **Comprehensive Documentation** - Detailed comments throughout
- **Error Handling** - Proper null checks and validation

## Customization Guide

### 1. Parameters

Add parameters to the `ParamId` enum:

```cpp
enum ParamId {
    KNOB_PARAM,             // Existing
    BUTTON_PARAM,           // Existing
    NEW_KNOB_PARAM,         // Add new parameters here
    NEW_SWITCH_PARAM,
    PARAMS_LEN
};
```

Configure them in the constructor:

```cpp
configParam(NEW_KNOB_PARAM, 0.f, 10.f, 5.f, "New knob", "V");
configSwitch(NEW_SWITCH_PARAM, 0.f, 1.f, 0.f, "New switch", {"Off", "On"});
```

### 2. Inputs/Outputs

Add inputs/outputs to their respective enums:

```cpp
enum InputId { 
    IN_INPUT,               // Existing
    NEW_INPUT,              // Add new inputs here
    INPUTS_LEN
};

enum OutputId { 
    OUT_OUTPUT,             // Existing
    NEW_OUTPUT,             // Add new outputs here
    OUTPUTS_LEN
};
```

Configure them in the constructor:

```cpp
configInput(NEW_INPUT, "New input");
configOutput(NEW_OUTPUT, "New output");
```

### 3. Lights

Add lights to the `LightId` enum:

```cpp
enum LightId { 
    LED_LIGHT,              // Existing
    NEW_LIGHT,              // Add new lights here
    LIGHTS_LEN
};
```

Configure them in the constructor:

```cpp
configLight(NEW_LIGHT, "New light");
```

### 4. DSP Processing

Implement your audio processing in the `process()` method:

```cpp
void process(const ProcessArgs& args) override {
    // Get input signals
    float input = inputs[IN_INPUT].getVoltage();
    
    // Get parameter values
    float knobValue = params[KNOB_PARAM].getValue();
    
    // Your DSP processing here
    float output = input * knobValue;
    
    // Set output signals
    outputs[OUT_OUTPUT].setVoltage(output);
    
    // Update lights
    lights[LED_LIGHT].setBrightness(output > 0.f ? 1.f : 0.f);
}
```

### 5. UI Layout

Update the widget constructor to place your components:

```cpp
// Add your components
addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(cxMM, yKnobMM)), module, YourModule::NEW_KNOB_PARAM));
addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cxMM, yInputMM)), module, YourModule::NEW_INPUT));
addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(cxMM, yOutputMM)), module, YourModule::NEW_OUTPUT));
addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(cxMM, yLEDMM)), module, YourModule::NEW_LIGHT));
```

### 6. State Persistence

Add your module state to the JSON methods:

```cpp
json_t* dataToJson() override {
    json_t* root = json_object();
    json_object_set_new(root, "someOption", json_boolean(someOption));
    return root;
}

void dataFromJson(json_t* root) override {
    json_t* someOptionJ = json_object_get(root, "someOption");
    if (someOptionJ) someOption = json_boolean_value(someOptionJ);
}
```

### 7. Context Menu

Add advanced controls to the context menu:

```cpp
void appendContextMenu(Menu* menu) override {
    YourModule* module = dynamic_cast<YourModule*>(this->module);
    if (!module) return;

    menu->addChild(new MenuSeparator);
    menu->addChild(createBoolMenuItem("Some Option", "", 
        [=]() { return module->someOption; }, 
        [=](bool val) { module->someOption = val; }));
}
```

## Building Your Module

1. **Add to Makefile** - Add your module to the build system
2. **Create Panel SVG** - Design your module panel in SVG format
3. **Test in Rack** - Load and test your module in VCV Rack
4. **Debug** - Use the debug console and logging for troubleshooting

## Best Practices

- **Keep DSP Alloc-Free** - Avoid memory allocation in the audio thread
- **Use Proper Threading** - UI updates only in the UI thread
- **Validate Inputs** - Check for null pointers and valid ranges
- **Document Everything** - Add comprehensive comments
- **Follow Naming Conventions** - Use consistent naming patterns
- **Test Thoroughly** - Test all features and edge cases

## Resources

- [VCV Rack 2 Developer Documentation](https://vcvrack.com/manual/PluginDevelopmentTutorial)
- [FUNmodules Repository](https://github.com/your-repo/FUNmodules) - Reference implementation
- [VCV Community Forum](https://community.vcvrack.com/) - Developer support

## License

This template is provided under the GPL-3.0-or-later license, same as the FUNmodules collection.
