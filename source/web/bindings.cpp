// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Brandalise Baril
#include "engine.h"
#include "sequence-wire.h"
#include <memory>
#include <cerrno>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define API EMSCRIPTEN_KEEPALIVE
// AudioWorkletGlobalScope has no Web Crypto API. libc++ random_device is only used to seed musical
// randomness here: a browser-generated seed enters before construction. No cryptographic use or global shim.
static uint32_t entropyState = 0x9e3779b9u;
extern "C" int getentropy(void* destination, size_t length) {
    if(length>256){errno=EIO;return -1;}
    auto* bytes=static_cast<unsigned char*>(destination);
    for(size_t i=0;i<length;++i){entropyState^=entropyState<<13;entropyState^=entropyState>>17;entropyState^=entropyState<<5;bytes[i]=(unsigned char)entropyState;}
    return 0;
}
#else
#define API
#endif
namespace { std::unique_ptr<usina::web::Instrument> instrument; float functionPoints[64*3] {}; int chordNotes[16] {}; std::vector<float> sampleStage; }
namespace { float sequenceWire[usina::web::sequenceWireSize] {}; }
namespace { float chordDurations[16] {}; }
extern "C" {
API void web_seed(unsigned value) {
#ifdef __EMSCRIPTEN__
    entropyState=value?value:0x9e3779b9u;
#else
    (void)value;
#endif
}
API const char* web_schema() {static std::string s=usina::web::metadata();return s.c_str();}
API int web_init(double rate,int oversampling) {
    if(!std::isfinite(rate)||rate<22050||rate>192000)return 0;
    if(oversampling!=1&&oversampling!=2&&oversampling!=4&&oversampling!=8&&oversampling!=16)return 0;
    instrument.reset();
    instrument=std::make_unique<usina::web::Instrument>(rate,oversampling);return 1;
}
API void web_dispose() {instrument.reset();}
API void web_set(int index,float value) {if(instrument)instrument->set(index,value);}
API void web_reset() {if(instrument)instrument->resetPatch();}
API void web_commit() {if(instrument)instrument->commitPatch();}
API void web_midi(int status,int a,int b) {if(instrument)instrument->midi(status,a,b);}
API void web_route(int index,int src,int dst,float amount) {if(instrument)instrument->route(index,src,dst,amount);}
API void web_step(int lane,int step,int on,int note,int vel,float prob,int ratchet,float gate) {if(instrument)instrument->step(lane,step,on,note,vel,prob,ratchet,gate);}
API int* web_chord_buffer() {return chordNotes;}
API void web_chord(int lane,int step,int count) {if(instrument)instrument->chord(lane,step,chordNotes,count);}
API float* web_duration_buffer() {return chordDurations;}
API void web_chord_durations(int lane,int step,int count) {if(instrument)instrument->chord(lane,step,chordNotes,count,chordDurations);}
API void web_step_expression(int lane,int step,float accent,int late,int condition,int offset,int inherit) {if(instrument)instrument->expression(lane,step,accent,late,condition,offset,inherit!=0);}
API void web_lane_base(int lane,int note) {if(instrument)instrument->baseNote(lane,note);}
API float* web_sequence_buffer() {return sequenceWire;}
API int web_quantize(int type,int root,int note) {if(type<0||type>31)return std::clamp(note,0,127);return usina::seq::midiQuantize({(ruidocore::ScaleType)type,std::clamp(root,0,11)},note);}
API int web_sequence_command(int lane,int operation,int pulses,int rotation,float amount,unsigned seed) {return usina::web::generateSequence(sequenceWire,lane,operation,pulses,rotation,amount,seed)?1:0;}
API void web_lane(int lane,int length,int direction,int mul,int div,int mute) {if(instrument)instrument->lane(lane,length,direction,mul,div,mute!=0);}
API void web_globals(int resolution,float swing) {if(instrument)instrument->globals(resolution,swing);}
API void web_flush_edits() {if(instrument)instrument->flushEdits();}
API float* web_function_buffer() {return functionPoints;}
API void web_function(int index,int count) {if(instrument)instrument->function(index,functionPoints,count);}
// SAMPLE (engines/fm/FmSample.h): the recording's room (n floats; the memory may grow), then handed to the engine
// (n 0: none). One thread here, the worklet's: handed over between two blocks
API float* web_sample_buffer(int n) {if(n<1||n>192000*30)return nullptr;try{sampleStage.assign((size_t)n,0.0f);}catch(...){return nullptr;}return sampleStage.data();}
API void web_sample(int n,double rate) {
    if(!instrument)return;
    if(n<1||n>(int)sampleStage.size()||!(rate>=8000&&rate<=192000)){instrument->engine.setSample(nullptr);instrument->engine.collectSample();return;}
    auto s=std::make_unique<usina::fm::SampleData>();s->rate=rate;s->data.assign(sampleStage.begin(),sampleStage.begin()+n);
    instrument->engine.setSample(std::move(s));instrument->engine.collectSample();
    std::vector<float>().swap(sampleStage);
}
API float* web_input(int channel) {return instrument&&channel>=0&&channel<2?instrument->input(channel):nullptr;}
API float* web_output(int channel) {return instrument&&channel>=0&&channel<2?instrument->output(channel):nullptr;}
API void web_render(int count) {if(instrument)instrument->render(count);}
API int web_cursor(int lane) {return instrument&&lane>=0&&lane<usina::seq::kLanes?instrument->cursor(lane):-1;}
API float web_meter(int index) {return instrument?instrument->meter(index):0;}
API float web_source(int index) {return instrument?instrument->engine.liveSource(index):0;}
API void web_control(int source,float value) {
    if(instrument)instrument->control(source,value);
}
}
