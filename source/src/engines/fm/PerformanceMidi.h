// SPDX-License-Identifier: GPL-3.0-only
// MIDI 1/MPE lower-zone performance state. No JUCE, allocation, locks or patch mutation.
#pragma once
#include "FmEngine.h"
#include <array>
#include <bitset>
#include <algorithm>
namespace usina::fm {
class PerformanceMidi {
public:
    explicit PerformanceMidi(Engine& target) : engine(target) { reset(); }
    int heldNotes() const {
        int n=0;for(int c=0;c<16;++c)n+=(int)(held[c]|deferred[c]).count();return n;
    }
    void reset() {
        legacyBendChannel=0;
        for(auto& h:held)h.reset();for(auto& d:deferred)d.reset();
        sustain.fill(false);bend.fill(0);rpnMsb.fill(127);rpnLsb.fill(127);
        for(int c=0;c<16;++c){sensitivity[c]=enabled&&c>0?memberRange:masterRange;engine.setChannelPitch(c,0);engine.setChannelPressure(c,0);engine.setChannelTimbre(c,.5);}
        at=mw=expr=0;engine.setControllers(0,0,0);engine.setTimbre(.5);engine.setPerformancePitch(0);
    }
    void configure(bool mpe, double globalRange, double noteRange) {
        globalRange=std::clamp(globalRange,1.0,96.0);noteRange=std::clamp(noteRange,1.0,96.0);
        if(mpe!=enabled){engine.allNotesOff();enabled=mpe;members=mpe?15:0;masterRange=globalRange;memberRange=noteRange;reset();return;}
        if(globalRange!=masterRange||noteRange!=memberRange){
            masterRange=globalRange;memberRange=noteRange;
            for(int c=0;c<16;++c)sensitivity[c]=enabled&&c>0?memberRange:masterRange;
            // In legacy mode every channel shares one bend. Updating all 16 in order let unused channels
            // overwrite the active bend with zero when the range changed. MPE retains independent channels.
            if(enabled){for(int c=0;c<16;++c)updatePitch(c);}else updatePitch(legacyBendChannel);
        }
    }
    void process(int status,int a,int b) {
        if(status<0x80||status>=0xf0||a<0||a>127||b<0||b>127)return;
        const int c=status&15,k=status&0xf0;
        const bool member=enabled&&c>0&&c<=members;
        if(enabled&&c>members)return;
        // Master-channel notes remain the legacy keyboard (CABINE and the internal sequencer).
        if(k==0x90&&b>0){if(member){engine.allNotesOff(c);held[c].reset();deferred[c].reset();}held[c].set(a);deferred[c].reset(a);engine.noteOn(a,b,member?c:-1);return;}
        if(k==0x80||(k==0x90&&b==0)){held[c].reset(a);if(sustain[c]||(enabled&&sustain[0]))deferred[c].set(a);else engine.noteOff(a,member?c:enabled?-2:-1);return;}
        if(k==0xe0){const int value=a+(b<<7)-8192;bend[c]=value/(value>=0?8191.0:8192.0);updatePitch(c);return;}
        if(k==0xd0){if(member)engine.setChannelPressure(c,a/127.0);else{at=a/127.0;engine.setControllers(at,mw,expr);}return;}
        if(k!=0xb0)return;
        if(a==101){rpnMsb[c]=b;return;}if(a==100){rpnLsb[c]=b;return;}
        if(a==6&&rpnMsb[c]==0&&rpnLsb[c]==0){sensitivity[c]=std::clamp((double)b,0.0,96.0);updatePitch(c);return;}
        if(a==38&&rpnMsb[c]==0&&rpnLsb[c]==0){sensitivity[c]=std::floor(sensitivity[c])+std::min(b,99)/100.0;updatePitch(c);return;}
        if(a==6&&c==0&&rpnMsb[c]==0&&rpnLsb[c]==6&&enabled){engine.allNotesOff();members=std::clamp(b,0,15);reset();return;}
        if(a==120||a==123){if(member){engine.allNotesOff(c);held[c].reset();deferred[c].reset();sustain[c]=false;}else{engine.allNotesOff();reset();}return;}
        if(a==121){bend[c]=0;updatePitch(c);if(member){engine.setChannelPressure(c,0);engine.setChannelTimbre(c,.5);}else{at=mw=expr=0;engine.setControllers(0,0,0);engine.setTimbre(.5);}sustain[c]=false;releaseDeferred();return;}
        if(a==64){sustain[c]=b>=64;releaseDeferred();return;}
        if(a==74){if(member)engine.setChannelTimbre(c,b/127.0);else engine.setTimbre(b/127.0);return;}
        if(!member){if(a==1)mw=b/127.0;if(a==11)expr=b/127.0;engine.setControllers(at,mw,expr);}
    }
private:
    void updatePitch(int c){const double oct=bend[c]*sensitivity[c]/12.0;if(enabled&&c>0)engine.setChannelPitch(c,oct);else{if(!enabled)legacyBendChannel=c;engine.setPerformancePitch(oct);}}
    void releaseDeferred(){for(int c=0;c<16;++c)if(!sustain[c]&&!(enabled&&sustain[0]))for(int n=0;n<128;++n)if(deferred[c].test(n)&&!held[c].test(n)){engine.noteOff(n,enabled?(c>0?c:-2):-1);deferred[c].reset(n);}}
    Engine& engine;
    bool enabled=false;int members=0,legacyBendChannel=0;
    double masterRange=2,memberRange=48,at=0,mw=0,expr=0;
    std::array<double,16> bend{},sensitivity{};
    std::array<int,16> rpnMsb{},rpnLsb{};
    std::array<bool,16> sustain{};
    std::array<std::bitset<128>,16> held{},deferred{};
};
}
