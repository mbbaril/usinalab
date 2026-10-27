// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Baril
#include "engine.h"
#include <cstdio>
#include <cstdlib>
#include <new>
static bool tracking=false;
static unsigned allocations=0;
void* operator new(std::size_t n){if(tracking)++allocations;if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
int main(){
    {
        auto probe=std::make_unique<usina::web::Instrument>(48000,4);
        auto setting=[&](const char* id,float value){int i=0;for(const auto& p:usina::web::parameters()){if(p.id==id){probe->set(i,value);return;}++i;}std::abort();};
        setting("fx_on",0);probe->commitPatch();probe->midi(0x90,69,100);
        auto frequency=[&](){for(int b=0;b<80;++b)probe->render(128);int crossings=0;float previous=probe->output(0)[127];for(int b=0;b<160;++b){probe->render(128);for(int i=0;i<128;++i){float x=probe->output(0)[i];if(previous<=0&&x>0)++crossings;previous=x;}}return crossings*48000.0/(160*128);};
        if(std::abs(frequency()-440)>6)return 10;
        probe->midi(0xe0,127,127);if(std::abs(frequency()-493.883)>6)return 11;
        setting("bend_range",12);probe->commitPatch();if(std::abs(frequency()-880)>8)return 12;
        probe->midi(0xe0,0,0);if(std::abs(frequency()-220)>4)return 13;
        probe->midi(0xe0,0,64);if(std::abs(frequency()-440)>6)return 14;
        setting("op1_freq",1);setting("op1_fixed_hz",44000);probe->commitPatch();probe->midi(0xe0,127,127);
        if(std::abs(frequency()-440)>6)return 15;
        std::puts("Web-only global pitch bend: center, +/- endpoints, range changes and fixed-Hz exclusion: pass");
    }
    auto s=std::make_unique<usina::web::Instrument>(48000,4);
    auto set=[&](const char* id,float value){int i=0;for(const auto& p:usina::web::parameters()){if(p.id==id){s->set(i,value);return;}++i;}std::abort();};
    auto render=[&](int blocks){tracking=true;for(int k=0;k<blocks;++k){s->render(128);for(int c=0;c<2;++c)for(int i=0;i<128;++i)if(!std::isfinite(s->output(c)[i])||std::abs(s->output(c)[i])>1)std::abort();}tracking=false;};
    for(int mode=0;mode<=11;++mode){s->resetPatch();s->set(usina::fm::opIndex(0,usina::fm::OV_MODE),(float)mode);s->commitPatch();s->midi(0x90,60,100);render(4);s->midi(0x80,60,0);render(4);}
    s->resetPatch();s->midi(0xb0,64,127);s->midi(0x90,60,100);render(4);s->midi(0x80,60,0);render(20);
    if(s->meter(0)==0)return 2;
    s->midi(0xb0,64,0);render(800);if(s->meter(0)!=0)return 3;
    set("seq_play",1);s->step(0,0,1,60,100,1,3,.5f);s->flushEdits();render(60);
    s->step(0,0,1,64,100,1,3,.5f);s->flushEdits();render(60);set("seq_play",0);render(800);
    if(s->meter(0)!=0)return 4;
    for(int kind:{1,2,3,5,6}){set("fx_a_type",(float)kind);set("tex_on",1);set("vap_on",1);set("rev_amount",.5f);s->midi(0x90,60,100);render(10);s->midi(0x80,60,0);render(10);}
    std::printf("render allocations: %u; finite bounded output; sustain release; sequencer stop: pass\n",allocations);
    return allocations?5:0;
}
