// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "seq/SeqCommands.h"
#include "seq/SeqPlayer.h"
#include <cmath>
namespace usina::web {
constexpr int sequenceStepSize=12,sequenceLaneSize=8+64*sequenceStepSize,sequenceWireSize=seq::kLanes*sequenceLaneSize;
inline bool generateSequence(float* wire,int lane,int operation,int pulses,int rotation,float amount,uint32_t seed) {
    if(lane<0||lane>=seq::kLanes||operation<0||operation>6||!std::isfinite(amount))return false;
    for(int i=0;i<sequenceWireSize;++i)if(!std::isfinite(wire[i]))return false;
    auto pattern=seq::defaultPattern();
    for(int l=0;l<seq::kLanes;++l){auto& p=pattern.parts[(size_t)l];const float* row=wire+l*sequenceLaneSize;
        p.length=std::clamp((int)row[0],1,64);p.direction=(ruidocore::Direction)std::clamp((int)row[1],0,3);p.clockMultiplier=std::clamp((int)row[2],1,16);p.clockDivider=std::clamp((int)row[3],1,16);p.baseNote=std::clamp((int)row[5],0,127);
        for(int i=0;i<64;++i){auto& e=p.steps[(size_t)i];const float* v=row+8+i*sequenceStepSize;e.enabled=v[0]!=0;e.explicitNote=v[10]!=0?ruidocore::kNoteUnset:std::clamp((int)v[1],0,127);e.velocity=std::clamp((int)v[2],0,127);e.probability=std::clamp(v[3],0.0f,1.0f);e.ratchet=std::clamp((int)v[4],1,8);e.gateLength=std::clamp(v[5],.01f,1.0f);e.accentAmount=std::clamp(v[6],0.0f,1.0f);e.microtimingMs=std::clamp((int)v[7],-100,100);seq::setCondition(e,(int)v[8]);e.pitchSemitoneOffset=std::clamp((int)v[9],-48,48);}
    }
    if(operation<6)seq::command(pattern,lane,(seq::Command)operation,std::clamp(pulses,0,64),std::clamp(rotation,0,63),std::clamp(amount,0.0f,1.0f),seed);
    const float* selected=wire+lane*sequenceLaneSize;
    if(selected[6]>=0&&selected[6]<32&&(operation==1||operation==2||operation==6)){
        auto& part=pattern.parts[(size_t)lane];const ruidocore::Scale scale{(ruidocore::ScaleType)(int)selected[6],std::clamp((int)selected[7],0,11)};
        for(int i=0;i<part.length;++i){auto& e=part.steps[(size_t)i];const int base=ruidocore::resolvedNote(e,part),quantized=seq::midiQuantize(scale,base+e.pitchSemitoneOffset);
            e.explicitNote=quantized;e.pitchSemitoneOffset=0;
        }
    }
    const auto& p=pattern.parts[(size_t)lane];float* row=wire+lane*sequenceLaneSize;
    for(int i=0;i<64;++i){const auto& e=p.steps[(size_t)i];float* v=row+8+i*sequenceStepSize;v[0]=e.enabled?1.0f:0.0f;v[1]=(float)ruidocore::resolvedNote(e,p);v[2]=(float)e.velocity;v[3]=e.probability;v[4]=(float)e.ratchet;v[5]=e.gateLength;v[6]=e.accentAmount;v[7]=(float)e.microtimingMs;v[8]=(float)seq::conditionIndex(e.condition);v[9]=(float)e.pitchSemitoneOffset;v[10]=e.explicitNote<0?1.0f:0.0f;}
    return true;
}
}
