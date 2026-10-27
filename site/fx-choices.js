// SPDX-License-Identifier: GPL-3.0-only
// Choice strings and index order from TexturePanel, FxPanel and native effect name tables.
export const fxChoices={
 tex_on:['OFF','ON'],tex_media:['DIGITAL','VINTAGE','TAPE','WORN TAPE'],tex_mono:['OFF','ON'],tex_seed:['INTERNAL','NOTES','NOTES DIV','HOST CLOCK'],tex_freeze:['OFF','ON'],
 vap_mode:['GRANULAR','STRETCH','LOOP DELAY','SPECTRAL','REVERB','RESONATOR'],vap_quality:['STEREO HI','MONO HI','STEREO LO-FI','MONO LO-FI'],vap_trig:['OFF','NOTES','HOST CLOCK'],
 rev_color:['CLEAN','TAPE'],dist_mode:['SATURATE','HARD','DIODE','FUZZ','FOLD','RECTIFY','CRUSH','BITS','COMPAND'],
 dly_character:['DIGITAL','TAPE'],dly_routing:['STEREO','PING-PONG','MONO'],dly_sync:['FREE','1/32','1/16T','1/16','1/16D','1/8T','1/8','1/8D','1/4T','1/4','1/4D','1/2','1 BAR','2 BARS','4 BARS']
};
export const fxChoiceParameter=d=>fxChoices[d.id]?{...d,choices:fxChoices[d.id]}:d;
