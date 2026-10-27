// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Baril
#pragma once
#include "engines/fm/FmEngine.h"
#include "engines/fm/PerformanceMidi.h"
#include "dsp/OutputGuard.h"
#include "dsp/FeedbackGuard.h"
#include "dsp/HowlGuard.h"
#include "fx/Texture.h"
#include "fx/Vapor.h"
#include "fx/RoomReverb.h"
#include "fx/Distort.h"
#include "fx/Delay.h"
#include "fx/BusMatch.h"
#include "seq/SeqPlayer.h"
#include <string>
#include <vector>

namespace usina::web {
constexpr int blockSize = 128;
struct Param { std::string id, name, group; float min, max, def, step, skew = 1; };
const std::vector<Param>& parameters();
std::string metadata();
// Linear ramps match the plugin's JUCE SmoothedValue in sample time.
struct Ramp {
    float value = 0, target = 0, increment = 0;
    int remaining = 0, length = 1;
    void init(float v, int samples) { value = target = v; remaining = 0; length = std::max(1, samples); }
    void set(float v) { if(v != target) { target=v; remaining=length; increment=(v-value)/length; } }
    float next() { if(remaining && --remaining == 0) value=target; else if(remaining) value+=increment; return value; }
};
class Instrument {
public:
    Instrument(double sampleRate, int oversampling);
    void set(int index, float value);
    void resetPatch();
    void commitPatch();
    void midi(int status, int a, int b);
    void route(int index, int source, int destination, float amount);
    void function(int index, const float* points, int count);
    void step(int lane, int index, int on, int note, int velocity, float probability, int ratchet, float gate);
    void chord(int lane, int index, const int* notes, int count, const float* durations = nullptr);
    void expression(int lane,int index,float accent,int late,int condition,int offset,bool inherit);
    void baseNote(int lane,int note);
    void lane(int index, int length, int direction, int mul, int div, bool muted);
    void globals(int resolution, float swing);
    void flushEdits();
    void control(int index, float value);
    void render(int count);
    float* output(int channel) { return out[channel].data(); }
    float* input(int channel) { return in[channel].data(); }
    int cursor(int lane) const { return seq.cursor(lane); }
    float meter(int which) const;
    fm::Engine engine;
    fm::Routes routes;
    std::vector<float> values;
private:
    float get(const char* id) const;
    float mod(const char* id, int dst, float min, float max, float skew = 1) const;
    void fx(int kind, float* l, float* r, int n, bool wet);
    int latency(int kind) const;
    void prepareFx();
    double fs, ppq = 0;
    int os, fbPos = 0;
    bool playing = false;
    fm::PerformanceMidi performanceMidi {engine};
    float inputEnv = 0;
    std::array<int,fm::kNumValues> patch;
    std::array<float,4> macros {};
    std::array<float,8> controls {};
    std::array<std::vector<float>,2> up, fbLine;
    std::array<std::array<float,blockSize>,2> out {}, in {};
    std::array<fm::Decimator,2> decim;
    std::array<FeedbackGuard,2> fbGuard;
    HowlGuard howl, howlIn;
    OutputGuard guard;
    fx::Texture texture;
    fx::Vapor vapor;
    fx::RoomReverb room;
    fx::Distort distort;
    fx::Delay delay;
    fx::Texture::Knobs tk;
    fx::Vapor::Knobs vk;
    fx::Distort::Settings dk;
    fx::Delay::Settings ek;
    std::array<bool,blockSize> texGate {}, vapGate {};
    std::array<float,blockSize> dryL {}, dryR {}, aL {}, aR {}, bL {}, bR {}, parL {}, parR {}, auxL {}, auxR {}, loopL {}, loopR {};
    std::array<fx::BusMatch,2> matchDry, matchPad;
    fx::BusMatch matchPar;
    std::array<Ramp,3> busGain;
    Ramp gain, inputGain, inputMix, inputFx, feedback, feedbackTime;
    seq::Player seq;
    ruidocore::Pattern pattern;
    std::array<seq::Player::ChordLane, seq::kLanes> chords {};
    std::array<seq::Event,1024> seqEvents;
    int dirtyLanes = 0;
};
}
