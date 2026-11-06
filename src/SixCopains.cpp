// SPDX-License-Identifier: GPL-3.0-or-later
//
// 6 Copains for VCV Rack: six Just Friends-style voices with a built-in
// Banks note player.
//
// A port of 6Copains for the disting NT by Mark IJzerman
// (https://github.com/markijzerman/6copains), which ports nb_monamie by
// sonoCircuit (https://github.com/sonocircuit/nb_monamie), whose waveshaping
// comes from the Imaginary Friends engine by @synthetivv. GPL-3.0.

#include "plugin.hpp"
#include "CopainsEngine.hpp"
#include "BanksPlayer.hpp"

using copains::kNumVoices;
using copains::kNumBanks;
using copains::kBankNotes;

// Panel positions in mm. res/make_panel.py draws the labels at the same places.
namespace layout {
static const float R1 = 25.f, R2 = 41.5f, R3 = 61.f, R4 = 75.5f, R5 = 91.5f, R6 = 107.5f, RIO = 120.5f;
static const float C1 = 12.f, C2 = 31.f, C3 = 50.f, C4 = 69.f, C5 = 88.f;
static const float PAN_X[6] = {10.f, 26.f, 42.f, 58.f, 74.f, 90.f};
static const float IN_X[3] = {10.f, 21.f, 32.f};
static const float OUT_X[6] = {47.f, 59.f, 71.f, 83.f, 95.f, 107.f};
static const float MIX_L_X = 125.f, MIX_R_X = 139.f;
static const float B1 = 113.f, B2 = 127.f, B3 = 141.f;
static const float BR1 = 46.f, BR2 = 59.5f, BR3 = 73.f, BR4 = 87.f, BR5 = 104.f;
static const float BOUT_V_X = 120.f, BOUT_G_X = 134.f;
static const float DISP_X = 104.f, DISP_Y = 18.6f, DISP_W = 46.f, DISP_H = 18.f;
}  // namespace layout

static const char* const kNoteLetters[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static std::string noteName(int n) {
	n = clamp(n, 0, 127);
	return string::f("%s%d", kNoteLetters[n % 12], n / 12 - 1);
}

// ---------------------------------------------------------------- quantities

struct RampQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		int rise = (int)std::round((getValue() + 1.f) * 50.f);
		return string::f("%d/%d", rise, 100 - rise);
	}
	std::string getUnit() override { return " rise/fall"; }
};

struct CurveQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		float c = getValue();
		if (c <= -0.995f) return "square";
		if (c < -0.005f) return string::f("sqr %d / tri %d", (int)std::round(-c * 100.f), (int)std::round((1.f + c) * 100.f));
		if (c < 0.005f) return "triangle";
		if (c <= 0.5f) return string::f("tri, rounded %d%%", (int)std::round(c * 200.f));
		if (c >= 0.995f) return "sine";
		float m = (c - 0.5f) * 2.f;
		return string::f("tri %d / sin %d", (int)std::round((1.f - m) * 100.f), (int)std::round(m * 100.f));
	}
};

struct FmIndexQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		float v = getValue();
		int pct = (int)std::round(std::fabs(v) * 100.f);
		if (pct == 0) return "0%";
		return string::f("%s %d%%", v < 0.f ? "intone" : "linear", pct);
	}
};

struct SlopeQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		int atk = (int)std::round(getValue() * 100.f);
		return string::f("%d/%d", atk, 100 - atk);
	}
	std::string getUnit() override { return " atk/rel"; }
};

struct PanQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		int p = (int)std::round(getValue() * 100.f);
		if (p == 0) return "centre";
		return p < 0 ? string::f("L %d", -p) : string::f("R %d", p);
	}
};

// ---------------------------------------------------------------- module

static const int kBlock = 8;          // frames per engine render: under 0.2 ms at 48 kHz
static const float kOutVolts = 10.f;  // engine 1.0 = 5 V, plus the NT's default +6 dB

struct SixCopains : Module {
	enum ParamId {
		LEVEL_PARAM, RAMP_PARAM, CURVE_PARAM, FM_RATIO_PARAM, FM_INDEX_PARAM, PAN_DRIFT_PARAM,
		ENV_MODE_PARAM, SLOPE_PARAM, TIME_PARAM, RETRIG_PARAM, OCTAVE_PARAM,
		MORPH_PARAM, RAMP_MOD_PARAM, CURVE_MOD_PARAM, FM_MOD_PARAM, SLOPE_MOD_PARAM, TIME_MOD_PARAM,
		ENUMS(PAN_PARAM, kNumVoices),
		PLAY_PARAM, CAPTURE_PARAM, RATE_PARAM, RAND_PARAM, AMOUNT_PARAM, BANK_PARAM,
		VEL_MIN_PARAM, VEL_MAX_PARAM, GATE_LEN_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT, GATE_INPUT, VEL_INPUT,
		RAMP_INPUT, CURVE_INPUT, FM_INPUT, SLOPE_INPUT, TIME_INPUT, MORPH_INPUT,
		CLOCK_INPUT, AMOUNT_INPUT, BANK_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		ENUMS(VOICE_OUTPUT, kNumVoices),
		LEFT_OUTPUT, RIGHT_OUTPUT, BANK_VOCT_OUTPUT, BANK_GATE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(VOICE_LIGHT, kNumVoices),
		PLAY_LIGHT, CAPTURE_LIGHT,
		LIGHTS_LEN
	};

	copains::Engine engine;
	copains::BanksPlayer banks;
	copains::GateDetector gates[kNumVoices];
	float heldPitch[kNumVoices] = {};
	int cvCount = 0;

	float bufL[kBlock] = {}, bufR[kBlock] = {};
	float voiceBuf[kNumVoices][copains::kMaxChunk] = {};
	int bufPos = kBlock;

	dsp::SchmittTrigger clockTrigger;
	dsp::BooleanTrigger captureTrigger;
	float captureFlash = 0.f;
	dsp::ClockDivider lightDivider;

	// Saved with the patch
	bool sampleAndHold = false;   // pitch captured on the gate's rising edge
	bool outsLeaveMix = true;     // a patched voice output takes that voice out of L/R

	// For the display
	int shownBank = 0;
	float voiceLevel[kNumVoices] = {};

	SixCopains() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// Sound (defaults are nb_monamie's default patch)
		configParam(LEVEL_PARAM, 0.f, 1.f, 0.8f, "Level (drive)", "%", 0.f, 100.f);
		configParam<RampQuantity>(RAMP_PARAM, -1.f, 1.f, 0.08f, "Ramp");
		configParam<CurveQuantity>(CURVE_PARAM, -1.f, 1.f, 1.f, "Curve");
		configParam(FM_RATIO_PARAM, 0.5f, 2.f, 1.01f, "FM ratio", "x");
		configParam<FmIndexQuantity>(FM_INDEX_PARAM, -1.f, 1.f, 0.f, "FM index (left: intone, right: linear)");
		configParam(PAN_DRIFT_PARAM, 0.f, 1.f, 0.12f, "Pan drift", "%", 0.f, 100.f);
		configSwitch(ENV_MODE_PARAM, 0.f, 2.f, 0.f, "Envelope", {"AR", "ASR (holds while the gate is high)", "Cycle (repeats while the gate is high)"});
		configParam<SlopeQuantity>(SLOPE_PARAM, 0.f, 1.f, 0.03f, "Slope");
		// Time = 0.0026 s * e^(8.0317 x): 2.6 ms to 8 s
		configParam(TIME_PARAM, 0.f, 1.f, 0.547f, "Time", " s", 3076.923f, 0.0026f);
		configSwitch(RETRIG_PARAM, 0.f, 1.f, 0.f, "Retrigger", {"Fade (old note fades in 50 ms)", "Overlap (old note rings on)"});
		configParam(OCTAVE_PARAM, -3.f, 3.f, 0.f, "Octave");
		getParamQuantity(OCTAVE_PARAM)->snapEnabled = true;
		// Morph
		configParam(MORPH_PARAM, 0.f, 1.f, 0.f, "Morph", "%", 0.f, 100.f);
		configParam(RAMP_MOD_PARAM, -1.f, 1.f, 0.61f, "Morph moves Ramp by", "%", 0.f, 100.f);
		configParam(CURVE_MOD_PARAM, -1.f, 1.f, 0.f, "Morph moves Curve by", "%", 0.f, 100.f);
		configParam(FM_MOD_PARAM, -1.f, 1.f, -0.51f, "Morph moves FM index by", "%", 0.f, 100.f);
		configParam(SLOPE_MOD_PARAM, -1.f, 1.f, 0.f, "Morph moves Slope by", "%", 0.f, 100.f);
		configParam(TIME_MOD_PARAM, -1.f, 1.f, 0.f, "Morph moves Time by", " s");
		// Pans
		const float pans[kNumVoices] = {-0.2f, 0.2f, -0.5f, 0.5f, -0.8f, 0.8f};
		for (int v = 0; v < kNumVoices; v++)
			configParam<PanQuantity>(PAN_PARAM + v, -1.f, 1.f, pans[v], string::f("Voice %d pan", v + 1));
		// Banks
		configSwitch(PLAY_PARAM, 0.f, 1.f, 0.f, "Banks play", {"Stopped", "Playing"});
		configButton(CAPTURE_PARAM, "Capture the chord on V/OCT into this bank");
		// Rate = 20 ms * 100^x: 20 ms to 2 s, default 150 ms
		configParam(RATE_PARAM, 0.f, 1.f, 0.4375f, "Clock rate (when CLOCK is unpatched)", " ms", 100.f, 20.f);
		configParam(RAND_PARAM, 0.f, 1.f, 0.f, "Clock rand", "%", 0.f, 100.f);
		configParam(AMOUNT_PARAM, 0.f, 1.f, 0.4f, "Note amount (chance per note)", "%", 0.f, 100.f);
		configParam(BANK_PARAM, 1.f, 8.f, 1.f, "Bank");
		getParamQuantity(BANK_PARAM)->snapEnabled = true;
		configParam(VEL_MIN_PARAM, 0.f, 1.f, 40.f / 127.f, "Velocity min", "%", 0.f, 100.f);
		configParam(VEL_MAX_PARAM, 0.f, 1.f, 100.f / 127.f, "Velocity max", "%", 0.f, 100.f);
		configParam(GATE_LEN_PARAM, 0.01f, 1.f, 0.5f, "Gate length (of the clock interval)", "%", 0.f, 100.f);

		configInput(VOCT_INPUT, "V/OCT (polyphonic: channel n plays voice n)");
		configInput(GATE_INPUT, "Gate (polyphonic, or mono to play every V/OCT channel as a chord)");
		configInput(VEL_INPUT, "Velocity, 0-10 V (polyphonic)");
		configInput(RAMP_INPUT, "Ramp CV");
		configInput(CURVE_INPUT, "Curve CV");
		configInput(FM_INPUT, "FM index CV");
		configInput(SLOPE_INPUT, "Slope CV");
		configInput(TIME_INPUT, "Time CV");
		configInput(MORPH_INPUT, "Morph CV (0-10 V)");
		configInput(CLOCK_INPUT, "Banks clock");
		configInput(AMOUNT_INPUT, "Note amount CV");
		configInput(BANK_INPUT, "Bank CV (1.25 V per bank)");
		for (int v = 0; v < kNumVoices; v++) configOutput(VOICE_OUTPUT + v, string::f("Voice %d", v + 1));
		configOutput(LEFT_OUTPUT, "Mix left (mono if R is unpatched)");
		configOutput(RIGHT_OUTPUT, "Mix right");
		configOutput(BANK_VOCT_OUTPUT, "Banks V/OCT (6 channels)");
		configOutput(BANK_GATE_OUTPUT, "Banks gates (6 channels)");
		configLight(PLAY_LIGHT, "Playing");

		lightDivider.setDivision(64);
		engine.init(APP->engine->getSampleRate());
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.init(e.sampleRate);
		cvCount = 0;
		for (int v = 0; v < kNumVoices; v++) gates[v] = copains::GateDetector();
		bufPos = kBlock;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		banks.resetBanks();
		sampleAndHold = false;
		outsLeaveMix = true;
		engine.init(APP->engine->getSampleRate());
		cvCount = 0;
		for (int v = 0; v < kNumVoices; v++) gates[v] = copains::GateDetector();
	}

	// Knob + CV, where 10 V moves the full width of the knob.
	float knobCv(int param, int input, float lo, float hi) {
		float v = params[param].getValue();
		if (inputs[input].isConnected()) v += inputs[input].getVoltage() * 0.1f * (hi - lo);
		return clamp(v, lo, hi);
	}

	int currentBank() {
		float b = params[BANK_PARAM].getValue() - 1.f;
		if (inputs[BANK_INPUT].isConnected()) b += inputs[BANK_INPUT].getVoltage() / 1.25f;
		return clamp((int)std::floor(b + 0.5f), 0, kNumBanks - 1);
	}

	void applyParams() {
		copains::Params& p = engine.params;
		p.level = params[LEVEL_PARAM].getValue();
		p.ramp = knobCv(RAMP_PARAM, RAMP_INPUT, -1.f, 1.f);
		p.curve = knobCv(CURVE_PARAM, CURVE_INPUT, -1.f, 1.f);
		p.fmRatio = params[FM_RATIO_PARAM].getValue();
		p.fmIndex = knobCv(FM_INDEX_PARAM, FM_INPUT, -1.f, 1.f);
		p.panDrift = params[PAN_DRIFT_PARAM].getValue();
		p.envMode = (int)std::round(params[ENV_MODE_PARAM].getValue());
		p.slope = knobCv(SLOPE_PARAM, SLOPE_INPUT, 0.f, 1.f);
		p.time = 0.0026f * std::exp(knobCv(TIME_PARAM, TIME_INPUT, 0.f, 1.f) * 8.03168474f);
		p.overlap = params[RETRIG_PARAM].getValue() > 0.5f;
		p.octave = (int)std::round(params[OCTAVE_PARAM].getValue());
		p.morph = knobCv(MORPH_PARAM, MORPH_INPUT, 0.f, 1.f);
		p.rampMod = params[RAMP_MOD_PARAM].getValue();
		p.curveMod = params[CURVE_MOD_PARAM].getValue();
		p.fmMod = params[FM_MOD_PARAM].getValue();
		p.slopeMod = params[SLOPE_MOD_PARAM].getValue();
		p.timeMod = params[TIME_MOD_PARAM].getValue();
		for (int v = 0; v < kNumVoices; v++) p.pan[v] = params[PAN_PARAM + v].getValue();
	}

	static float voltsToNote(float volts) {
		if (!std::isfinite(volts)) volts = 0.f;
		return 60.f + 12.f * clamp(volts, -10.f, 10.f);  // VCV: 0 V = C4
	}

	// Polyphonic V/OCT + GATE: channel n plays voice n. A mono gate is shared
	// by every channel, so it plays the V/OCT channels as a chord.
	void processCvVoices() {
		int count = 0;
		if (inputs[GATE_INPUT].isConnected())
			count = clamp(std::max(inputs[VOCT_INPUT].getChannels(), inputs[GATE_INPUT].getChannels()), 1, kNumVoices);
		if (count != engine.cvVoiceCount()) {
			engine.setCvVoiceCount(count);
			for (int v = 0; v < kNumVoices; v++) gates[v] = copains::GateDetector();
		}
		cvCount = count;
		const bool velIn = inputs[VEL_INPUT].isConnected();
		for (int v = 0; v < count; v++) {
			const float pitch = inputs[VOCT_INPUT].getPolyVoltage(v);
			const int edge = gates[v].process(inputs[GATE_INPUT].getPolyVoltage(v));
			if (edge > 0) {
				const float vel = velIn ? clamp(inputs[VEL_INPUT].getPolyVoltage(v) * 0.1f, 0.f, 1.f) : 1.f;
				heldPitch[v] = pitch;
				engine.cvVoiceOn(v, voltsToNote(pitch), vel);
			} else if (edge < 0) {
				engine.cvVoiceOff(v);
			} else if (gates[v].high && !sampleAndHold && pitch != heldPitch[v]) {
				heldPitch[v] = pitch;
				engine.cvSetNote(v, voltsToNote(pitch));
			}
		}
	}

	void capture() {
		if (!inputs[VOCT_INPUT].isConnected()) return;
		const int ch = std::min(inputs[VOCT_INPUT].getChannels(), kBankNotes);
		int held[kBankNotes];
		for (int c = 0; c < ch; c++)
			held[c] = clamp((int)std::round(voltsToNote(inputs[VOCT_INPUT].getVoltage(c))), 0, 127);
		banks.capture(currentBank(), held, ch);
		captureFlash = 0.3f;
	}

	void process(const ProcessArgs& args) override {
		processCvVoices();

		if (captureTrigger.process(params[CAPTURE_PARAM].getValue() > 0.f)) capture();

		// Banks player
		copains::BanksSettings s;
		s.externalClock = inputs[CLOCK_INPUT].isConnected();
		const bool clockEdge = s.externalClock && clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		s.rateSeconds = 0.02f * std::pow(100.f, params[RATE_PARAM].getValue());
		s.rand = params[RAND_PARAM].getValue();
		s.amount = knobCv(AMOUNT_PARAM, AMOUNT_INPUT, 0.f, 1.f);
		s.velMin = params[VEL_MIN_PARAM].getValue();
		s.velMax = params[VEL_MAX_PARAM].getValue();
		s.gate = params[GATE_LEN_PARAM].getValue();
		s.bank = currentBank();
		shownBank = s.bank;
		const bool playing = params[PLAY_PARAM].getValue() > 0.5f;
		banks.step(args.sampleTime, playing, clockEdge, s,
			[&](int note, float vel) { engine.noteOn(note, vel); },
			[&](int note) { engine.noteOff(note); });

		// Audio, rendered kBlock frames at a time
		if (bufPos >= kBlock) {
			applyParams();
			unsigned mask = 0x3Fu;
			if (outsLeaveMix)
				for (int v = 0; v < kNumVoices; v++)
					if (outputs[VOICE_OUTPUT + v].isConnected()) mask &= ~(1u << v);
			const bool stereo = outputs[RIGHT_OUTPUT].isConnected();
			engine.render(bufL, stereo ? bufR : nullptr, kBlock, voiceBuf, mask);
			if (!stereo)
				for (int i = 0; i < kBlock; i++) bufR[i] = 0.f;
			for (int v = 0; v < kNumVoices; v++) voiceLevel[v] = engine.voiceLevel(v);
			bufPos = 0;
		}
		outputs[LEFT_OUTPUT].setVoltage(bufL[bufPos] * kOutVolts);
		outputs[RIGHT_OUTPUT].setVoltage(bufR[bufPos] * kOutVolts);
		for (int v = 0; v < kNumVoices; v++) outputs[VOICE_OUTPUT + v].setVoltage(voiceBuf[v][bufPos] * kOutVolts);
		bufPos++;

		// Banks outputs: what the player is playing, octave included
		const int oct = (int)std::round(params[OCTAVE_PARAM].getValue());
		outputs[BANK_VOCT_OUTPUT].setChannels(kBankNotes);
		outputs[BANK_GATE_OUTPUT].setChannels(kBankNotes);
		for (int v = 0; v < kBankNotes; v++) {
			const copains::BanksPlayer::Held& h = banks.held(v);
			outputs[BANK_VOCT_OUTPUT].setVoltage((h.note + 12 * oct - 60) / 12.f, v);
			outputs[BANK_GATE_OUTPUT].setVoltage(h.active ? 10.f : 0.f, v);
		}

		if (lightDivider.process()) {
			const float dt = args.sampleTime * lightDivider.getDivision();
			for (int v = 0; v < kNumVoices; v++) lights[VOICE_LIGHT + v].setBrightnessSmooth(voiceLevel[v], dt);
			lights[PLAY_LIGHT].setBrightness(playing ? 1.f : 0.f);
			captureFlash = std::max(0.f, captureFlash - dt);
			lights[CAPTURE_LIGHT].setBrightness(captureFlash > 0.f ? 1.f : 0.f);
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "sampleAndHold", json_boolean(sampleAndHold));
		json_object_set_new(root, "outsLeaveMix", json_boolean(outsLeaveMix));
		json_t* bk = json_array();
		for (int b = 0; b < kNumBanks; b++) {
			json_t* row = json_array();
			for (int v = 0; v < kBankNotes; v++) json_array_append_new(row, json_integer(banks.notes[b][v]));
			json_array_append_new(bk, row);
		}
		json_object_set_new(root, "banks", bk);
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "sampleAndHold")) sampleAndHold = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "outsLeaveMix")) outsLeaveMix = json_boolean_value(j);
		if (json_t* bk = json_object_get(root, "banks")) {
			for (int b = 0; b < kNumBanks; b++) {
				json_t* row = json_array_get(bk, b);
				for (int v = 0; v < kBankNotes && row; v++) {
					json_t* n = json_array_get(row, v);
					if (n) banks.notes[b][v] = clamp((int)json_integer_value(n), 0, 127);
				}
			}
		}
	}
};

// ---------------------------------------------------------------- display

static const NVGcolor D_BG = nvgRGB(0x16, 0x14, 0x12);
static const NVGcolor D_INK = nvgRGB(0xF2, 0xEC, 0xE0);
static const NVGcolor D_SOFT = nvgRGB(0x80, 0x78, 0x6E);
static const NVGcolor D_BOX = nvgRGB(0x2A, 0x25, 0x21);
static const NVGcolor D_ACCENT = nvgRGB(0xF0, 0x8A, 0x3C);
static const NVGcolor D_CV = nvgRGB(0x6C, 0xC4, 0xB4);

struct CopainsDisplay : Widget {
	SixCopains* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) { Widget::drawLayer(args, layer); return; }
		NVGcontext* vg = args.vg;
		const float W = box.size.x, H = box.size.y;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, W, H, mm2px(1.4f));
		nvgFillColor(vg, D_BG);
		nvgFill(vg);
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font || font->handle < 0) { Widget::drawLayer(args, layer); return; }
		nvgFontFaceId(vg, font->handle);

		const float pad = mm2px(1.3f);
		const int bank = module ? module->shownBank : 0;
		const bool playing = module ? module->params[SixCopains::PLAY_PARAM].getValue() > 0.5f : false;

		// header: bank and clock
		nvgFontSize(vg, mm2px(2.4f));
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		nvgFillColor(vg, D_ACCENT);
		nvgText(vg, pad, pad, string::f("BANK %d", bank + 1).c_str(), NULL);
		std::string clk;
		if (!module) clk = "150 ms";
		else if (module->inputs[SixCopains::CLOCK_INPUT].isConnected()) clk = "ext clock";
		else clk = string::f("%d ms", (int)std::round(20.f * std::pow(100.f, module->params[SixCopains::RATE_PARAM].getValue())));
		nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		nvgFillColor(vg, playing ? D_INK : D_SOFT);
		nvgText(vg, W - pad, pad, (playing ? clk : std::string("stopped")).c_str(), NULL);

		// the bank's six notes, lit when they fire
		const float boxW = (W - 2 * pad - 5 * mm2px(0.6f)) / 6.f;
		const float rowY = pad + mm2px(3.6f), rowH = mm2px(4.4f);
		for (int v = 0; v < kBankNotes; v++) {
			const float x = pad + v * (boxW + mm2px(0.6f));
			const int note = module ? module->banks.notes[bank][v] : copains::kDefaultBanks[0][v];
			float glow = 0.f;
			if (module && playing) glow = clamp(1.f - (float)module->banks.sinceFired(v) / 0.15f, 0.f, 1.f);
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x, rowY, boxW, rowH, mm2px(0.6f));
			nvgFillColor(vg, nvgLerpRGBA(D_BOX, D_ACCENT, glow));
			nvgFill(vg);
			nvgFontSize(vg, mm2px(2.5f));
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, glow > 0.5f ? D_BG : D_INK);
			nvgText(vg, x + boxW / 2, rowY + rowH / 2, noteName(note).c_str(), NULL);
		}

		// six voices: who plays each (CV input or banks) and how loud
		const float barY = rowY + rowH + mm2px(1.2f), barH = H - barY - pad - mm2px(2.2f);
		const int cv = module ? module->cvCount : 0;
		for (int v = 0; v < kNumVoices; v++) {
			const float x = pad + v * (boxW + mm2px(0.6f));
			const float lv = module ? clamp(module->voiceLevel[v], 0.f, 1.f) : 0.f;
			const NVGcolor col = v < cv ? D_CV : D_ACCENT;
			nvgBeginPath(vg);
			nvgRect(vg, x, barY, boxW, barH);
			nvgFillColor(vg, D_BOX);
			nvgFill(vg);
			if (lv > 0.001f) {
				nvgBeginPath(vg);
				nvgRect(vg, x, barY + barH * (1.f - lv), boxW, barH * lv);
				nvgFillColor(vg, col);
				nvgFill(vg);
			}
			nvgFontSize(vg, mm2px(1.9f));
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgFillColor(vg, v < cv ? D_CV : D_SOFT);
			nvgText(vg, x + boxW / 2, barY + barH + mm2px(0.3f), v < cv ? string::f("cv%d", v + 1).c_str() : string::f("%d", v + 1).c_str(), NULL);
		}
		Widget::drawLayer(args, layer);
	}
};

// ---------------------------------------------------------------- menu: bank notes

struct NoteQuantity : Quantity {
	SixCopains* module;
	int bank, voice;
	NoteQuantity(SixCopains* m, int b, int v) : module(m), bank(b), voice(v) {}
	void setValue(float value) override { module->banks.notes[bank][voice] = clamp((int)std::round(value), 0, 127); }
	float getValue() override { return (float)module->banks.notes[bank][voice]; }
	float getMinValue() override { return 0.f; }
	float getMaxValue() override { return 127.f; }
	float getDefaultValue() override { return (float)copains::kDefaultBanks[bank][voice]; }
	std::string getLabel() override { return string::f("Note %d", voice + 1); }
	std::string getDisplayValueString() override { return noteName(module->banks.notes[bank][voice]); }
};

struct NoteSlider : ui::Slider {
	NoteSlider(SixCopains* m, int b, int v) {
		quantity = new NoteQuantity(m, b, v);
		box.size.x = 180.f;
	}
	~NoteSlider() { delete quantity; }
};

// ---------------------------------------------------------------- widget

struct SixCopainsWidget : ModuleWidget {
	SixCopainsWidget(SixCopains* module) {
		using namespace layout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SixCopains.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		auto knob = [&](float x, float y, int p) { addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x, y)), module, p)); };
		auto trim = [&](float x, float y, int p) { addParam(createParamCentered<Trimpot>(mm2px(Vec(x, y)), module, p)); };
		auto in = [&](float x, float y, int i) { addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, y)), module, i)); };
		auto out = [&](float x, float y, int o) { addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(x, y)), module, o)); };

		// Sound
		knob(C1, R1, SixCopains::LEVEL_PARAM);
		knob(C2, R1, SixCopains::RAMP_PARAM);
		knob(C3, R1, SixCopains::CURVE_PARAM);
		knob(C4, R1, SixCopains::FM_RATIO_PARAM);
		knob(C5, R1, SixCopains::FM_INDEX_PARAM);
		trim(C1, R2, SixCopains::PAN_DRIFT_PARAM);
		in(C2, R2, SixCopains::RAMP_INPUT);
		in(C3, R2, SixCopains::CURVE_INPUT);
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(C4, R2)), module, SixCopains::OCTAVE_PARAM));
		in(C5, R2, SixCopains::FM_INPUT);

		// Envelope and morph
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(C1, R3)), module, SixCopains::ENV_MODE_PARAM));
		knob(C2, R3, SixCopains::SLOPE_PARAM);
		knob(C3, R3, SixCopains::TIME_PARAM);
		addParam(createParamCentered<CKSS>(mm2px(Vec(C4, R3)), module, SixCopains::RETRIG_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(C5, R3)), module, SixCopains::MORPH_PARAM));
		in(C2, R4, SixCopains::SLOPE_INPUT);
		in(C3, R4, SixCopains::TIME_INPUT);
		in(C5, R4, SixCopains::MORPH_INPUT);

		trim(C1, R5, SixCopains::RAMP_MOD_PARAM);
		trim(C2, R5, SixCopains::CURVE_MOD_PARAM);
		trim(C3, R5, SixCopains::FM_MOD_PARAM);
		trim(C4, R5, SixCopains::SLOPE_MOD_PARAM);
		trim(C5, R5, SixCopains::TIME_MOD_PARAM);

		for (int v = 0; v < kNumVoices; v++) trim(PAN_X[v], R6, SixCopains::PAN_PARAM + v);

		// Banks
		CopainsDisplay* d = createWidget<CopainsDisplay>(mm2px(Vec(DISP_X, DISP_Y)));
		d->box.size = mm2px(Vec(DISP_W, DISP_H));
		d->module = module;
		addChild(d);
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(mm2px(Vec(B1, BR1)), module,
			SixCopains::PLAY_PARAM, SixCopains::PLAY_LIGHT));
		in(B2, BR1, SixCopains::CLOCK_INPUT);
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(B3, BR1)), module,
			SixCopains::CAPTURE_PARAM, SixCopains::CAPTURE_LIGHT));
		knob(B1, BR2, SixCopains::RATE_PARAM);
		knob(B2, BR2, SixCopains::AMOUNT_PARAM);
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(B3, BR2)), module, SixCopains::BANK_PARAM));
		trim(B1, BR3, SixCopains::RAND_PARAM);
		in(B2, BR3, SixCopains::AMOUNT_INPUT);
		in(B3, BR3, SixCopains::BANK_INPUT);
		trim(B1, BR4, SixCopains::VEL_MIN_PARAM);
		trim(B2, BR4, SixCopains::VEL_MAX_PARAM);
		trim(B3, BR4, SixCopains::GATE_LEN_PARAM);
		out(BOUT_V_X, BR5, SixCopains::BANK_VOCT_OUTPUT);
		out(BOUT_G_X, BR5, SixCopains::BANK_GATE_OUTPUT);

		// Bottom row
		in(IN_X[0], RIO, SixCopains::VOCT_INPUT);
		in(IN_X[1], RIO, SixCopains::GATE_INPUT);
		in(IN_X[2], RIO, SixCopains::VEL_INPUT);
		for (int v = 0; v < kNumVoices; v++) {
			out(OUT_X[v], RIO, SixCopains::VOICE_OUTPUT + v);
			addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(OUT_X[v] + 3.6f, RIO - 6.3f)), module, SixCopains::VOICE_LIGHT + v));
		}
		out(MIX_L_X, RIO, SixCopains::LEFT_OUTPUT);
		out(MIX_R_X, RIO, SixCopains::RIGHT_OUTPUT);
	}

	void appendContextMenu(Menu* menu) override {
		SixCopains* m = getModule<SixCopains>();
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Sample & hold pitch on each gate", "", &m->sampleAndHold));
		menu->addChild(createBoolPtrMenuItem("Patched voice outputs leave the L/R mix", "", &m->outsLeaveMix));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Banks"));
		for (int b = 0; b < kNumBanks; b++) {
			menu->addChild(createSubmenuItem(string::f("Bank %d notes", b + 1), b == m->shownBank ? "(playing)" : "", [=](Menu* sub) {
				for (int v = 0; v < kBankNotes; v++) sub->addChild(new NoteSlider(m, b, v));
				sub->addChild(new MenuSeparator);
				sub->addChild(createSubmenuItem("Load a scale", "", [=](Menu* sub2) {
					for (int s = 0; s < kNumBanks; s++) {
						sub2->addChild(createMenuItem(copains::kDefaultBankNames[s], "", [=]() {
							for (int v = 0; v < kBankNotes; v++) m->banks.notes[b][v] = copains::kDefaultBanks[s][v];
						}));
					}
				}));
				sub->addChild(createMenuItem("Transpose up a semitone", "", [=]() {
					for (int v = 0; v < kBankNotes; v++) m->banks.notes[b][v] = std::min(127, m->banks.notes[b][v] + 1);
				}));
				sub->addChild(createMenuItem("Transpose down a semitone", "", [=]() {
					for (int v = 0; v < kBankNotes; v++) m->banks.notes[b][v] = std::max(0, m->banks.notes[b][v] - 1);
				}));
			}));
		}
		menu->addChild(createMenuItem("Reset all banks", "", [=]() { m->banks.resetBanks(); }));
	}
};

Model* modelSixCopains = createModel<SixCopains, SixCopainsWidget>("SixCopains");
