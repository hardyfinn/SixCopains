// SPDX-License-Identifier: GPL-3.0-or-later
//
// 6 Copains for VCV Rack - the Banks note player.
//
// After 6Copains Banks by Mark IJzerman (https://github.com/markijzerman/6copains),
// itself a port of his JF Banks script for Just Friends. On every clock tick
// each of the bank's six notes plays with Amount probability, at a random
// velocity, and is released after Gate length. VCV changes: an external clock
// input (the gate length then follows the measured clock period) and capture
// of a held chord into a bank. No VCV dependencies, so test/engine_test.cpp
// drives it directly.

#pragma once
#include <stdint.h>

namespace copains {

constexpr int kNumBanks = 8;
constexpr double kBanksMinInterval = 0.005;  // seconds
constexpr double kBanksMinGate = 0.005;
constexpr int kBankNotes = 6;

// Bank 1 is JF Banks' default (C major pentatonic); the others are starting points.
static const int kDefaultBanks[kNumBanks][kBankNotes] = {
    {60, 62, 64, 67, 69, 72},  // C major pentatonic
    {57, 60, 62, 64, 67, 69},  // A minor pentatonic
    {50, 57, 62, 64, 65, 69},  // D dorian
    {53, 60, 64, 67, 71, 76},  // F lydian
    {52, 55, 59, 64, 65, 71},  // E phrygian
    {60, 62, 64, 66, 68, 70},  // whole tone
    {48, 55, 62, 69, 76, 83},  // stacked fifths
    {72, 74, 76, 79, 81, 84},  // high pentatonic
};
static const char* const kDefaultBankNames[kNumBanks] = {
    "C major pentatonic", "A minor pentatonic", "D dorian", "F lydian",
    "E phrygian", "Whole tone", "Stacked fifths", "High pentatonic",
};

struct BanksSettings {
    float rateSeconds = 0.15f;  // internal clock interval
    float rand = 0.0f;          // 0..1: each interval varies by up to +/- this
    float amount = 0.4f;        // 0..1: chance of each note on a tick
    float velMin = 40.0f / 127.0f, velMax = 100.0f / 127.0f;  // 0..1
    float gate = 0.5f;          // 0.01..1 of the interval
    int bank = 0;               // 0..7
    bool externalClock = false;
};

class BanksPlayer {
public:
    int notes[kNumBanks][kBankNotes];

    struct Held {
        int note = 0;
        double off = 0.0;
        bool active = false;
    };

    BanksPlayer() {
        resetBanks();
        for (int v = 0; v < kBankNotes; ++v) fired_[v] = -1.0;
    }

    void resetBanks() {
        for (int b = 0; b < kNumBanks; ++b)
            for (int v = 0; v < kBankNotes; ++v) notes[b][v] = kDefaultBanks[b][v];
    }

    // Store up to six held notes in a bank. Fewer than six repeat an octave up,
    // so a three-note chord fills the bank as chord + chord an octave higher.
    void capture(int bank, const int* held, int count) {
        if (bank < 0 || bank >= kNumBanks || count <= 0) return;
        if (count > kBankNotes) count = kBankNotes;
        for (int v = 0; v < kBankNotes; ++v) {
            int n = held[v % count] + 12 * (v / count);
            while (n > 127) n -= 12;
            notes[bank][v] = n < 0 ? 0 : n;
        }
    }

    // Advance by dt seconds. noteOn(int note, float velocity) and noteOff(int note)
    // are called for each event. clockEdge is a rising edge on the clock input.
    template <class On, class Off>
    void step(double dt, bool playing, bool clockEdge, const BanksSettings& s, On noteOn, Off noteOff) {
        now_ += dt;
        if (!playing) {
            releaseAll(noteOff);
            nextTick_ = now_;  // pressing PLAY ticks straight away
            lastClock_ = -1.0;
            return;
        }
        for (int v = 0; v < kBankNotes; ++v) {
            Held& h = held_[v];
            if (h.active && now_ >= h.off) {
                noteOff(h.note);
                h.active = false;
            }
        }

        double interval;
        if (s.externalClock) {
            if (!clockEdge) return;
            if (lastClock_ >= 0.0) {
                const double p = now_ - lastClock_;
                clockPeriod_ = p < kBanksMinInterval ? kBanksMinInterval : (p > 10.0 ? 10.0 : p);
            }
            lastClock_ = now_;
            interval = clockPeriod_;
        } else {
            lastClock_ = -1.0;
            if (now_ < nextTick_) return;
            interval = s.rateSeconds;
            if (s.rand > 0.0f) interval *= 1.0 + (rng() * 2.0 - 1.0) * s.rand;
            if (interval < kBanksMinInterval) interval = kBanksMinInterval;
            nextTick_ = now_ + interval;
        }
        tick(interval, s, noteOn, noteOff);
    }

    template <class Off>
    void releaseAll(Off noteOff) {
        for (int v = 0; v < kBankNotes; ++v) {
            if (held_[v].active) {
                noteOff(held_[v].note);
                held_[v].active = false;
            }
        }
    }

    const Held& held(int v) const { return held_[v]; }
    // Seconds since bank note v last fired (large if never).
    double sinceFired(int v) const { return fired_[v] < 0.0 ? 1.0e9 : now_ - fired_[v]; }
    int lastBank() const { return lastBank_; }

private:
    template <class On, class Off>
    void tick(double interval, const BanksSettings& s, On noteOn, Off noteOff) {
        double gate = interval * s.gate;
        if (gate < kBanksMinGate) gate = kBanksMinGate;
        const int bank = s.bank < 0 ? 0 : (s.bank >= kNumBanks ? kNumBanks - 1 : s.bank);
        lastBank_ = bank;
        const float lo = s.velMin < s.velMax ? s.velMin : s.velMax;
        const float hi = s.velMin < s.velMax ? s.velMax : s.velMin;
        for (int v = 0; v < kBankNotes; ++v) {
            if (rng() >= s.amount) continue;
            Held& h = held_[v];
            if (h.active) noteOff(h.note);
            int note = notes[bank][v];
            note = note < 0 ? 0 : (note > 127 ? 127 : note);
            float vel = lo + (float)rng() * (hi - lo);
            if (vel < 1.0f / 127.0f) vel = 1.0f / 127.0f;
            noteOn(note, vel);
            h.note = note;
            h.off = now_ + gate;
            h.active = true;
            fired_[v] = now_;
        }
    }

    // Same LCG as JF Banks, 0..1.
    double rng() {
        rng_ = (rng_ * 1103515245u + 12345u) & 0x7FFFFFFFu;
        return (double)((rng_ >> 16) & 0x7FFF) / 32767.0;
    }

    Held held_[kBankNotes];
    double fired_[kBankNotes];
    double now_ = 0.0, nextTick_ = 0.0, lastClock_ = -1.0, clockPeriod_ = 0.15;
    uint32_t rng_ = 12345u;
    int lastBank_ = 0;
};

}  // namespace copains
