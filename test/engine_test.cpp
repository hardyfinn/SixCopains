// engine_test.cpp: checks for CopainsEngine.hpp and BanksPlayer.hpp. No VCV needed.
//   c++ -std=c++11 -O2 -o test/engine_test test/engine_test.cpp && test/engine_test
// With a file name it also renders a demo: test/engine_test demo.wav
#include "../src/CopainsEngine.hpp"
#include "../src/BanksPlayer.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace copains;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static const float SR = 48000.f;
static const int N = 8;

struct Run {
	std::vector<float> L, R, V[kNumVoices];
};

static void render(Engine& e, int frames, Run& out, unsigned mask = 0x3Fu, bool stereo = true) {
	float l[N], r[N], v[kNumVoices][kMaxChunk];
	for (int f = 0; f < frames; f += N) {
		e.render(l, stereo ? r : nullptr, N, v, mask);
		for (int i = 0; i < N; i++) {
			out.L.push_back(l[i]);
			out.R.push_back(stereo ? r[i] : 0.f);
			for (int k = 0; k < kNumVoices; k++) out.V[k].push_back(v[k][i]);
		}
	}
}

static float peak(const std::vector<float>& x, size_t from = 0, size_t to = (size_t)-1) {
	float p = 0.f;
	for (size_t i = from; i < x.size() && i < to; i++) p = std::max(p, std::fabs(x[i]));
	return p;
}

static bool finite(const std::vector<float>& x) {
	for (float f : x) if (!std::isfinite(f)) return false;
	return true;
}

// Frequency from upward zero crossings over a steady stretch.
static float frequency(const std::vector<float>& x, size_t from, size_t to) {
	int first = -1, last = -1, count = 0;
	for (size_t i = from + 1; i < to; i++) {
		if (x[i - 1] <= 0.f && x[i] > 0.f) {
			if (first < 0) first = (int)i;
			last = (int)i;
			count++;
		}
	}
	if (count < 2) return 0.f;
	return (count - 1) * SR / (float)(last - first);
}

int main(int argc, char** argv) {
	// One CV voice at A4 (note 69) with a sine curve and a long ASR: 440 Hz on its own output.
	{
		Engine e;
		e.init(SR);
		e.params.envMode = kModeASR;
		e.params.time = 0.05f;
		e.params.ramp = 0.f;
		e.params.curve = 1.f;
		e.params.fmIndex = 0.f;
		e.params.morph = 0.f;
		e.setCvVoiceCount(2);
		e.cvVoiceOn(0, 69.f, 1.f);
		Run r;
		render(e, 24000, r);
		float f = frequency(r.V[0], 6000, 24000);
		printf("A4 voice: %.2f Hz, voice peak %.3f, mix peak %.3f\n", f, peak(r.V[0]), peak(r.L));
		CHECK(std::fabs(f - 440.f) < 2.f);
		CHECK(peak(r.V[0]) > 0.1f);
		CHECK(peak(r.L) > 0.05f);
		CHECK(peak(r.V[1]) == 0.f);  // voice 2 untouched
		CHECK(e.voiceLevel(0) > 0.9f);
		// Gate off: ASR releases to silence.
		e.cvVoiceOff(0);
		Run r2;
		render(e, 24000, r2);
		CHECK(peak(r2.V[0], 20000) < 1e-4f);
		CHECK(e.voiceLevel(0) == 0.f);
	}

	// Two voices, each with its own gate: voice 2 starts later and stops on its own.
	{
		Engine e;
		e.init(SR);
		e.params.envMode = kModeASR;
		e.params.time = 0.02f;
		e.setCvVoiceCount(2);
		e.cvVoiceOn(0, 60.f, 1.f);
		Run r;
		render(e, 4800, r);
		CHECK(peak(r.V[1]) == 0.f);
		e.cvVoiceOn(1, 67.f, 1.f);
		render(e, 4800, r);
		CHECK(peak(r.V[1], 4800) > 0.1f);
		e.cvVoiceOff(1);
		render(e, 9600, r);
		CHECK(peak(r.V[1], 4800 + 9600 - 1000) < 1e-4f);  // voice 2 released
		CHECK(peak(r.V[0], 4800 + 9600 - 1000) > 0.1f);   // voice 1 still held
	}

	// Mix mask: a voice taken out of the mix still plays on its own output.
	{
		Engine e;
		e.init(SR);
		e.params.envMode = kModeASR;
		e.setCvVoiceCount(1);
		e.cvVoiceOn(0, 60.f, 1.f);
		Run r;
		render(e, 4800, r, 0x3Eu);
		CHECK(peak(r.L) == 0.f && peak(r.R) == 0.f);
		CHECK(peak(r.V[0]) > 0.1f);
	}

	// Mono: R unrendered, L carries the voice.
	{
		Engine e;
		e.init(SR);
		e.params.envMode = kModeASR;
		e.setCvVoiceCount(1);
		e.cvVoiceOn(0, 60.f, 1.f);
		Run r;
		render(e, 4800, r, 0x3Fu, false);
		CHECK(peak(r.L) > 0.1f);
	}

	// Extreme settings stay finite and bounded: six voices, full FM, overlap, cycle.
	{
		Engine e;
		e.init(SR);
		e.params.fmIndex = 1.f;
		e.params.curve = -1.f;
		e.params.ramp = 1.f;
		e.params.envMode = kModeCycle;
		e.params.time = 0.003f;
		e.params.overlap = true;
		e.params.level = 1.f;
		Run r;
		for (int k = 0; k < 40; k++) {
			e.noteOn(30 + k * 2, 1.f);
			render(e, 480, r);
		}
		CHECK(finite(r.L) && finite(r.R));
		printf("extreme: L peak %.3f\n", peak(r.L));
		CHECK(peak(r.L) < 3.f);
		e.params.fmIndex = -1.f;
		for (int k = 0; k < 6; k++) e.noteOff(30 + (34 + k) * 2);
		render(e, 48000, r);
		CHECK(finite(r.L));
	}

	// Banks with the internal clock: amount 100 % fires every note on every tick.
	{
		BanksPlayer b;
		BanksSettings s;
		s.amount = 1.01f;
		s.rateSeconds = 0.1f;
		s.gate = 0.5f;
		int ons = 0, offs = 0;
		double dt = 1.0 / SR;
		for (int i = 0; i < 48000; i++)
			b.step(dt, true, false, s, [&](int, float v) { ons++; CHECK(v > 0.f && v <= 1.f); }, [&](int) { offs++; });
		printf("banks internal: %d note-ons, %d note-offs in 1 s at 100 ms\n", ons, offs);
		CHECK(ons == 60);  // ticks at 0, 0.1 .. 0.9
		CHECK(offs >= 54 && offs <= 60);
		// Stopping releases everything still held.
		int stopOffs = 0;
		b.step(dt, false, false, s, [&](int, float) {}, [&](int) { stopOffs++; });
		CHECK(offs + stopOffs == ons);
		for (int v = 0; v < kBankNotes; v++) CHECK(!b.held(v).active);
	}

	// Banks with amount 0 never play; with an external clock it ticks only on edges.
	{
		BanksPlayer b;
		BanksSettings s;
		s.amount = 0.f;
		int ons = 0;
		for (int i = 0; i < 48000; i++) b.step(1.0 / SR, true, false, s, [&](int, float) { ons++; }, [](int) {});
		CHECK(ons == 0);
		s.amount = 1.01f;
		s.externalClock = true;
		for (int i = 0; i < 48000; i++) b.step(1.0 / SR, true, i % 12000 == 0, s, [&](int, float) { ons++; }, [](int) {});
		CHECK(ons == 24);  // 4 edges x 6 notes
	}

	// The bank knob picks the notes; capture fills a bank, repeating a short chord an octave up.
	{
		BanksPlayer b;
		BanksSettings s;
		s.amount = 1.01f;
		s.bank = 6;  // stacked fifths
		std::vector<int> notes;
		b.step(1.0 / SR, true, false, s, [&](int n, float) { notes.push_back(n); }, [](int) {});
		CHECK(notes.size() == 6 && notes[0] == 48 && notes[5] == 83);
		int chord[3] = {60, 63, 67};
		b.capture(2, chord, 3);
		CHECK(b.notes[2][0] == 60 && b.notes[2][2] == 67 && b.notes[2][3] == 72 && b.notes[2][5] == 79);
		int high[2] = {120, 125};
		b.capture(3, high, 2);
		for (int v = 0; v < kBankNotes; v++) CHECK(b.notes[3][v] <= 127);
	}

	// Banks into the engine: notes go to the voices CV does not use.
	{
		Engine e;
		e.init(SR);
		e.setCvVoiceCount(4);
		BanksPlayer b;
		BanksSettings s;
		s.amount = 1.01f;
		Run r;
		float l[N], rr[N], v[kNumVoices][kMaxChunk];
		for (int f = 0; f < 4800; f += N) {
			for (int i = 0; i < N; i++)
				b.step(1.0 / SR, true, false, s, [&](int n, float vel) { e.noteOn(n, vel); }, [&](int n) { e.noteOff(n); });
			e.render(l, rr, N, v);
			for (int k = 0; k < kNumVoices; k++) r.V[k].insert(r.V[k].end(), v[k], v[k] + N);
		}
		for (int k = 0; k < 4; k++) CHECK(peak(r.V[k]) == 0.f);
		CHECK(peak(r.V[4]) > 0.01f && peak(r.V[5]) > 0.01f);
	}

	// Demo: 20 s of Banks through nb_monamie's three patches, with morph sweeping.
	if (argc > 1) {
		Engine e;
		e.init(SR);
		BanksPlayer b;
		BanksSettings s;
		s.rateSeconds = 0.125f;  // 16ths at 120 BPM
		s.amount = 0.35f;
		const int frames = (int)SR * 24;
		std::vector<short> pcm;
		float l[N], r[N];
		for (int f = 0; f < frames; f += N) {
			float t = f / SR;
			int part = (int)(t / 8.f);
			if (part == 0) {  // default
				e.params.envMode = kModeAR; e.params.ramp = 0.08f; e.params.curve = 1.f; e.params.time = 0.21f;
				e.params.slope = 0.03f; e.params.fmRatio = 1.01f; e.params.fmIndex = 0.f; s.bank = 0;
			} else if (part == 1) {  // ciclette
				e.params.envMode = kModeCycle; e.params.ramp = 0.f; e.params.curve = 0.52f; e.params.time = 0.26f;
				e.params.slope = 0.15f; e.params.fmRatio = 1.5f; e.params.fmIndex = -0.05f; e.params.rampMod = 0.72f;
				e.params.fmMod = -0.4f; e.params.panDrift = 0.48f; s.bank = 2; s.gate = 0.9f;
			} else {  // soufflet
				e.params.envMode = kModeASR; e.params.ramp = 0.52f; e.params.curve = -0.48f; e.params.time = 1.51f;
				e.params.slope = 0.67f; e.params.fmRatio = 0.97f; e.params.fmIndex = 0.f; e.params.rampMod = -0.24f;
				e.params.curveMod = 0.59f; e.params.fmMod = 0.2f; s.bank = 6; s.rateSeconds = 0.25f; s.gate = 0.6f;
			}
			e.params.morph = 0.5f - 0.5f * std::cos(t / 8.f * 6.2831853f);
			for (int i = 0; i < N; i++)
				b.step(1.0 / SR, true, false, s, [&](int n, float vel) { e.noteOn(n, vel); }, [&](int n) { e.noteOff(n); });
			e.render(l, r, N);
			for (int i = 0; i < N; i++) {
				pcm.push_back((short)std::max(-32767.f, std::min(32767.f, l[i] * 2.f * 32767.f * 0.5f)));
				pcm.push_back((short)std::max(-32767.f, std::min(32767.f, r[i] * 2.f * 32767.f * 0.5f)));
			}
		}
		FILE* fp = fopen(argv[1], "wb");
		if (fp) {
			unsigned dataBytes = (unsigned)pcm.size() * 2, sr = (unsigned)SR, byteRate = sr * 4, riff = 36 + dataBytes, fmtLen = 16;
			unsigned short fmt = 1, chans = 2, align = 4, bits = 16;
			fwrite("RIFF", 1, 4, fp); fwrite(&riff, 4, 1, fp); fwrite("WAVEfmt ", 1, 8, fp);
			fwrite(&fmtLen, 4, 1, fp); fwrite(&fmt, 2, 1, fp); fwrite(&chans, 2, 1, fp); fwrite(&sr, 4, 1, fp);
			fwrite(&byteRate, 4, 1, fp); fwrite(&align, 2, 1, fp); fwrite(&bits, 2, 1, fp);
			fwrite("data", 1, 4, fp); fwrite(&dataBytes, 4, 1, fp); fwrite(pcm.data(), 2, pcm.size(), fp);
			fclose(fp);
			printf("wrote %s\n", argv[1]);
		}
	}

	if (fails) { printf("%d check(s) failed\n", fails); return 1; }
	printf("all engine checks passed\n");
	return 0;
}
