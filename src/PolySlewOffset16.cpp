#include "plugin.hpp"
using namespace rack;
using namespace rack::componentlibrary;

struct PolySlewOffset16 : Module {
    // --- keep your generated enums but add a 32-slot light bank for bipolar LEDs ---
    enum ParamId {
        SL1_PARAM, SL2_PARAM, OFF1_PARAM, OFF2_PARAM,
        SL3_PARAM, SL4_PARAM, OFF3_PARAM, OFF4_PARAM,
        SL5_PARAM, SL6_PARAM, OFF5_PARAM, OFF6_PARAM,
        SL7_PARAM, SL8_PARAM, OFF7_PARAM, OFF8_PARAM,
        SL9_PARAM, SL10_PARAM, OFF9_PARAM, OFF10_PARAM,
        SL11_PARAM, SL12_PARAM, OFF11_PARAM, OFF12_PARAM,
        SL13_PARAM, SL14_PARAM, OFF13_PARAM, OFF14_PARAM,
        SL15_PARAM, SL16_PARAM, OFF15_PARAM, OFF16_PARAM,
        RISE_SHAPE_PARAM,
        FALL_SHAPE_PARAM,
        RND_PARAM,
        PARAMS_LEN
    };
    enum InputId { IN_INPUT, RND_TRIG_INPUT, INPUTS_LEN  };
    enum OutputId { OUT_OUTPUT, OUTPUTS_LEN };

    // 2 light channels per voice: + (green) and – (red)
    enum LightId { ENUMS(CH_LIGHT, 32), LIGHTS_LEN };

    // Per-voice slew units 
    dsp::SlewLimiter slews[16];
    // Per-channel normalization for shape & timing
    float stepNorm[16] = {10.f};  // current step magnitude (V), defaults to 10 V
    int   stepSign[16] = {0};     // sign of current error (+1 / −1)
    dsp::BooleanTrigger rndBtnTrig;
    dsp::BooleanTrigger rndCvTrig;

    // Map channel index -> your interleaved enum IDs
	static const int SL_PARAM[16];
	static const int OFF_PARAM[16];

	PolySlewOffset16() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		// Per-channel knobs
		for (int i = 0; i < 16; ++i) {
			configParam(OFF_PARAM[i], -10.f, 10.f, 0.f, string::f("Ch %d offset", i+1), " V"); // New: ±10 V range offset
			// Slew: 0 .. 10 seconds, default 0 ms (i.e. no slew)
			// Note: 0 s is a bit problematic for our shape code, so we use 0.1 ms internally.
			configParam(SL_PARAM[i], 0.f, 10.0f, 0.0f, string::f("Ch %d slew (rise & fall)", i+1), " s"); // New: 0 .. 10 s range
		}
        // Global rise/fall curve: -1 = log-ish, 0 = linear, +1 = expo-ish
        configParam(RISE_SHAPE_PARAM, -1.f, 1.f, 0.f, "Rise shape");
        configParam(FALL_SHAPE_PARAM, -1.f, 1.f, 0.f, "Fall shape");
		// Input and output ports (only 1 each, poly)
		configInput(IN_INPUT,  "Poly signal");                          // shown on hover
        configInput(RND_TRIG_INPUT, "Randomize trigger (gate)");	    // shown on hover
		configOutput(OUT_OUTPUT, "Poly signal (slewed + offset)");      // shown on hover

		// Optional bonus: when the module is bypassed in Rack, pass IN → OUT
        configBypass(IN_INPUT, OUT_OUTPUT);
        // Momentary button (edge-detected in process)
        configParam(RND_PARAM, 0.f, 1.f, 0.f, "Randomize");
        // init step tracking
        for (int i = 0; i < 16; ++i) {
            stepNorm[i] = 10.f;
            stepSign[i] = 0;
        }
	}

    void doRandomize() {
        for (int i = 0; i < 16; ++i) {
            // Slew: 0..10 s
            params[SL_PARAM[i]].setValue(10.f * random::uniform());
            // Offset: -10..+10 V
            params[OFF_PARAM[i]].setValue(20.f * (random::uniform() - 0.5f));
        }
        // Shapes: -1..+1
        params[RISE_SHAPE_PARAM].setValue(2.f * random::uniform() - 1.f);
        params[FALL_SHAPE_PARAM].setValue(2.f * random::uniform() - 1.f);
    }

    void process(const ProcessArgs& args) override {
        // If no input: act as 16-ch DC source
        int n = inputs[IN_INPUT].isConnected() ? inputs[IN_INPUT].getChannels() : 16;
        if (n > 16) n = 16;
        outputs[OUT_OUTPUT].setChannels(n);

        // Randomize on UI button or gate edge (≥ 2 V)
        if (rndBtnTrig.process(params[RND_PARAM].getValue() > 0.5f) ||
            rndCvTrig.process(inputs[RND_TRIG_INPUT].getVoltage() >= 2.f)) {
            doRandomize();
        }

        for (int c = 0; c < n; ++c) {
            // Target = input + per-channel offset
            float in  = inputs[IN_INPUT].isConnected() ? inputs[IN_INPUT].getVoltage(c) : 0.f;
            float off = params[OFF_PARAM[c]].getValue();
            float target = in + off;

            // Base rate from seconds: **current step** takes `sec` seconds (matches BGA behavior)
            float sec = params[SL_PARAM[c]].getValue();
            const float minSec = 1e-4f;                   // ~0.1 ms → “no slew”
            // Compute error using last output sample
            float yPrev = outputs[OUT_OUTPUT].getVoltage(c);
            float err   = target - yPrev;
            int   sign  = (err > 0.f) - (err < 0.f);
            float aerr  = std::fabs(err);
            // Start of a new move (direction change) or larger jump? Re-normalize.
            if (sign != stepSign[c] || aerr > stepNorm[c]) {
                stepSign[c] = sign;
                stepNorm[c] = std::max(aerr, 1e-4f);
            }
            // Seconds → base rate so this **stepNorm** volt jump takes `sec` seconds.
            float baseRate = (sec <= minSec) ? 1e9f : (stepNorm[c] / sec); // V/s

            // Global shapes: -1=log-ish, 0=linear, +1=expo-ish
            float riseShape = params[RISE_SHAPE_PARAM].getValue(); // [-1,1]
            float fallShape = params[FALL_SHAPE_PARAM].getValue(); // [-1,1]

            // Distance to target (0..1) normalized to **this** step magnitude
            float u = clamp(aerr / stepNorm[c], 0.f, 1.f);

            // Normalized shape (time preserved). u = remaining fraction (1→0).
            // s < 0  → **more logarithmic** (fast start):   m(u) = exp(k*u),      C = (1 - e^{-k})/k
            // s > 0  → exponential (slow start):            m(u) = 1/(1 + k*u),   C = 1 + k/2
            auto shapeMul = [](float u, float s) -> float {
                if (std::fabs(s) < 1e-6f) return 1.f;   // strictly linear
                const float K_POS = 6.0f;               // expo strength (unchanged feel)
                const float K_NEG = 8.0f;               // log strength (make it DRAMATIC)
                if (s < 0.f) {
                    float k = K_NEG * (-s);             // s in [-1,0)
                    float m = std::exp(k * u);          // big early rate, eases to target
                    float C = (1.f - std::exp(-k)) / k; // ∫ e^{-k u} du = (1-e^{-k})/k
                    return std::max(1e-4f, C * m);
                } else {
                    float k = K_POS * s;                // s in (0,1]
                    float m = 1.f / (1.f + k * u);      // slow start, fast finish
                    float C = 1.f + 0.5f * k;           // ∫ (1 + k u) du
                    return std::max(1e-4f, C * m);
                }
            };

            float rateRise = baseRate * shapeMul(u, riseShape);
            float rateFall = baseRate * shapeMul(u, fallShape);
            slews[c].setRiseFall(rateRise, rateFall);

            float y = slews[c].process(args.sampleTime, target);    // slew towards target
            y = clamp(y, -12.f, 12.f);

            outputs[OUT_OUTPUT].setVoltage(y, c);

            // Bipolar LED (green for +, red for −). Smooth = nice decay.
            float g = clamp( y / 10.f, 0.f, 1.f);
            float r = clamp(-y / 10.f, 0.f, 1.f);
            lights[CH_LIGHT + 2*c + 0].setBrightnessSmooth(g, args.sampleTime);
            lights[CH_LIGHT + 2*c + 1].setBrightnessSmooth(r, args.sampleTime);
        }

        // Clear any unused LEDs
        for (int c = n; c < 16; ++c) {
            lights[CH_LIGHT + 2*c + 0].setBrightness(0.f);
            lights[CH_LIGHT + 2*c + 1].setBrightness(0.f);
        }
    }
};

// out-of-class definitions required by C++11
const int PolySlewOffset16::SL_PARAM[16] = {
    PolySlewOffset16::SL1_PARAM,  PolySlewOffset16::SL2_PARAM,
    PolySlewOffset16::SL3_PARAM,  PolySlewOffset16::SL4_PARAM,
    PolySlewOffset16::SL5_PARAM,  PolySlewOffset16::SL6_PARAM,
    PolySlewOffset16::SL7_PARAM,  PolySlewOffset16::SL8_PARAM,
    PolySlewOffset16::SL9_PARAM,  PolySlewOffset16::SL10_PARAM,
    PolySlewOffset16::SL11_PARAM, PolySlewOffset16::SL12_PARAM,
    PolySlewOffset16::SL13_PARAM, PolySlewOffset16::SL14_PARAM,
    PolySlewOffset16::SL15_PARAM, PolySlewOffset16::SL16_PARAM
};

const int PolySlewOffset16::OFF_PARAM[16] = {
    PolySlewOffset16::OFF1_PARAM,  PolySlewOffset16::OFF2_PARAM,
    PolySlewOffset16::OFF3_PARAM,  PolySlewOffset16::OFF4_PARAM,
    PolySlewOffset16::OFF5_PARAM,  PolySlewOffset16::OFF6_PARAM,
    PolySlewOffset16::OFF7_PARAM,  PolySlewOffset16::OFF8_PARAM,
    PolySlewOffset16::OFF9_PARAM,  PolySlewOffset16::OFF10_PARAM,
    PolySlewOffset16::OFF11_PARAM, PolySlewOffset16::OFF12_PARAM,
    PolySlewOffset16::OFF13_PARAM, PolySlewOffset16::OFF14_PARAM,
    PolySlewOffset16::OFF15_PARAM, PolySlewOffset16::OFF16_PARAM
};

struct PolySlewOffset16Widget : ModuleWidget {
	PolySlewOffset16Widget(PolySlewOffset16* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/PolySlewOffset16.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Global shape controls (Rise / Fall), placed symmetrically from panel center.
        {
            const float cx = 25.4f;      // 10HP center in mm (50.8 / 2)
            const float y  = 17.5f;      // vertical position under the labels
            float dx       = 17.5f;      // horizontal offset from center (tweak to taste)
            addParam(createParamCentered<Trimpot>(mm2px(Vec(cx - dx, y)), module, PolySlewOffset16::RISE_SHAPE_PARAM));
            addParam(createParamCentered<Trimpot>(mm2px(Vec(cx + dx, y)), module, PolySlewOffset16::FALL_SHAPE_PARAM));
        }
		// Slew and offset controls (tiny trimpots in a grid)
		addParam(createParamCentered<Trimpot>(mm2px(Vec(6.442, 41.308)), module, PolySlewOffset16::SL1_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(13.318, 41.308)), module, PolySlewOffset16::SL2_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(37.501, 41.308)), module, PolySlewOffset16::OFF1_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(44.377, 41.308)), module, PolySlewOffset16::OFF2_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(6.442, 49.56)), module, PolySlewOffset16::SL3_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(13.318, 49.56)), module, PolySlewOffset16::SL4_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(37.501, 49.56)), module, PolySlewOffset16::OFF3_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(44.377, 49.56)), module, PolySlewOffset16::OFF4_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(6.442, 57.811)), module, PolySlewOffset16::SL5_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(13.318, 57.811)), module, PolySlewOffset16::SL6_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(37.501, 57.811)), module, PolySlewOffset16::OFF5_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(44.377, 57.811)), module, PolySlewOffset16::OFF6_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(6.442, 66.063)), module, PolySlewOffset16::SL7_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(13.318, 66.063)), module, PolySlewOffset16::SL8_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(37.501, 66.063)), module, PolySlewOffset16::OFF7_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(44.377, 66.063)), module, PolySlewOffset16::OFF8_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(6.442, 74.314)), module, PolySlewOffset16::SL9_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(13.318, 74.314)), module, PolySlewOffset16::SL10_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(37.501, 74.314)), module, PolySlewOffset16::OFF9_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(44.377, 74.314)), module, PolySlewOffset16::OFF10_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(6.442, 82.566)), module, PolySlewOffset16::SL11_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(13.318, 82.566)), module, PolySlewOffset16::SL12_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(37.501, 82.566)), module, PolySlewOffset16::OFF11_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(44.377, 82.566)), module, PolySlewOffset16::OFF12_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(6.442, 90.817)), module, PolySlewOffset16::SL13_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(13.318, 90.817)), module, PolySlewOffset16::SL14_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(37.501, 90.817)), module, PolySlewOffset16::OFF13_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(44.377, 90.817)), module, PolySlewOffset16::OFF14_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(6.442, 99.069)), module, PolySlewOffset16::SL15_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(13.318, 99.069)), module, PolySlewOffset16::SL16_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(37.501, 99.069)), module, PolySlewOffset16::OFF15_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(44.377, 99.069)), module, PolySlewOffset16::OFF16_PARAM));
		// Input port (poly)
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(10.424, 111.743)), module, PolySlewOffset16::IN_INPUT));
		// Randomize trigger jack (centered between IN/OUT)
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(25.409, 121.743)), module, PolySlewOffset16::RND_TRIG_INPUT));
		// Randomize pushbutton slightly above the jack row
		addParam(createParamCentered<VCVButton>(mm2px(Vec(25.409, 104.000)), module, PolySlewOffset16::RND_PARAM));
		// Output port (poly)
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(40.395, 111.743)), module, PolySlewOffset16::OUT_OUTPUT));
		// Lights (tiny bi-color LEDs in a grid)
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(21.971, 41.308)), module, PolySlewOffset16::CH_LIGHT + 2*0));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(28.848, 41.308)), module, PolySlewOffset16::CH_LIGHT + 2*1));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(21.971, 49.56)), module, PolySlewOffset16::CH_LIGHT + 2*2));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(28.848, 49.56)), module, PolySlewOffset16::CH_LIGHT + 2*3));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(21.971, 57.811)), module, PolySlewOffset16::CH_LIGHT + 2*4));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(28.848, 57.811)), module, PolySlewOffset16::CH_LIGHT + 2*5));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(21.971, 66.063)), module, PolySlewOffset16::CH_LIGHT + 2*6));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(28.848, 66.063)), module, PolySlewOffset16::CH_LIGHT + 2*7));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(21.971, 74.314)), module, PolySlewOffset16::CH_LIGHT + 2*8));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(28.848, 74.314)), module, PolySlewOffset16::CH_LIGHT + 2*9));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(21.971, 82.566)), module, PolySlewOffset16::CH_LIGHT + 2*10));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(28.848, 82.566)), module, PolySlewOffset16::CH_LIGHT + 2*11));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(21.971, 90.817)), module, PolySlewOffset16::CH_LIGHT + 2*12));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(28.848, 90.817)), module, PolySlewOffset16::CH_LIGHT + 2*13));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(21.971, 99.069)), module, PolySlewOffset16::CH_LIGHT + 2*14));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(28.848, 99.069)), module, PolySlewOffset16::CH_LIGHT + 2*15));
	}
};


Model* modelPolySlewOffset16 = createModel<PolySlewOffset16, PolySlewOffset16Widget>("PolySlewOffset16");