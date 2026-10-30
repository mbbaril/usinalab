// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <ruidocore/Mutation/Mutation.h>
#include <ruidocore/Rhythm/Euclidean.h>
#include <algorithm>
#include <array>
#include <random>

namespace usina::seq {
inline int midiQuantize(const ruidocore::Scale& scale,int note) {
    const int quantized=ruidocore::quantizeSemitone(scale,std::clamp(note,0,127));
    if(quantized<0){for(int n=0;n<12;++n)if(ruidocore::quantizeSemitone(scale,n)==n)return n;}
    if(quantized>127){for(int n=127;n>115;--n)if(ruidocore::quantizeSemitone(scale,n)==n)return n;}
    return std::clamp(quantized,0,127);
}
enum class Command { Euclid, Random, Mutate, Clear, Left, Right };
inline constexpr std::array<const char*,11> conditionNames { "ALL", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "1ST" };
inline constexpr int conditionK[] {1,1,2,1,2,3,1,2,3,4,1}, conditionN[] {1,2,2,3,3,3,4,4,4,4,1};
inline void setCondition(ruidocore::StepEvent& e,int i) {
    i=std::clamp(i,0,10);e.condition.type=i==0?ruidocore::TrigConditionType::Always:i==10?ruidocore::TrigConditionType::FirstLoop:ruidocore::TrigConditionType::Ratio;
    e.condition.ratioK=conditionK[i];e.condition.ratioN=conditionN[i];
}
inline int conditionIndex(const ruidocore::TrigCondition& c) {
    if(c.type==ruidocore::TrigConditionType::FirstLoop)return 10;
    if(c.type==ruidocore::TrigConditionType::Ratio)for(int i=1;i<10;++i)if(c.ratioK==conditionK[i]&&c.ratioN==conditionN[i])return i;
    return 0;
}
// Edit-time commands; never call from render or processBlock.
inline void command(ruidocore::Pattern& pattern,int lane,Command command,int pulses,int rotation,float amount,uint32_t& seed) {
    if(lane<0||lane>=(int)pattern.parts.size())return;
    auto& part=pattern.parts[(size_t)lane];if(part.steps.empty())return;const int n=std::clamp(part.length,1,(int)part.steps.size());
    switch(command){
    case Command::Euclid: {
        const auto mask=ruidocore::euclideanRhythm(n,std::clamp(pulses,0,n),rotation%n);
        for(int i=0;i<n&&i<(int)mask.size();++i)part.steps[(size_t)i].enabled=mask[(size_t)i];break;
    }
    case Command::Random: {
        std::mt19937 rng(seed++*2654435761u);std::uniform_real_distribution<float> u(0.0f,1.0f);
        static constexpr int scale[]{0,3,5,7,10,12,15};
        for(int i=0;i<n;++i){auto& e=part.steps[(size_t)i];e.enabled=u(rng)<amount;
            if(lane==0){e.explicitNote=std::clamp(part.baseNote+scale[(int)(u(rng)*7.0f)%7],0,127);e.velocity=70+(int)(u(rng)*57.0f);}
            else e.velocity=(int)(u(rng)*127.0f);
        }break;
    }
    case Command::Mutate: {
        ruidocore::MutationParams mp;mp.amount=amount;mp.seed=seed++;mp.locks.part.assign(pattern.parts.size(),true);mp.locks.part[(size_t)lane]=false;
        ruidocore::MutationProfile profile;profile.pitchTarget=lane==0?ruidocore::MutationProfile::PitchTarget::SemitoneOffset:ruidocore::MutationProfile::PitchTarget::None;
        const auto mutated=ruidocore::Mutation::mutate(pattern,mp,profile);part=mutated.parts[(size_t)lane];break;
    }
    case Command::Clear: for(auto& e:part.steps)e.enabled=false;break;
    case Command::Left: case Command::Right: {
        const int by=command==Command::Left?-1:1;const auto before=part.steps;
        for(int i=0;i<n;++i)part.steps[(size_t)((i+by+n)%n)]=before[(size_t)i];break;
    }
    }
}
}
