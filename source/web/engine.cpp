// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Baril
#include "engine.h"
#include "seq/SeqCommands.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace usina::web {
namespace {
float db(float x) { return std::pow(10.0f,x/20.0f); }
std::string quote(const std::string& s) {
    std::string r="\"";
    for(char c:s) { if(c=='"'||c=='\\') r+='\\'; r+=c; }
    return r+'"';
}
}
const std::vector<Param>& parameters() {
    static const auto result=[] {
        std::vector<Param> p;
        for(int i=0;i<fm::kNumValues;++i) {
            const auto& s=fm::schema()[i];
            std::string group=i<162 ? "OP"+std::to_string(i/27+1) : i<256 ? "PATCH" : "MOD";
            p.push_back({s.id,s.name,group,(float)s.min,(float)s.max,(float)s.def,1});
        }
        auto a=[&](const char* id,const char* name,const char* group,float lo,float hi,float def,float step=.001f,float skew=1) {
            p.push_back({id,name,group,lo,hi,def,step,skew});
        };
        a("level","Saida (dB)","MASTER",-36,6,0,.1f);
        a("ceiling","Teto (dBTP)","MASTER",-24,-.3f,-1,.1f);
        a("in_gain","Ganho de entrada (dB)","INPUT",-36,24,0,.1f);
        a("in_mix","Entrada","INPUT",0,1,0);
        a("in_fx","Entrada para FX","INPUT",0,1,0,1);
        a("feedback","Realimentacao","INPUT",0,1.5f,0);
        a("fb_time","Tempo do laco (ms)","INPUT",1,2000,120,.1f,.35f);
        for(int i=1;i<=4;++i) p.push_back({"macro"+std::to_string(i),"MACRO "+std::to_string(i),"PERFORM",-1,1,0,.001f});
        a("seq_bpm","BPM","SEQ",20,300,120,.1f);
        a("seq_play","Tocar","SEQ",0,1,0,1);
        a("fx_on","FX","ROUTING",0,1,1,1);
        a("fx_route","A > B / A // B / B > A","ROUTING",0,2,0,1);
        const char* buses[]={"fx_a","fx_b","aux","loop"};
        const char* groups[]={"FX A","FX B","AUX","LOOP"};
        for(int b=0;b<4;++b) {
            for(int s=0;s<3;++s) {
                std::string id=std::string(buses[b])+(s?std::to_string(s+1):"")+"_type";
                p.push_back({id,"Slot "+std::to_string(s+1),groups[b],0,6,(float)(b==0&&s==0?1:b==1&&s==0?2:0),1});
            }
            p.push_back({b==3?"loop_fx_on":std::string(buses[b])+"_on","Ativo",groups[b],0,1,1,1});
            if(b<2) p.push_back({std::string(buses[b])+"_mix","Mix",groups[b],0,1,1,.001f});
        }
        a("aux_send","Envio","AUX",0,1,0);
        a("aux_pre","Pre FX","AUX",0,1,0,1);
        a("aux_return","Retorno (dB)","AUX",-36,6,0,.1f);
        const char* tex[]={"time","size","shape","pitch","time_amt","size_amt","shape_amt","pitch_amt","density","feedback","drywet","reverb","gain"};
        for(int i=0;i<13;++i) p.push_back({"tex_"+std::string(tex[i]),tex[i],"TEXTURE",0,1,i==8?.3f:i==9||i==11||i==12?0:.5f,.001f});
        for(auto id:{"tex_on","tex_freeze","tex_mono"}) a(id,id,"TEXTURE",0,1,0,1);
        a("tex_media","Digital / Vintage / Tape / Worn","TEXTURE",0,3,0,1);
        a("tex_seed","Internal / Notes / Notes div / Clock","TEXTURE",0,3,0,1);
        const char* vap[]={"position","size","pitch","density","texture","drywet","spread","feedback","reverb"};
        const float vapDefaults[]={.2f,.5f,.5f,.7f,.5f,.5f,.3f,0,.3f};
        for(int i=0;i<9;++i) p.push_back({"vap_"+std::string(vap[i]),vap[i],"VAPOR",0,1,vapDefaults[i],.001f});
        for(auto id:{"vap_on","vap_freeze","vap_reverse"}) a(id,id,"VAPOR",0,1,std::string(id)=="vap_on"?1:0,1);
        a("vap_mode","Granular / Stretch / Loop / Spectral / Reverb / Resonator","VAPOR",0,5,0,1);
        a("vap_quality","Stereo16 / Mono16 / Stereo8 / Mono8","VAPOR",0,3,0,1);
        a("vap_trig","Internal / Notes / Clock","VAPOR",0,2,1,1);
        a("rev_on","Ativo","ROOM",0,1,1,1);
        a("rev_amount","Espaco","ROOM",0,1,0);
        a("rev_color","Clean / Tape","ROOM",0,1,0,1);
        a("dist_on","Ativo","DISTORT",0,1,1,1);
        a("dist_mode","Saturate / Hard / Diode / Fuzz / Fold / Rectify / Crush / Bits / Compand","DISTORT",0,8,0,1);
        a("dist_drive","Drive (dB)","DISTORT",0,48,12,.1f);
        a("dist_char","Carater","DISTORT",0,1,.5f);
        a("dist_sym","Simetria","DISTORT",-1,1,0);
        a("dist_tone","Tonalidade","DISTORT",-1,1,0);
        a("dist_mix","Mix","DISTORT",0,1,1);
        a("dist_out","Saida (dB)","DISTORT",-24,12,0,.1f);
        a("dly_on","Ativo","DELAY",0,1,1,1);
        a("dly_time","Tempo (ms)","DELAY",1,4000,375,.1f,.3f);
        a("dly_feedback","Feedback","DELAY",0,1.1f,.4f);
        a("dly_lowcut","Low cut (Hz)","DELAY",20,2000,20,1,.3f);
        a("dly_highcut","High cut (Hz)","DELAY",500,20000,20000,1,.3f);
        a("dly_wow","Wow","DELAY",0,1,.3f);
        a("dly_flutter","Flutter","DELAY",0,1,.2f);
        a("dly_mix","Mix","DELAY",0,1,.35f);
        a("dly_out","Saida (dB)","DELAY",-24,12,0,.1f);
        a("dly_sync","Sync","DELAY",0,14,0,1);
        a("dly_character","Digital / Tape","DELAY",0,1,0,1);
        a("dly_routing","Stereo / Ping-pong / Mono","DELAY",0,2,0,1);
        a("dly_freeze","Freeze","DELAY",0,1,0,1);
        a("bend_range","Alcance do bend (semitons)","PERFORM",1,48,2,1);
        // Append only: old parameter indices and sessions remain valid.
        a("mpe_mode","MPE INPUT","PERFORM",0,1,0,1);
        a("mpe_bend_range","NOTE BEND (st)","PERFORM",1,96,48,1);
        return p;
    }();
    return result;
}
static const auto& paramMap() {
    static const auto m=[] { std::map<std::string,int,std::less<>> m; int i=0; for(const auto& p:parameters()) m.emplace(p.id,i++); return m; }();
    return m;
}
std::string metadata() {
    std::ostringstream s;
    s<<"{\"parameters\":[";
    for(size_t i=0;i<parameters().size();++i) { const auto& p=parameters()[i]; if(i)s<<',';
        s<<"{\"id\":"<<quote(p.id)<<",\"name\":"<<quote(p.name)<<",\"group\":"<<quote(p.group)
         <<",\"min\":"<<p.min<<",\"max\":"<<p.max<<",\"default\":"<<p.def<<",\"step\":"<<p.step<<",\"skew\":"<<p.skew<<"}"; }
    s<<"],\"sources\":[";
    for(int i=1;i<fm::kNumSources;++i) {if(i>1)s<<','; s<<"{\"index\":"<<i<<",\"id\":"<<quote(fm::modSourceId(i))<<",\"name\":"<<quote(fm::modSourceName(i))<<"}";}
    s<<"],\"destinations\":["; bool first=true;
    for(int i=0;i<fm::kNumValues+fm::kNumFxDst;++i) {int d=i<fm::kNumValues?i:fm::kFxDst+i-fm::kNumValues;
        if(!fm::isRouteTarget(d))continue;
        if(fm::isFxDst(d)&&paramMap().find(fm::fxDstParamId(d))==paramMap().end())continue;
        if(!first)s<<',';first=false;
        s<<"{\"index\":"<<d<<",\"id\":"<<quote(fm::isFxDst(d)?fm::fxDstParamId(d):fm::schema()[d].id)
         <<",\"name\":"<<quote(fm::routeTargetName(d))<<"}";}
    s<<"],\"models\":{";
    auto names=[&](const char* key,int count,auto name) { s<<quote(key)<<":["; for(int i=0;i<count;++i){if(i)s<<',';s<<quote(name(i));}s<<']'; };
    names("modal",fm::kModalStructures,fm::modalStructureName); s<<',';
    names("material",fm::kModalMaterials,fm::modalMaterialName); s<<',';
    names("exciter",fm::kModalExciters,fm::modalExciterName); s<<',';
    names("chaos",fm::kChaosModels,fm::chaosModelName); s<<',';
    names("stoch",fm::kStochDists,fm::stochDistName); s<<',';
    names("macro",fm::kMacroModels,fm::macroModelName);
    s<<"},\"vaporKnobs\":[";
    for(int m=0;m<fx::Vapor::kNumModes;++m){if(m)s<<',';s<<'[';for(int k=0;k<fx::Vapor::kNumKnobs;++k){if(k)s<<',';s<<quote(fx::Vapor::knobName(m,k));}s<<']';}
    s<<"],\"envMs\":[";for(int i=0;i<128;++i){if(i)s<<',';s<<fm::Tables::get().envMs[i];}
    s<<"],\"sequenceScales\":[";
    for(size_t i=0;i<ruidocore::kAllScaleTypes.size();++i){if(i)s<<',';const auto type=ruidocore::kAllScaleTypes[i];s<<"{\"name\":"<<quote(ruidocore::toString(type))<<",\"intervals\":[";const auto& intervals=ruidocore::intervalsFor(type);for(size_t j=0;j<intervals.size();++j){if(j)s<<',';s<<intervals[j];}s<<"]}";}
    s<<"],\"patchCount\":"<<fm::kNumValues<<"}"; return s.str();
}
float Instrument::get(const char* id) const {auto it=paramMap().find(id);return it==paramMap().end()?0:values[it->second];}
float Instrument::mod(const char* id,int dst,float lo,float hi,float skew) const {
    float normal=std::pow(std::clamp((get(id)-lo)/(hi-lo),0.0f,1.0f),skew);
    for(int i=0;i<routes.n;++i) if(routes.r[i].dst==fm::kFxDst+dst) normal+=routes.r[i].amount*engine.liveSource(routes.r[i].src);
    normal=std::clamp(normal,0.0f,1.0f);
    return lo+(hi-lo)*(normal>0?std::exp(std::log(normal)/skew):0);
}
Instrument::Instrument(double rate,int factor):fs(rate),os(factor),pattern(seq::defaultPattern()) {
    for(const auto& p:parameters())values.push_back(p.def);
    (void)paramMap();
    (void)fm::modalTable(); // Resolve the membrane's Bessel zeros before any note reaches the realtime callback.
    for(int c=0;c<2;++c) { up[c].resize(blockSize*os); decim[c].setFactor(os);fbLine[c].resize((size_t)(2*fs)+2);fbGuard[c].prepare(fs); }
    engine.prepare(fs*os,os==1,fs);
    guard.prepare(fs,blockSize); howl.prepare(fs);howlIn.prepare(fs);
    texture.prepare(fs,blockSize); vapor.prepare(fs,blockSize);room.prepare(fs);distort.prepare(fs);delay.prepare(fs);
    for(auto* m:{&matchDry[0],&matchDry[1],&matchPad[0],&matchPad[1],&matchPar})m->prepare(fs,std::max(4096,(int)std::ceil(.08*fs)));
    for(auto& b:busGain)b.init(1,(int)(fs*.02));
    gain.init(db(get("level")),(int)(fs*.03));inputGain.init(1,(int)(fs*.03));inputMix.init(0,(int)(fs*.03));inputFx.init(0,(int)(fs*.03));
    feedback.init(0,(int)(fs*.03));feedbackTime.init((float)(fs*.12),(int)(fs*.2));
    seq.prepare(fs);seq.setPatternNow(pattern);resetPatch();
}
void Instrument::set(int i,float v) {
    if(i<0||i>=(int)values.size()||!std::isfinite(v))return;
    const auto& p=parameters()[i];v=std::clamp(v,p.min,p.max);
    values[i]=p.step==1?std::round(v):v;
    if(i<fm::kNumValues)patch[i]=(int)values[i];
}
void Instrument::resetPatch() {
    for(int i=0;i<fm::kNumValues;++i)set(i,parameters()[i].def);
    routes.n=0;engine.setRoutes(routes);
    for(int i=0;i<3;++i)engine.setFunctionShape(i,fm::defaultFuncShape(i));
    commitPatch();
}
void Instrument::commitPatch() {
    performanceMidi.configure(get("mpe_mode")>.5f,get("bend_range"),get("mpe_bend_range"));
    int m[3];
    for(int i=0;i<4;++i)macros[i]=get(("macro"+std::to_string(i+1)).c_str());
    for(int i=0;i<3;++i)m[i]=std::clamp((int)std::round(macros[i]*64),-64,63);
    engine.setPatch(fm::Patch::fromArrayWithMacros(patch.data(),m));engine.setMacroSources(macros.data());
}
void Instrument::midi(int status,int a,int b) {
    performanceMidi.process(status,a,b);
}
void Instrument::route(int i,int src,int dst,float amount) {
    if(i<0||i>=fm::kMaxRoutes||!std::isfinite(amount))return;
    if(src<=0) {if(i<routes.n){for(int k=i;k<routes.n-1;++k)routes.r[k]=routes.r[k+1];--routes.n;}}
    else if(src<fm::kNumSources&&fm::isRouteTarget(dst)) {i=std::min(i,routes.n);routes.r[i]={src,dst,std::clamp(amount,-1.0f,1.0f)};routes.n=std::max(routes.n,i+1);}
    engine.setRoutes(routes);
}
void Instrument::function(int k,const float* points,int n) {
    if(k<0||k>=3||n<2||n>fm::kMaxFuncPoints)return;
    fm::FuncShape f;f.n=n;
    for(int i=0;i<n;++i){for(int j=0;j<3;++j)if(!std::isfinite(points[i*3+j]))return;
        f.p[i]={std::clamp(points[i*3],0.0f,1.0f),std::clamp(points[i*3+1],0.0f,1.0f),std::clamp(points[i*3+2],-1.0f,1.0f)};
        if(i&&f.p[i].x<f.p[i-1].x)return;}
    engine.setFunctionShape(k,f);
}
void Instrument::step(int l,int i,int on,int note,int velocity,float prob,int ratchet,float gate) {
    if(l<0||l>=seq::kLanes||i<0||i>=64||!std::isfinite(prob)||!std::isfinite(gate))return;
    auto& part=pattern.parts[l];ruidocore::ensureStepStorage(part);auto& e=part.steps[i];
    e.enabled=on!=0;e.explicitNote=std::clamp(note,0,127);e.velocity=std::clamp(velocity,1,127);
    e.probability=std::clamp(prob,0.0f,1.0f);e.ratchet=std::clamp(ratchet,1,8);e.gateLength=std::clamp(gate,.01f,1.0f);
    dirtyLanes|=1<<l;
}
void Instrument::lane(int l,int len,int dir,int mul,int div,bool mute) {
    if(l<0||l>=seq::kLanes)return;auto& p=pattern.parts[l];p.length=std::clamp(len,1,64);
    p.direction=(ruidocore::Direction)std::clamp(dir,0,3);p.clockMultiplier=std::clamp(mul,1,16);p.clockDivider=std::clamp(div,1,16);
    seq.setMuted(l,mute);dirtyLanes|=1<<l;
}
void Instrument::chord(int l,int i,const int* notes,int count,const float* durations) {
    if(!seq::isNoteLane(l)||l<0||l>=seq::kLanes||i<0||i>=64)return;
    auto& c=chords[(size_t)l][(size_t)i];c.count=0;
    for(int k=0;k<std::clamp(count,0,seq::Player::kChordNotes);++k){const int note=std::clamp(notes[k],0,127);bool duplicate=false;for(int j=0;j<c.count;++j)if(c.notes[(size_t)j]==note)duplicate=true;if(!duplicate){const int n=c.count++;c.notes[(size_t)n]=note;c.durations[(size_t)n]=durations&&std::isfinite(durations[k])&&durations[k]>0?std::clamp(durations[k],.01f,64.0f):0.0f;}}
    dirtyLanes|=1<<l;
}
void Instrument::expression(int l,int i,float accent,int late,int condition,int offset,bool inherit) {
    if(l<0||l>=seq::kLanes||i<0||i>=64||!std::isfinite(accent))return;
    auto& e=pattern.parts[(size_t)l].steps[(size_t)i];e.accentAmount=std::clamp(accent,0.0f,1.0f);e.microtimingMs=std::clamp(late,-100,100);e.pitchSemitoneOffset=std::clamp(offset,-48,48);seq::setCondition(e,condition);
    if(inherit)e.explicitNote=ruidocore::kNoteUnset;dirtyLanes|=1<<l;
}
void Instrument::baseNote(int l,int note) {if(l>=0&&l<seq::kLanes){pattern.parts[(size_t)l].baseNote=std::clamp(note,0,127);dirtyLanes|=1<<l;}}
void Instrument::globals(int resolution,float swing) {if(std::isfinite(swing))seq.setGlobals(std::clamp(resolution,1,16),std::clamp(swing,0.0f,75.0f));}
void Instrument::control(int i,float v) {if(i>=0&&i<8&&std::isfinite(v))controls[i]=std::clamp(v,0.0f,1.0f);}
void Instrument::flushEdits() {
    // Called by the worklet's message handler, outside render. The existing desktop mailbox can allocate on edits.
    seq.collect();
    for(int i=0;i<seq::kLanes;++i)if((dirtyLanes&(1<<i))&&seq.submitLane(i,pattern.parts[i],&chords[(size_t)i]))dirtyLanes&=~(1<<i);
}
void Instrument::prepareFx() {
    float* t[]={&tk.time,&tk.size,&tk.shape,&tk.pitch,&tk.timeAmt,&tk.sizeAmt,&tk.shapeAmt,&tk.pitchAmt,&tk.density,&tk.feedback,&tk.dryWet,&tk.reverb,&tk.gain};
    const char* ids[]={"tex_time","tex_size","tex_shape","tex_pitch","tex_time_amt","tex_size_amt","tex_shape_amt","tex_pitch_amt","tex_density","tex_feedback","tex_drywet","tex_reverb","tex_gain"};
    for(int i=0;i<13;++i)*t[i]=get(ids[i]);
    const char* ti[]={"tex_time","tex_size","tex_shape","tex_pitch","tex_density","tex_feedback","tex_drywet","tex_reverb"};
    float* td[]={&tk.time,&tk.size,&tk.shape,&tk.pitch,&tk.density,&tk.feedback,&tk.dryWet,&tk.reverb};
    for(int i=0;i<8;++i)*td[i]=mod(ti[i],i,0,1);
    tk.freeze=get("tex_freeze")>.5f;tk.mono=get("tex_mono")>.5f;tk.media=3-(int)get("tex_media");tk.seedMode=(int)get("tex_seed");
    float* v[]={&vk.position,&vk.size,&vk.pitch,&vk.density,&vk.texture,&vk.dryWet,&vk.spread,&vk.feedback,&vk.reverb};
    const char* vi[]={"vap_position","vap_size","vap_pitch","vap_density","vap_texture","vap_drywet","vap_spread","vap_feedback","vap_reverb"};
    for(int i=0;i<9;++i)*v[i]=mod(vi[i],fm::FD_VAP_POSITION+i,0,1);
    vk.freeze=get("vap_freeze")>.5f;vk.reverse=get("vap_reverse")>.5f;vk.mode=(int)get("vap_mode");vk.quality=(int)get("vap_quality");
    dk.driveDb=mod("dist_drive",fm::FD_DIST_DRIVE,0,48);dk.character=mod("dist_char",fm::FD_DIST_DRIVE+1,0,1);
    dk.symmetry=mod("dist_sym",fm::FD_DIST_DRIVE+2,-1,1);dk.tone=mod("dist_tone",fm::FD_DIST_DRIVE+3,-1,1);dk.mix=mod("dist_mix",fm::FD_DIST_DRIVE+4,0,1);
    dk.mode=(int)get("dist_mode");dk.outputDb=get("dist_out");
    ek.timeMs=mod("dly_time",fm::FD_DLY_TIME,1,4000,.3f);ek.feedback=mod("dly_feedback",fm::FD_DLY_TIME+1,0,1.1f);
    ek.lowCutHz=mod("dly_lowcut",fm::FD_DLY_TIME+2,20,2000,.3f);ek.highCutHz=mod("dly_highcut",fm::FD_DLY_TIME+3,500,20000,.3f);
    ek.wow=mod("dly_wow",fm::FD_DLY_TIME+4,0,1);ek.mix=mod("dly_mix",fm::FD_DLY_TIME+5,0,1);ek.flutter=mod("dly_flutter",fm::FD_DLY_TIME+6,0,1);
    ek.outputDb=get("dly_out");ek.sync=(int)get("dly_sync");ek.bpm=get("seq_bpm");ek.character=(int)get("dly_character");ek.routing=(int)get("dly_routing");ek.freeze=get("dly_freeze")>.5f;
}
int Instrument::latency(int k) const {return k==1&&texture.active()?texture.latencySamples():k==5&&vapor.active()?vapor.latencySamples():0;}
void Instrument::fx(int kind,float* l,float* r,int n,bool wet) {
    bool silent=false;
    switch(kind) {
    case 1:{auto k=tk;if(wet)k.dryWet=1;texture.process(l,r,n,k,tk.seedMode?texGate.data():nullptr,get("tex_on")>.5f);silent=!texture.active();break;}
    case 2:room.process(l,r,n,get("rev_on")>.5f?get("rev_amount"):0,(int)get("rev_color"),wet);return;
    case 3:{auto k=dk;if(wet)k.mix=1;distort.process(l,r,n,k,get("dist_on")>.5f);silent=get("dist_on")<.5f;break;}
    case 5:{auto k=vk;if(wet&&k.mode!=fx::Vapor::kResonator)k.dryWet=1;vapor.process(l,r,n,k,get("vap_trig")?vapGate.data():nullptr,get("vap_on")>.5f);silent=!vapor.active();break;}
    case 6:{auto k=ek;if(wet)k.mix=1;delay.process(l,r,n,k,get("dly_on")>.5f);silent=!delay.active();break;}
    default:silent=true;break; // IR remains unavailable: never claim a convolution that did not run.
    }
    if(wet&&silent){std::fill(l,l+n,0);std::fill(r,r+n,0);}
}
void Instrument::render(int n) {
    if(n<1||n>blockSize)return;
    bool play=get("seq_play")>.5f;if(play&&!playing)ppq=0;playing=play;
    const double bpm=get("seq_bpm");
    engine.setTransport(bpm,ppq,play);
    seq::Transport transport;transport.bpm=bpm;transport.ppq=ppq;transport.ppqValid=true;transport.playing=play;
    int count=seq.process(n,transport,seqEvents.data(),(int)seqEvents.size());
    float lanes[4];for(int i=0;i<4;++i)lanes[i]=seq.laneValue(i);engine.setSeqSources(lanes);engine.setCtrlSources(controls.data());
    for(int c=0;c<2;++c)std::fill(up[c].begin(),up[c].begin()+n*os,0);
    const int seedMode=(int)get("tex_seed"),trigMode=(int)get("vap_trig");
    int done=0,event=0;
    while(done<n) {
        while(event<count&&seqEvents[event].sample<=done) {auto& e=seqEvents[event++];midi(e.on?0x90:0x80,e.note,e.velocity);}
        int end=event<count?std::clamp(seqEvents[event].sample,done+1,n):n;
        engine.render(up[0].data()+done*os,up[1].data()+done*os,(end-done)*os);
        const bool held=performanceMidi.heldNotes()>0;
        for(int i=done;i<end;++i){double beat=ppq+i/fs*bpm/60;bool clock=play&&beat-std::floor(beat)<.1;texGate[i]=seedMode==3?clock:held;vapGate[i]=trigMode==2?clock:held;}
        done=end;
    }
    for(int c=0;c<2;++c)for(int i=0;i<n;++i)out[c][i]=decim[c].process(up[c].data()+i*os);
    if(play)ppq+=n/fs*bpm/60;
    prepareFx();
    inputGain.set(db(get("in_gain")));inputMix.set(get("in_mix"));inputFx.set(get("in_fx"));
    std::array<float,blockSize> ig{},im{};
    float peak=0;
    for(int i=0;i<n;++i){ig[i]=inputGain.next()*inputMix.next();im[i]=inputFx.next();float l=in[0][i]*ig[i]*im[i],r=in[1][i]*ig[i]*im[i];howlIn.process(l,r);out[0][i]+=l;out[1][i]+=r;peak=std::max(peak,std::max(std::abs(in[0][i]),std::abs(in[1][i])));}
    peak=std::min(1.0f,peak*db(get("in_gain")));float release=std::exp(-(float)n/(float)(.15*fs));
    inputEnv=peak>inputEnv?peak:peak+(inputEnv-peak)*release;engine.setInputLevel(inputEnv);
    int kinds[4][3]{};bool runs[4]{};bool seen[7]{};
    const char* ids[4][3]={{"fx_a_type","fx_a2_type","fx_a3_type"},{"fx_b_type","fx_b2_type","fx_b3_type"},{"aux_type","aux2_type","aux3_type"},{"loop_type","loop2_type","loop3_type"}};
    const char* onIds[]={"fx_a_on","fx_b_on","aux_on","loop_fx_on"};
    for(int b=0;b<4;++b){bool any=false;for(int s=0;s<3;++s){int k=get("fx_on")>.5f?(int)get(ids[b][s]):0;if(k==4)k=0;if(k&&seen[k])k=0;if(k)seen[k]=true;kinds[b][s]=k;any|=k!=0;}
        float target=get(onIds[b])>.5f?(b<2?get(b==0?"fx_a_mix":"fx_b_mix"):1):0;
        if(b<3){busGain[b].set(target);runs[b]=any&&(busGain[b].remaining||target>0);}else runs[b]=any&&target>0;}
    auto placed=[&](int k){for(int b=0;b<4;++b)for(int s=0;s<3;++s)if(runs[b]&&kinds[b][s]==k)return true;return false;};
    if(!placed(1)&&texture.active())texture.reset();if(!placed(2)&&room.active())room.reset();
    if(!placed(3)&&distort.active())distort.reset();if(!placed(5)&&vapor.active())vapor.reset();if(!placed(6)&&delay.active())delay.reset();
    auto run=[&](int b,float* l,float* r,bool wet){for(int s=0;s<3;++s)if(kinds[b][s])fx(kinds[b][s],l,r,n,wet);};
    int lat[2]{},dist[2]{};bool distPlaced=false;
    for(int b=0;b<2;++b)if(runs[b])for(int s=0;s<3;++s){lat[b]+=latency(kinds[b][s]);distPlaced|=kinds[b][s]==3;dist[b]|=kinds[b][s]==3&&distort.active()&&get("dist_on")>.5f;}
    auto apply=[&](int b,float* l,float* r){std::copy(l,l+n,dryL.data());std::copy(r,r+n,dryR.data());matchDry[b].process(dryL.data(),dryR.data(),n,lat[b],dist[b],fx::Distort::oversampled(dk.mode),distPlaced);if(!runs[b])return;
        run(b,l,r,false);for(int i=0;i<n;++i){float w=busGain[b].next();l[i]=dryL[i]+w*(l[i]-dryL[i]);r[i]=dryR[i]+w*(r[i]-dryR[i]);}};
    const bool aux=runs[2],pre=get("aux_pre")>.5f;float send=get("aux_send");
    if(aux&&pre)for(int i=0;i<n;++i){auxL[i]=out[0][i]*send;auxR[i]=out[1][i]*send;}
    int routing=(int)get("fx_route");
    if(routing!=1){int first=routing==2?1:0;apply(first,out[0].data(),out[1].data());apply(1-first,out[0].data(),out[1].data());}
    else if(runs[0]||runs[1]){
        for(int i=0;i<n;++i){aL[i]=bL[i]=parL[i]=out[0][i];aR[i]=bR[i]=parR[i]=out[1][i];}
        apply(0,aL.data(),aR.data());apply(1,bL.data(),bR.data());int d=std::max(lat[0],lat[1]);bool over=fx::Distort::oversampled(dk.mode);
        matchPad[0].process(aL.data(),aR.data(),n,d-lat[0],dist[1],over,distPlaced);matchPad[1].process(bL.data(),bR.data(),n,d-lat[1],dist[0],over,distPlaced);matchPar.process(parL.data(),parR.data(),n,d,dist[0]||dist[1],over,distPlaced);
        for(int i=0;i<n;++i){out[0][i]=aL[i]+bL[i]-parL[i];out[1][i]=aR[i]+bR[i]-parR[i];}}
    if(aux){if(!pre)for(int i=0;i<n;++i){auxL[i]=out[0][i]*send;auxR[i]=out[1][i]*send;}run(2,auxL.data(),auxR.data(),true);float ret=db(get("aux_return"));for(int i=0;i<n;++i){float w=busGain[2].next()*ret;out[0][i]+=auxL[i]*w;out[1][i]+=auxR[i]*w;}}
    feedback.set(get("feedback"));gain.set(db(get("level")));feedbackTime.set((float)(get("fb_time")*.001*fs));
    int length=(int)fbLine[0].size(),start=fbPos;
    for(int i=0;i<n;++i){float amount=feedback.next(),volume=gain.next(),d=std::clamp(feedbackTime.next(),runs[3]?(float)n+1:1.0f,(float)length-2);float x[2];
        for(int c=0;c<2;++c){float rp=fbPos-d;if(rp<0)rp+=length;int p=(int)rp;float back=fbLine[c][p]+(rp-p)*(fbLine[c][(p+1)%length]-fbLine[c][p]);x[c]=in[c][i]*ig[i]*(1-im[i])+fbGuard[c].process(back,amount);}
        howl.process(x[0],x[1]);for(int c=0;c<2;++c){out[c][i]=(out[c][i]+x[c])*volume;if(!runs[3])fbLine[c][fbPos]=out[c][i];}loopL[i]=out[0][i];loopR[i]=out[1][i];fbPos=(fbPos+1)%length;}
    if(runs[3]){run(3,loopL.data(),loopR.data(),false);for(int i=0;i<n;++i){fbLine[0][(start+i)%length]=loopL[i];fbLine[1][(start+i)%length]=loopR[i];}}
    float* channels[]={out[0].data(),out[1].data()};guard.process(channels,2,n,get("ceiling"));
    if(guard.takeRunawayFlag()){room.reset();texture.reset();vapor.reset();delay.reset();for(auto& l:fbLine)std::fill(l.begin(),l.end(),0);for(auto& g:fbGuard)g.reset();}
}
float Instrument::meter(int which) const {return which==0?(float)engine.activeVoices():which==1?guard.gainReductionDb():which==2?guard.loudnessCutDb():(float)guard.runawayEvents();}
}
