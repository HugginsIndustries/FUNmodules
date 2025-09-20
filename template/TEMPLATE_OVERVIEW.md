# VCV Rack 2 Module Template - Complete Overview

This template provides everything needed to create a new VCV Rack 2 module, for the FUNmodules collection.

## 📁 Template Structure

```
template/
├── TemplateModule.cpp          # Main module implementation
├── TemplateModule.hpp          # Header file with declarations
├── TemplateModule.svg          # Panel graphics template
├── README.md                   # Detailed documentation
└── TEMPLATE_OVERVIEW.md        # This overview file
```

## 🚀 Quick Start

### Setup
1. Copy all template files to /src (.cpp & .hpp) and /res (SVG) directories
2. Rename `TemplateModule` to `ModuleName`
3. Update all class names and references
4. Update file headers
5. Modify the panel SVG design

## 📋 What's Included

### Core Module Files
- **Complete Module Class** - Full VCV Rack 2 module implementation
- **Widget Class** - UI layout and interaction handling
- **Parameter System** - Knobs, buttons, switches with proper configuration
- **Input/Output System** - Jacks with proper signal handling
- **Light System** - LEDs and status indicators
- **JSON Persistence** - Save/load module state in patches
- **Context Menu** - Right-click menu for advanced controls

### Design Features
- **Scalable UI** - Millimeter-based coordinates that scale with HP size
- **Professional Layout** - Follows VCV Rack design patterns
- **Comprehensive Comments** - Detailed documentation throughout
- **Error Handling** - Proper null checks and validation
- **Thread Safety** - Correct audio/UI thread separation

## 🎯 Key Features

### Based on PolyQuanta Structure
- **Proven Architecture** - Uses the same patterns as the successful PolyQuanta module
- **Professional Quality** - Follows VCV Rack best practices
- **Comprehensive Documentation** - Detailed comments and examples
- **Modular Design** - Easy to extend and customize

### VCV Rack 2 Compatible
- **Latest APIs** - Uses current VCV Rack 2.6.4 features
- **Proper Integration** - Correct module registration and lifecycle
- **Performance Optimized** - Alloc-free audio processing
- **Cross-Platform** - Works on Windows, macOS, and Linux

### Developer Friendly
- **Clear Structure** - Well-organized code with logical sections
- **Easy Customization** - Simple to modify and extend
- **Comprehensive Examples** - Shows how to implement common features
- **Best Practices** - Follows VCV Rack development guidelines

## 🔧 Customization Guide

### 1. Basic Setup
- Copy files
- Update module name and class names
- Update file headers

### 2. Panel Design
- Edit the SVG file to create your panel design
- Use millimeter coordinates for proper scaling
- Follow VCV Rack design guidelines

### 3. Parameters
- Add parameters to the `ParamId` enum
- Configure them in the constructor
- Use them in the `process()` method

### 4. DSP Processing
- Implement your audio processing in `process()`
- Keep it alloc-free for performance
- Use proper thread safety

### 5. UI Layout
- Update widget constructor for component placement
- Use the coordinate system for proper scaling
- Add context menu items for advanced controls

## 📚 Documentation

### README.md
- Complete usage guide
- Customization instructions
- Best practices
- Troubleshooting tips

### Code Comments
- Detailed function documentation
- Parameter explanations
- Usage examples
- Implementation notes

### Examples
- Basic module implementation
- Advanced features
- Common patterns
- Best practices

## 🛠️ Development Workflow

1. **Setup** - Copy files & update module/class names and headers
2. **Design** - Create your panel SVG design
3. **Implement** - Add your DSP logic and UI
4. **Test** - Use the test framework to validate
5. **Build** - Compile
6. **Debug** - Test in VCV Rack
7. **Polish** - Add documentation and final touches

## 🎨 Design Patterns

### Module Structure
```cpp
struct YourModule : Module {
    // Enums for all UI elements
    enum ParamId { ... };
    enum InputId { ... };
    enum OutputId { ... };
    enum LightId { ... };
    
    // DSP state variables
    // Configuration options
    // Constructor
    // JSON persistence
    // Process method
};
```

### Widget Structure
```cpp
struct YourModuleWidget : ModuleWidget {
    // Constructor with UI layout
    // Coordinate system setup
    // Component placement
    // Context menu
};
```

### Coordinate System
- **Millimeter-based** - Consistent with panel SVGs
- **Centered layout** - Scales with HP size changes
- **Professional spacing** - Follows Eurorack standards

## 🔍 Quality Assurance

### Code Quality
- **Comprehensive Comments** - Every function documented
- **Error Handling** - Proper null checks and validation
- **Thread Safety** - Correct audio/UI separation
- **Performance** - Alloc-free audio processing

### Testing
- **Basic Tests** - Module creation and widget creation
- **Build Validation** - Compilation and linking
- **Runtime Testing** - Functionality in VCV Rack

### Documentation
- **Complete Guides** - Setup, customization, and usage
- **Code Examples** - Real implementations
- **Best Practices** - VCV Rack development guidelines

## 🚀 Getting Started

1. **Copy the template** to your development directory
2. **Run the setup script** with your module name
3. **Edit the panel SVG** to create your design
4. **Implement your DSP logic** in the process method
5. **Test and iterate** until you're happy with the result

## 📞 Support

- **Documentation** - Comprehensive guides included
- **Examples** - Real code examples throughout
- **Best Practices** - VCV Rack development guidelines
- **Community** - VCV Rack developer community

## 📄 License

This template is provided under the GPL-3.0-or-later license, same as the FUNmodules collection.

---

**Happy coding! 🎛️**

Create amazing VCV Rack modules with this professional template based on the proven FUNmodules architecture.
