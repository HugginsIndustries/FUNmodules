#include "plugin.hpp"


struct Offset : Module {
	enum ParamId {
		KNOB1_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		INPUTS_LEN
	};
	enum OutputId {
		OUTPUT1_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	Offset() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(KNOB1_PARAM, -5.f, 5.f, 0.f, "Offset", " V");  // -5..+5 V
		configOutput(OUTPUT1_OUTPUT, "Offset");
	}

	void process(const ProcessArgs& args) override {
		float v = params[KNOB1_PARAM].getValue();
    	outputs[OUTPUT1_OUTPUT].setVoltage(v);
	}
};


struct OffsetWidget : ModuleWidget {
	OffsetWidget(Offset* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Offset.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(25.4, 64.25)), module, Offset::KNOB1_PARAM));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(25.4, 115.152)), module, Offset::OUTPUT1_OUTPUT));
	}
};


Model* modelOffset = createModel<Offset, OffsetWidget>("Offset");