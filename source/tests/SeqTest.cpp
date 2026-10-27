// SPDX-License-Identifier: GPL-3.0-only
// The sequencer's player (src/seq/SeqPlayer.h), measured (rule 25), driven as a host drives it: blocks of random sizes,
// the host's beat position advancing with them (and looping, jumping, stopping):
//   TIMING    sixteenths at 120 BPM, 48 kHz: every note-on on its exact sample (k x 6000), its note-off half a step later
//   PAIRS     every note-on has its note-off - through stops, loops and jumps
//   LOOP      the host looping one bar: every pass plays the same notes from the bar's first step
//   JUMP      started in the middle (beat 2): the first note is step 8's
//   RATCHET   3 on a step: three notes, a third of a step apart
//   PROB      probability 0: never; 0.5: about half
//   EDIT      a lane changed while it plays: heard from the next block, its cursor and timing undisturbed
//   LANES     a modulation lane's value = its step's value (velocity / 127), held through the steps that are off
//   REAL-TIME no allocation and no free while it plays and is edited (counted; a Release build: MSVC's debug iterators
//             allocate on every container move)
#include "seq/SeqPlayer.h"
#include "seq/SeqCommands.h"
#include "../web/sequence-wire.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

static long allocations = 0, frees = 0;
void* operator new(std::size_t n)
{
    ++allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept
{
    if (p) { ++frees; std::free(p); }
}
void operator delete(void* p, std::size_t) noexcept
{
    if (p) { ++frees; std::free(p); }
}

namespace {
using namespace usina::seq;
int failures = 0;
void check(bool ok, const std::string& what, double got, const char* want)
{
    std::printf("%-60s %s  %12.3f  (want %s)\n", what.c_str(), ok ? "ok  " : "FAIL", got, want);
    failures += ok ? 0 : 1;
}

struct Note {
    long long at;                                        // absolute sample
    bool on;
    int note, velocity;
};

// a host: blocks of random sizes, the position (ppq) advancing at bpm; loopBeats > 0: the position loops over that many
// beats; startBeat: where the transport starts
struct Host {
    double fs = 48000.0, bpm = 120.0, ppq = 0.0, loopBeats = 0.0;
    long long sample = 0;
    std::mt19937 rng { 3 };
    std::vector<Note> got;
    void run(Player& p, double seconds, bool playing = true)
    {
        Event ev[256];
        const long long end = sample + (long long)(seconds * fs);
        std::uniform_int_distribution<int> sz(1, 700);
        while (sample < end) {
            const int n = (int)std::min<long long>(sz(rng), end - sample);
            Transport t;
            t.bpm = bpm;
            t.ppq = ppq;
            t.ppqValid = true;
            t.playing = playing;
            const int k = p.process(n, t, ev, 256);
            for (int i = 0; i < k; ++i) got.push_back({ sample + ev[i].sample, ev[i].on, ev[i].note, ev[i].velocity });
            sample += n;
            if (playing) {
                ppq += n / fs * bpm / 60.0;
                if (loopBeats > 0.0 && ppq >= loopBeats) ppq -= loopBeats;
            }
        }
    }
};

ruidocore::Pattern notes16(int stepsOn = 16)
{
    auto p = defaultPattern();
    auto& lane = p.parts[kNoteLane];
    for (int i = 0; i < 16; ++i) {
        lane.steps[(size_t)i].enabled = i < stepsOn;
        lane.steps[(size_t)i].explicitNote = 48 + i;
        lane.steps[(size_t)i].gateLength = 0.5f;
    }
    return p;
}

bool paired(const std::vector<Note>& v)
{
    std::map<int, int> open;
    for (const auto& n : v) {
        if (n.on) ++open[n.note];
        else if (--open[n.note] < 0) return false;
    }
    for (const auto& kv : open)
        if (kv.second != 0) return false;
    return true;
}
} // namespace

int main(int argc,char** argv)
{
    if(argc==3 && std::strcmp(argv[1],"--export-commands")==0){
        FILE* file=std::fopen(argv[2],"wb");if(!file)return 2;
        for(int operation=0;operation<6;++operation){
            std::array<float,usina::web::sequenceWireSize> wire{};
            for(int l=0;l<kLanes;++l){float* row=wire.data()+l*usina::web::sequenceLaneSize;row[0]=16;row[6]=-1;row[2]=row[3]=1;row[5]=(float)(l>=5?kDrumBaseNotes[l-5]:60);
                for(int i=0;i<64;++i){float* v=row+8+i*12;v[0]=i%3==0?1.0f:0.0f;v[1]=(float)(60+i%7);v[2]=(float)(80+i%30);v[3]=.75f;v[4]=1;v[5]=.5f;v[6]=.2f;}
            }
            if(!usina::web::generateSequence(wire.data(),0,operation,5,3,.4f,11)){std::fclose(file);return 3;}
            std::fwrite(wire.data(),sizeof(float),wire.size(),file);
        }
        std::fclose(file);return 0;
    }
    {   // TIMING and PAIRS
        Player p;
        p.prepare(48000.0);
        p.setPatternNow(notes16());
        Host h;
        h.run(p, 4.0);
        h.run(p, 0.1, false);                            // (stopped: every note ends)
        int wrong = 0, ons = 0;
        long long expect = 0;
        for (const auto& n : h.got)
            if (n.on) {
                wrong += n.at != expect ? 1 : 0;
                expect += 6000;
                ++ons;
            }
        check(ons == 32 && wrong == 0, "TIMING 32 sixteenths in 4 s, on their samples (wrong)", wrong, "0");
        int offWrong = 0;
        for (size_t i = 0; i + 1 < h.got.size(); ++i)
            if (h.got[i].on && !(!h.got[i + 1].on && h.got[i + 1].note == h.got[i].note && h.got[i + 1].at == h.got[i].at + 3000))
                ++offWrong;
        check(offWrong == 0, "TIMING each note-off half a step after its note-on (wrong)", offWrong, "0");
        check(paired(h.got), "PAIRS every note-on has its note-off (stop included)", 1, "1");
    }
    {   // LOOP: one bar looped; every pass from step 0
        Player p;
        p.prepare(48000.0);
        p.setPatternNow(notes16());
        Host h;
        h.loopBeats = 4.0;
        h.run(p, 6.0);
        h.run(p, 0.1, false);
        std::vector<int> seq;
        for (const auto& n : h.got)
            if (n.on) seq.push_back(n.note);
        int wrong = 0;
        for (size_t i = 0; i < seq.size(); ++i) wrong += seq[i] != 48 + (int)(i % 16) ? 1 : 0;
        check(seq.size() >= 40 && wrong == 0, "LOOP every pass from the bar's first step (wrong notes)", wrong, "0");
        check(paired(h.got), "LOOP pairs kept across the loop's jumps", 1, "1");
    }
    {   // JUMP: started at beat 2
        Player p;
        p.prepare(48000.0);
        p.setPatternNow(notes16());
        Host h;
        h.ppq = 2.0;
        h.run(p, 0.5);
        const int first = h.got.empty() ? -1 : h.got.front().note;
        check(first == 48 + 8 && h.got.front().at == 0, "JUMP started at beat 2: the first note is step 8's", first, "56");
    }
    {   // RATCHET
        auto pat = notes16(1);
        pat.parts[kNoteLane].steps[0].ratchet = 3;
        Player p;
        p.prepare(48000.0);
        p.setPatternNow(pat);
        Host h;
        h.run(p, 0.12);
        std::vector<long long> at;
        for (const auto& n : h.got)
            if (n.on) at.push_back(n.at);
        check(at.size() == 3 && at[1] == 2000 && at[2] == 4000, "RATCHET 3: three notes a third of a step apart", (double)at.size(), "3");
    }
    {   // PROB
        for (const float pr : { 0.0f, 0.5f }) {
            auto pat = notes16();
            for (auto& s : pat.parts[kNoteLane].steps) s.probability = pr;
            Player p;
            p.prepare(48000.0);
            p.setPatternNow(pat);
            Host h;
            h.run(p, 20.0);
            int ons = 0;
            for (const auto& n : h.got) ons += n.on ? 1 : 0;
            const double share = ons / 160.0;
            check(pr == 0.0f ? ons == 0 : std::fabs(share - 0.5) < 0.1, "PROB " + std::to_string(pr) + ": share of the steps", share,
                  pr == 0.0f ? "0" : "0.5 +- 0.1");
        }
    }
    {   // DRUMS (22/10): the four drum lanes play their base notes with their steps' strength, alongside the note lane;
        // a muted lane is silent
        auto pat = notes16(1);                           // (the note lane: step 0 only, note 48)
        auto& d2 = pat.parts[(size_t)(kFirstDrumLane + 1)];
        d2.steps[0].enabled = true;
        d2.steps[0].velocity = 40;
        d2.steps[4].enabled = true;
        d2.steps[4].velocity = 127;
        auto& d4 = pat.parts[(size_t)(kFirstDrumLane + 3)];
        d4.steps[8].enabled = true;
        Player p;
        p.prepare(48000.0);
        p.setPatternNow(pat);
        p.setMuted(kFirstDrumLane + 3, true);
        Host h;
        h.run(p, 1.9);                                   // (just under a bar: each step once)
        h.run(p, 0.1, false);
        int drumWeak = 0, drumStrong = 0, melody = 0, muted = 0;
        for (const auto& n : h.got)
            if (n.on) {
                drumWeak += n.note == kDrumBaseNotes[1] && n.velocity == 40 && n.at == 0 ? 1 : 0;
                drumStrong += n.note == kDrumBaseNotes[1] && n.velocity == 127 && n.at == 4 * 6000 ? 1 : 0;
                melody += n.note == 48 && n.at == 0 ? 1 : 0;
                muted += n.note == kDrumBaseNotes[3] ? 1 : 0;
            }
        check(drumWeak == 1 && drumStrong == 1 && melody == 1 && muted == 0,
              "DRUMS weak + strong hits on their steps, the melody beside, the muted lane silent", drumWeak + drumStrong + melody,
              "3");
        check(paired(h.got), "DRUMS pairs", 1, "1");
    }
    {   // EDIT while playing, and REAL-TIME
        Player p;
        p.prepare(48000.0);
        p.setPatternNow(notes16());
        Host h;
        h.run(p, 1.0);                                   // (warmed: the buffers have their sizes)
        auto lane = notes16().parts[kNoteLane];
        for (auto& s : lane.steps) s.explicitNote = 72;
        p.submitLane(kNoteLane, lane);                   // (message thread: the copy made here)
        const size_t before = h.got.size();
        h.got.reserve(before + 10000);                   // (the test host's own record: grown before the count)
        const long a0 = allocations, f0 = frees;
        h.run(p, 1.0);                                   // (the audio thread: takes it, plays it)
        const long a1 = allocations, f1 = frees;
        int newNotes = 0, wrongTime = 0;
        for (size_t i = before; i < h.got.size(); ++i)
            if (h.got[i].on) {
                newNotes += h.got[i].note == 72 ? 1 : 0;
                wrongTime += h.got[i].at % 6000 != 0 ? 1 : 0;
            }
        check(newNotes == 8 && wrongTime == 0, "EDIT the new notes from the next block, on the grid (of 8)", newNotes, "8");
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
        std::printf("SKIP REAL-TIME (debug iterators: %ld allocations; run the Release build)\n", a1 - a0);
#else
        check(a1 == a0 && f1 == f0, "REAL-TIME allocations + frees while playing and taking the edit", (double)(a1 - a0 + f1 - f0), "0");
#endif
        p.collect();
    }
    {   // LANES
        auto pat = defaultPattern();
        auto& m = pat.parts[1];                          // (SEQ 1)
        m.steps[0].enabled = true;
        m.steps[0].velocity = 127;
        m.steps[4].enabled = true;
        m.steps[4].velocity = 32;
        Player p;
        p.prepare(48000.0);
        p.setPatternNow(pat);
        Host h;
        h.run(p, 0.05);                                  // (step 0 fired; a step is 0.125 s)
        const float v0 = p.laneValue(0);
        h.run(p, 0.40);                                  // (to 0.45 s: steps 1..3 off, held)
        const float v1 = p.laneValue(0);
        h.run(p, 0.10);                                  // (to 0.55 s: step 4, at 0.5 s)
        const float v2 = p.laneValue(0);
        check(v0 == 1.0f && v1 == 1.0f && std::fabs(v2 - 32.0f / 127.0f) < 1e-6f, "LANES the step's value, held through the off steps", v2, "0.252");
    }
    {
        Player p; p.prepare(48000); auto pattern=notes16(1); p.setPatternNow(pattern);
        Player::ChordLane chords {}; chords[0].count=3; chords[0].notes[0]=60; chords[0].notes[1]=64; chords[0].notes[2]=67;
        p.submitLane(0,pattern.parts[0],&chords); Host h; h.run(p,.10); h.run(p,.01,false);
        int ons=0; bool pitches=true; for(const auto& n:h.got)if(n.on){++ons;pitches &= n.note==60||n.note==64||n.note==67;}
        check(ons==3&&pitches&&paired(h.got),"CHORD three pitches, sample timing and paired offs",ons,"3 paired notes");
        chords[0].count=16;for(int i=0;i<16;++i){chords[0].notes[(size_t)i]=48+i;chords[0].durations[(size_t)i]=64;}
        p.submitLane(0,pattern.parts[0],&chords);Event events[256];Transport t;t.playing=true;t.bpm=120;t.ppqValid=false;
        const long alloc=allocations,freed=frees;const int n=p.process(128,t,events,256);
        // (MSVC's Debug iterator checking allocates a container proxy when the lane's vectors are swapped in -
        // exchangePart: measured 27/10, Debug 16 allocations, Release none. Not the plugin's: measured in Release)
#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
        const bool rt=true;std::printf("     (render allocations not measurable with MSVC's iterator debugging: see the Release run)\n");
#else
        const bool rt=allocations==alloc&&frees==freed;
#endif
        check(n==16&&rt,"CHORD sixteen-note edit has no render allocations",n,"16 / zero allocs");
        t.playing=false;const int off=p.process(128,t,events,256);
        check(off==16,"CHORD stop closes every held pitch",off,"16 offs");
        p.setPatternNow(pattern);Host legacy;legacy.run(p,.10);legacy.run(p,.01,false);int mono=0;for(const auto& e:legacy.got)if(e.on)++mono;
        check(mono==1&&paired(legacy.got),"CHORD legacy pattern load clears chord overlays",mono,"1 paired note");
    }
    {
        Player p;p.prepare(48000);auto pattern=notes16(1);auto& lane=pattern.parts[0];lane.length=2;auto& e=lane.steps[0];e.velocity=30;e.accentAmount=1;e.microtimingMs=12;e.gateLength=1;usina::seq::setCondition(e,1);p.setPatternNow(pattern);Host h;h.run(p,.9);h.run(p,.02,false);
        std::vector<Note> on;for(const auto& n:h.got)if(n.on)on.push_back(n);
        check(on.size()==2&&on[0].at==576&&on[1].at==24576,"EXPRESSION condition 1:2 and LATE 12ms on exact samples",on.size(),"2 at 576 / 24576");
        check(on.size()==2&&on[0].velocity==127&&paired(h.got),"EXPRESSION accent raises velocity, gate 100% stays paired",on.empty()?0:on[0].velocity,"127 and paired");
        uint32_t seed=1;auto generated=notes16();usina::seq::command(generated,0,usina::seq::Command::Euclid,5,3,.4f,seed);int hits=0;for(int i=0;i<16;++i)hits+=generated.parts[0].steps[(size_t)i].enabled;
        check(hits==5&&seed==1,"COMMAND Euclid has five pulses without consuming seed",hits,"5");
        auto first=generated.parts[0];usina::seq::command(generated,0,usina::seq::Command::Left,0,0,0,seed);usina::seq::command(generated,0,usina::seq::Command::Right,0,0,0,seed);bool same=true;for(int i=0;i<64;++i)same &= generated.parts[0].steps[(size_t)i].explicitNote==first.steps[(size_t)i].explicitNote&&generated.parts[0].steps[(size_t)i].enabled==first.steps[(size_t)i].enabled;
        check(same,"COMMAND rotation inverse preserves steps past length",same,"same pattern");
        usina::seq::command(generated,0,usina::seq::Command::Random,0,0,1,seed);hits=0;for(int i=0;i<16;++i)hits+=generated.parts[0].steps[(size_t)i].enabled;
        check(hits==16&&seed==2,"COMMAND RANDOM at 100% draws ON, never inverts old bits",hits,"16 / seed 2");
        usina::seq::command(generated,0,usina::seq::Command::Clear,0,0,0,seed);hits=0;for(const auto& step:generated.parts[0].steps)hits+=step.enabled;check(hits==0,"COMMAND CLEAR disables all 64 steps",hits,"0");
    }
    {   // Independent note lengths must not change any legacy gate or lose paired offs across steps.
        auto pattern=notes16(1);pattern.parts[0].steps[0].explicitNote=60;
        Player::ChordLane chords {};auto& chord=chords[0];chord.count=3;chord.notes={60,64,67};chord.durations={2.5f,.75f,0};
        Player p;p.prepare(48000);p.setPatternNow(pattern);p.submitLane(0,pattern.parts[0],&chords);Host h;h.run(p,.5);h.run(p,.01,false);
        long long end60=-1,end64=-1,end67=-1;for(const auto& e:h.got)if(!e.on){if(e.note==60)end60=e.at;if(e.note==64)end64=e.at;if(e.note==67)end67=e.at;}
        check(end60==15000&&end64==4500&&end67==3000&&paired(h.got),"DURATION independent chord tails cross steps, legacy gate inherited",end60,"15000 / 4500 / 3000 paired");
        Player tempo;tempo.prepare(48000);tempo.setPatternNow(pattern);tempo.submitLane(0,pattern.parts[0],&chords);Host th;th.run(tempo,.125);th.bpm=60;th.run(tempo,.6);th.run(tempo,.01,false);end60=-1;for(const auto& e:th.got)if(!e.on&&e.note==60)end60=e.at;
        check(end60==24000&&paired(th.got),"DURATION tempo changes preserve remaining musical length",end60,"24000 paired");
        pattern.parts[0].steps[0].microtimingMs=50;Player delayed;delayed.prepare(48000);delayed.setPatternNow(pattern);delayed.submitLane(0,pattern.parts[0],&chords);Host mh;mh.run(delayed,.025);mh.bpm=60;mh.run(delayed,.8);mh.run(delayed,.01,false);end60=-1;for(const auto& e:mh.got)if(!e.on&&e.note==60)end60=e.at;
        check(end60==32400&&paired(mh.got),"DURATION tempo before LATE onset scales only the note length",end60,"32400 paired");pattern.parts[0].steps[0].microtimingMs=0;
        pattern.parts[0].clockDivider=2;Player divided;divided.prepare(48000);divided.setPatternNow(pattern);divided.submitLane(0,pattern.parts[0],&chords);Host dh;dh.run(divided,1);dh.run(divided,.01,false);long long begin=-1;end60=-1;for(const auto& e:dh.got)if(e.note==60){if(e.on)begin=e.at;else end60=e.at;}
        check(end60-begin==30000&&paired(dh.got),"DURATION respects the lane clock divider",end60-begin,"30000 paired");
        pattern.parts[0].clockDivider=1;pattern.parts[0].steps[1].enabled=true;pattern.parts[0].steps[1].explicitNote=60;Player repeat;repeat.prepare(48000);repeat.setPatternNow(pattern);repeat.submitLane(0,pattern.parts[0],&chords);Host rh;rh.run(repeat,.5);rh.run(repeat,.01,false);
        check(paired(rh.got),"DURATION same-pitch retrigger closes the previous tail",paired(rh.got),"paired");
        Player stopped;stopped.prepare(48000);stopped.setPatternNow(pattern);stopped.submitLane(0,pattern.parts[0],&chords);Host sh;sh.run(stopped,.05);sh.run(stopped,.01,false);check(paired(sh.got),"DURATION stop flushes independent and inherited tails",paired(sh.got),"paired");
        Player jumped;jumped.prepare(48000);jumped.setPatternNow(pattern);jumped.submitLane(0,pattern.parts[0],&chords);Host jh;jh.run(jumped,.05);jh.ppq=2;jh.run(jumped,.05);jh.run(jumped,.01,false);check(paired(jh.got),"DURATION host seek flushes independent tails",paired(jh.got),"paired");
    }
    std::printf("\n%s (%d failed)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
