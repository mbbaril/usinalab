// SPDX-License-Identifier: GPL-3.0-only
// Choice order and units match src/plugin/ui/PatchText.cpp and FxPanel.cpp.
export const syncChoices = ['Livre', '1/32', '1/16T', '1/16', '1/16D', '1/8T', '1/8', '1/8D', '1/4T', '1/4', '1/4D', '1/2', '1 compasso', '2 compassos', '4 compassos'];
let envelopeTimes = [],modSources=[],patchNames=[];
export function configureControls(meta) { envelopeTimes = meta.envMs || [];modSources=meta.sources||[];patchNames=(meta.parameters||[]).slice(0,256).map(d=>d.name); }
const captions = {tex_on:'TEXTURE',tex_media:'MEDIA',tex_seed:'SEED',tex_mono:'MONO',tex_freeze:'FREEZE',tex_time_amt:'TIME RND',tex_size_amt:'SIZE RND',tex_shape_amt:'SHAPE RND',tex_pitch_amt:'PITCH RND',tex_drywet:'DRY/WET',tex_gain:'IN GAIN',vap_on:'VAPOR',vap_mode:'MODE',vap_quality:'QUALITY',vap_trig:'TRIG',vap_freeze:'FREEZE',vap_reverse:'REVERSE',rev_color:'COLOR',rev_on:'ROOM',dly_character:'CHARACTER',dly_routing:'ROUTING',dly_on:'DELAY',dly_freeze:'FREEZE',dist_on:'DISTORT',dist_mode:'MODE',dist_char:'CHARACTER',fx_route:'ROUTING',in_fx:'INPUT → FX',loop_fx_on:'LOOP'};
export function parameterCaption(d) { const op=d.id.match(/^op\d_(.+)$/),native={atk:'ATTACK',dec:'DECAY',sus:'SUSTAIN',rel:'RELEASE',kt_l_curve:'LOW CURVE',kt_h_curve:'HIGH CURVE',lfo1_pitch:'LFO1 PIT',lfo1_level:'LFO1 LVL',env_mode:'MODE',env_retrig:'RETRIG',env_atk_curve:'ATK CURVE',env_rel_curve:'REL CURVE'};return captions[d.id] || (op?(native[op[1]]||d.name.replace(/^OP\d[. ]/,'')):d.name); }
const choices = {
  mpe_mode:['OFF','LOWER ZONE / CH 1 MASTER'],
  tex_media: ['Digital', 'Vintage', 'Tape', 'Worn Tape'],
  tex_seed: ['Interno', 'Notas', 'Notas ÷', 'Relógio'],
  vap_mode: ['Granular', 'Stretch', 'Loop Delay', 'Spectral', 'Reverb', 'Resonator'],
  vap_quality: ['Stereo · 16 bits', 'Mono · 16 bits', 'Stereo · 8 bits', 'Mono · 8 bits'],
  vap_trig: ['Desligado', 'Notas', 'Relógio'],
  rev_color: ['Clean', 'Tape'],
  dist_mode: ['Saturate', 'Hard', 'Diode', 'Fuzz', 'Fold', 'Rectify', 'Crush', 'Bits', 'Compand'],
  dly_character: ['Digital', 'Tape'], dly_routing: ['Stereo', 'Ping-pong', 'Mono'],
  fx_route: ['A → B', 'A ∥ B', 'B → A'], phase: ['Livre', 'Aleatória', 'Aleatória por nota'],
  filter_type: ['LPF 12', 'LPF 24', 'LPF SK', 'LPF OTA', 'HPF 12', 'HPF 24', 'HPF SK', 'BPF 6', 'BPF 12', 'BRF 6', 'BRF 12', 'SK CASC (HP → LP)', 'SK LPF HOT', 'SK HPF HOT', 'CMOS LP', 'CMOS HP', 'CMOS BP', 'CMOS DIST']
};
const waves = ['SIN', 'SIN 12BIT', 'SIN 8BIT', 'TRI', 'SAW', 'SAW HD', 'SQR', 'SQR HD', 'ADTV SAW3', 'ADTV SQR3', 'ADTV TRI3', 'ADTV 12345', 'ADTV 1+2', 'ADTV 1+3', 'ADTV 1+4', 'ADTV 1+5', 'ADTV 1+6', 'ADTV 1+7', 'ADTV 1+8', 'NOISE S/H', 'NOISE WHITE', 'NOISE PINK', 'NOISE BLUE', 'HALF SIN', 'ABS SIN', 'QTR SIN', 'ALT SIN', 'CAMEL SIN', 'LOG SAW', 'NOISE BROWN', 'NOISE GREY', 'NOISE CLIP', 'NOISE RAMP'];
export function controlChoices(d) {
  if (d.choices) return d.choices;
  if(d.id==='bend_range'||d.id==='mpe_bend_range')return Array.from({length:d.max-d.min+1},(_,i)=>`${i+d.min} st`);
  if (/_retrig$/.test(d.id)||/^comb\d_(source|mod)$/.test(d.id)) return Array.from({length:d.max+1},(_,i)=>i?modSources.find(s=>s.index===i)?.name||String(i):'NONE');
  if (/^(op\d_env|eg[12]|env1)_mode$/.test(d.id)) return ['ADSR','ADR','LOOP'];
  if (/^lfo\d_(wave|wave_b)$/.test(d.id)) return ['TRI','SAW DOWN','SAW UP','SQR','SIN','S/H','GUITAR','RANDOM','TRI +','SAW DOWN +','SAW UP +','SQR +','EXP TRI','EXP SAW DN','EXP SAW UP','TRI 4 STEP','TRI 6 STEP','SAW 4 STEP','SAW 6 STEP','S.RND TIME','S.RND LV+TIME','RND TIME','RND LV+TIME','CHAOS LOGISTIC','CHAOS HENON','CHAOS LORENZ'];
  if (/^lfo\d_key_sync$/.test(d.id)) return ['FREE','COMMON','VOICE'];
  if (/^lfo\d_polarity$/.test(d.id)) return ['AS WAVE','+ (0..1)','+/− (−1..1)'];
  if (/^func\d_polarity$/.test(d.id)) return ['+ (0..1)','+/− (−1..1)'];
  if (/^func\d_mode$/.test(d.id)) return ['LOOP','ONE-SHOT','ENVELOPE','ENV LOOP'];
  if (/^rand\d_type$/.test(d.id)) return ['RANDOM LOOP','SAMPLE & HOLD','BINARY'];
  if (/^comb\d_type$/.test(d.id)) return ['SUM','DIFF','MULTIPLY','DIVIDE','CROSSFADE','LAG','THRESHOLD','OFFSET','REMAP','FOLLOWER','MIN','MAX','INPUT LEVEL'];
  if (/^a[123]_target$/.test(d.id)) return ['CLASSIC (A1 RATIO, A2 FB, A3 DETUNE)','OFF','RATIO (MODULATORS)','FB (ALL FM OPS)','DETUNE SPREAD (CARRIERS)',...patchNames];
  if (d.id === 'algo') return [...Array.from({length:40},(_,i)=>String(i+1)), 'USER'];
  if (d.id === 'casc_vca') return ['Gate', 'EG2', 'Envelope'];
  if (d.id === 'lpg_mode') return ['Desligado', 'Ping', 'EG2'];
  if (/^op\d_freq$/.test(d.id)) return ['RATIO', 'FIXED'];
  if (/^op\d_coarse$/.test(d.id)) return ['1/128', '1/64', '1/32', '1/16', '1/8', '1/4', '1/2', ...Array.from({length:32}, (_, i) => String(i + 1))].map(v => `× ${v}`);
  if (/^op\d_wave$/.test(d.id)) return waves;
  if (/^op\d_kt_[lh]_curve$/.test(d.id)) return ['Linear', 'Exponencial'];
  if (d.id.endsWith('_sync') && d.max === 14) return syncChoices;
  if (choices[d.id]) return choices[d.id];
  if (d.min === 0 && d.max === 1 && d.step === 1) return ['Desligado', 'Ligado'];
  return null;
}
export function choiceDisabled(d,value){
  if(/^comb\d_(source|mod)$/.test(d.id)){const k=+d.id.match(/^comb(\d)/)[1];const first=modSources.find(s=>s.id===`comb${k}`)?.index,last=modSources.find(s=>s.id==='comb3')?.index;return value>=first&&value<=last;}
  if(!/_retrig$/.test(d.id))return false;
  const prefix=d.id.replace(/_retrig$/,''),self=prefix==='env1'?'env1':prefix==='eg1'?'env2':prefix==='eg2'?'env3':/^(lfo|func|rand)\d$/.test(prefix)?prefix:null;
  return !!self&&value===modSources.find(s=>s.id===self)?.index;
}
export function displayValue(d, v) {
  if (d.text) return d.text(v);
  if (/^op\d_level$/.test(d.id)&&v===0) return 'OFF';
  if (/^op\d_(vel|sus|rate_key)$/.test(d.id)||/^(eg[12]|env1)_sus$/.test(d.id)) return `${v}%`;
  if (/^op\d_(vel_pitch|lfo1_level|kt_low|kt_high)$/.test(d.id)) return `${v>0?'+':''}${v}%`;
  const opts = controlChoices(d); if (opts) return opts[v-d.min] ?? String(v);
  if (/_(atk|dec|rel|fade|rise|fall)$/.test(d.id) && d.max === 127 && envelopeTimes.length) {
    const ms = envelopeTimes[Math.round(v)]; return ms < 1000 ? `${ms.toFixed(ms < 10 ? 1 : 0)} ms` : `${(ms/1000).toFixed(ms < 10000 ? 2 : 1)} s`;
  }
  if (d.id === 'cutoff' || d.id === 'hp_cut') { const hz = 6.489 * (23680 / 6.489) ** (v / 1000); return hz < 1000 ? `${hz.toFixed(hz < 100 ? 1 : 0)} Hz` : `${(hz / 1000).toFixed(2)} kHz`; }
  if (d.id === 'flt_drive') return `× ${(2 ** (3 * v / 100)).toFixed(2)}`;
  if (d.id === 'out_gain' || d.id === 'casc_out') return `+${(.75 * v).toFixed(2)} dB`;
  if (/(_eg2_int|^eg2_int$|^lfo2_int$|^op\d_(eg1|lfo1_pitch)$)/.test(d.id)) return `${(v / 16).toFixed(1)} st`;
  if (/_speed$/.test(d.id) && d.max === 127) {const hz = .01 * 10000 ** (v / 127);return `${hz.toFixed(hz < 10 ? 2 : 1)} Hz`;}
  if (/^func\d_sustain$/.test(d.id)) return `POINT ${v+1}`;
  if (/^func\d_smooth$/.test(d.id)) return `${v} ms`;
  if (/^lfo\d_phase$/.test(d.id)) return v>180?'RANDOM':`${v} deg`;
  if (/^op\d_fixed_hz$/.test(d.id)) return `${(Math.max(1, v) / 100).toFixed(2)} Hz`;
  if (/^op\d_fine$/.test(d.id)) return `× ${(v / 100).toFixed(2)}`;
  if (/^op\d_detune$/.test(d.id)) return `${v > 0 ? '+' : ''}${v} cents`;
  if (/^op\d_trans$/.test(d.id)) return `${v > 0 ? '+' : ''}${v} st`;
  if (/(?:_env)?_rel_curve$/.test(d.id)&&v<0) return '= DEC';
  if (/_curve$/.test(d.id)) return v===0?'LIN':v===10?'EXP':String(v);
  if (/_kt_center$/.test(d.id)||d.id==='key_center') return `${['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'][v%12]}${Math.floor(v/12)-1}`;
  if (d.step < 1 && d.max <= 1.1 && d.min >= -1) return `${(v * 100).toFixed(1)} %`;
  return d.step === 1 ? String(Math.round(v)) : Math.abs(v) >= 100 ? v.toFixed(0) : v.toFixed(Math.abs(v) >= 2 ? 1 : 2);
}

const opFilters = ['LPF', 'HPF', 'BPF', 'BRF', 'LADDER LP12', 'LADDER LP24', 'LADDER HP12', 'LADDER HP24', 'LADDER BP6', 'LADDER BP12', 'SK LPF', 'SK HPF'];
const opEffects = ['PEAK EQ', 'SHELV EQ', 'PHASER', 'RING', 'COMB', 'DIST', 'DRIVE', 'DEC', 'WAVESHAPER', 'PUNCH', 'STRING', 'TUBE', 'PLATE', 'BAR', 'SHEET', 'SPRING', 'DISPERSE'];
const pct = v => `${v} %`, signed = v => `${v>0?'+':''}${v}`, semi = v => `${v>100?'+':''}${((v-100)/2).toFixed(1)} st`, db = v => `${v>50?'+':''}${((v-50)*.36).toFixed(1)} dB`;
const roundNative = v => {const n=Math.floor(v),f=v-n;return f===.5?n+(n%2!==0?1:0):Math.round(v);};
export const nativeFixed=(v,digits)=>{const scale=10**digits,n=v*scale;return (n-Math.floor(n)===.5?roundNative(n)/scale:v).toFixed(digits);};
export function sourceColor(id = '') {
  if (/^(env|openv)/.test(id)) return '#c2f000';
  for (const [prefix, color] of [['lfo','#25e0c4'],['func','#9d4edd'],['rand','#ffb000'],['comb','#3fa2ff'],['mac','#ff5ca8'],['seq','#e8e8e3'],['ctrl','#ff7a45']]) if (id.startsWith(prefix)) return color;
  return '#9aa0a6';
}
export function operatorValue(mode, which, fx, models = {}) {
  const spec=(name,text=pct,choices)=>({name,text,choices});
  switch(mode){
    case 0:return [spec('FB'),spec('WIDTH',v=>v>=1&&v<=99?pct(v):'OFF')][which];
    case 1:return [spec('DEPTH'),spec('SHAPE')][which];
    case 2:case 3:return [spec('TYPE',null,opFilters),mode===2?spec('OSC MIX'):null,spec('CUTOFF',semi),spec('RESO')][which];
    case 4:return [spec('GAIN'),spec('OSC MIX'),spec('BIAS',v=>`${signed(v-100)} %`)][which];
    case 5:{
      if(which===0)return spec('FX',null,opEffects);
      if(which===1)return spec('OSC MIX');
      fx=((fx%17)+17)%17;
      if(which===2){
        if(fx===1)return spec('LOW',v=>db(v>>1));
        if(fx===5||fx===6)return spec(fx===5?'DIST':'DRIVE',v=>`x${nativeFixed(1+v*(fx===5?126:12)/256,1)}`);
        if(fx===7)return spec('FREQ',v=>`x${(2**(1+7*v/200)).toFixed(1)}`);
        if(fx===8)return spec('SHAPE',v=>['CHEB 3','CHEB 5','CHEB 7','GLASS','SINE x2','SINE x5','RECT','STAIRS'][Math.min(7,Math.floor(v*8/201))]);
        if(fx===9)return spec('PUNCH',v=>pct(roundNative(v/2)));
        return spec('FREQ',semi);
      }
      if([0,1,5,6,9].includes(fx))return spec(fx===0?'GAIN':fx===1||fx===9?'HIGH':'LOW',db);
      if(fx===2||fx===4)return spec('FB',v=>`${signed(roundNative((v-50)*1.9))} %`);
      if(fx===3||fx===8)return spec(fx===3?'MIX':'DAMP');
      if(fx===7)return spec('BIT',v=>{const bits=2+Math.floor(v*22/100);return `${bits}${bits>=16?' (off)':''}`;});
      if(fx===10)return spec('DECAY',v=>(.8+.001996*v).toFixed(3));
      if(fx===16)return spec('STAGES',v=>String(1+Math.floor(v*15/100)));
      return spec('DECAY',v=>`${(.05*160**(v/100)).toFixed(2)} s`);
    }
    case 8:return [spec('STRUCTURE',null,models.modal),spec('OSC MIX'),spec('SIZE',v=>`x${nativeFixed(2**(-(v-100)/24),2)}`),spec('DECAY',v=>`x${nativeFixed(2**((v-50)/10),v<30?3:2)}`)][which];
    case 9:return [spec('MODEL',null,models.chaos),spec('CHAOS'),spec('SHAPE',v=>pct(Math.floor((v+1)/2))),spec('SMOOTH')][which];
    case 10:return [spec('DISTRIB',null,models.stoch),spec('LVL STEP'),spec('POINTS',v=>String(3+Math.round(v/200*29))),spec('LEN STEP')][which];
    case 11:return [spec('MODEL',null,models.macro),spec('HARMONICS'),spec('TIMBRE',v=>pct(Math.floor((v+1)/2))),spec('MORPH')][which];
    case 12:return [spec('READ',null,['ONCE','LOOP','PING-PONG','REVERSE']),spec('START'),spec('LENGTH',v=>pct(Math.floor((v+1)/2))),
      ((fx%4)+4)%4===1?spec('XFADE'):spec('FADE',v=>{const ms=200**(Math.max(0,Math.min(100,v))/100);return `${ms<10?ms.toFixed(1):Math.round(ms)} ms`;})][which];   // (SAMPLE: engines/fm/FmSample.h; D: LOOP's seam, the others' fades)
    case 13:return [spec('VOICES',null,['1','2','3','4','5','6','7','8','9']),spec('DETUNE'),spec('MIX',v=>pct(Math.floor((v+1)/2))),spec('DRIFT')][which];   // (UNISON: engines/fm/FmUnison.h)
    default:return null;
  }
}

export function envelopeTimeText(v){const ms=envelopeTimes[Math.round(v)]||0;return ms<=0?'0 ms':ms<10?`${nativeFixed(ms,1)} ms`:ms<1000?`${Math.round(ms)} ms`:`${nativeFixed(ms/1000,ms<10000?2:1)} s`;}
