// SPDX-License-Identifier: GPL-3.0-only
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Cabine.h"
#include <set>

namespace {
constexpr double kMaxFbSeconds = 2.0;

// the texture's parameters (fx/Texture.h), stable IDs (rule 19); pTex[] in this order
constexpr int kTexParams = 18;
const char* const kTexIds[kTexParams] = { "tex_time", "tex_size", "tex_shape", "tex_pitch", "tex_time_amt",
                                          "tex_size_amt", "tex_shape_amt", "tex_pitch_amt", "tex_density",
                                          "tex_feedback", "tex_drywet", "tex_reverb", "tex_gain", "tex_freeze",
                                          "tex_mono", "tex_media", "tex_seed", "tex_on" };

// VAPOR's parameters (fx/Vapor.h), stable IDs (rule 19); pVap[] in this order: the knobs as FmMod.h FD_VAP_*, then
// the switches and lists
const char* const kVapIds[] = { "vap_position", "vap_size", "vap_pitch", "vap_density", "vap_texture", "vap_drywet",
                                "vap_spread", "vap_feedback", "vap_reverb", "vap_on", "vap_freeze", "vap_reverse",
                                "vap_mode", "vap_quality", "vap_trig", "vap_limit" };
enum VapParam { kVapKnobs = 9, kVapOn = 9, kVapFreeze, kVapReverse, kVapMode, kVapQuality, kVapTrig, kVapLimit };

// the module's knob laws, for the values shown (fx/texture/pot_channel.h, without its smoothing): PITCH's intervals
float texPitchSemitones(float v)
{
    float detune = 2.0f * v - 1.0f;
    const float sign = detune < 0.0f ? -1.0f : 1.0f;
    detune = detune * sign * 6.0f;
    const int i = std::min(6, (int)detune);
    const float fr = std::max((detune - (float)i - 0.33333333f) * 1.5f, 0.0f);
    const float intervals[8] = { 0.0f, 1.0f, 3.0f, 4.0f, 7.0f, 12.0f, 24.0f, 24.0f };
    const float s = fr * fr * (3.0f - 2.0f * fr);
    return (intervals[i] + (intervals[i + 1] - intervals[i]) * s) * sign;
}
// ... and the attenuverters' (a dead zone around the centre)
float texAmount(float v)
{
    v -= 0.5f;
    return (2.0f / 0.47f) * v * std::max(std::fabs(v) - 0.03f, 0.0f);
}

// the sequencer's pattern in a session (v6): the grid, and each lane's settings and 64 steps, a step as
// "on,note,velocity,probability,ratchet,gate,accent,pitch,micro,condition,k,n" (stable forever: rule 19)
juce::ValueTree seqToTree(const ruidocore::Pattern& p)
{
    juce::ValueTree t("SEQ");
    t.setProperty("res", p.resolutionStepsPerBeat, nullptr);
    t.setProperty("swing", p.swingPercent, nullptr);
    for (const auto& part : p.parts) {
        juce::ValueTree l("LANE");
        l.setProperty("len", part.length, nullptr);
        l.setProperty("dir", (int)part.direction, nullptr);
        l.setProperty("mul", part.clockMultiplier, nullptr);
        l.setProperty("div", part.clockDivider, nullptr);
        l.setProperty("swing", part.swingPercent, nullptr);
        l.setProperty("base", part.baseNote, nullptr);
        juce::StringArray steps;
        for (const auto& e : part.steps)
            steps.add(juce::String(e.enabled ? 1 : 0) + "," + juce::String(e.explicitNote) + "," + juce::String(e.velocity) +
                      "," + juce::String(e.probability, 4) + "," + juce::String(e.ratchet) + "," +
                      juce::String(e.gateLength, 4) + "," + juce::String(e.accentAmount, 4) + "," +
                      juce::String(e.pitchSemitoneOffset) + "," + juce::String(e.microtimingMs) + "," +
                      juce::String((int)e.condition.type) + "," + juce::String(e.condition.ratioK) + "," +
                      juce::String(e.condition.ratioN));
        l.setProperty("steps", steps.joinIntoString(";"), nullptr);
        t.appendChild(l, nullptr);
    }
    return t;
}

// read back; anything missing or malformed keeps the default (a lane, a step), never a guess
ruidocore::Pattern seqFromTree(const juce::ValueTree& t)
{
    auto p = usina::seq::defaultPattern();
    if (!t.isValid()) return p;
    p.resolutionStepsPerBeat = juce::jlimit(1, 16, (int)t.getProperty("res", 4));
    p.swingPercent = juce::jlimit(0.0f, 100.0f, (float)t.getProperty("swing", 0.0f));
    for (int i = 0; i < juce::jmin(t.getNumChildren(), usina::seq::kLanes); ++i) {
        const auto l = t.getChild(i);
        auto& part = p.parts[(size_t)i];
        part.length = juce::jlimit(1, ruidocore::kStepsPerPart, (int)l.getProperty("len", 16));
        part.direction = (ruidocore::Direction)juce::jlimit(0, 3, (int)l.getProperty("dir", 0));
        part.clockMultiplier = juce::jlimit(1, 16, (int)l.getProperty("mul", 1));
        part.clockDivider = juce::jlimit(1, 16, (int)l.getProperty("div", 1));
        part.swingPercent = (float)l.getProperty("swing", ruidocore::kSwingInherit);
        part.baseNote = juce::jlimit(0, 127, (int)l.getProperty("base", 60));
        const auto steps = juce::StringArray::fromTokens(l.getProperty("steps").toString(), ";", "");
        for (int k = 0; k < juce::jmin(steps.size(), ruidocore::kStepsPerPart); ++k) {
            const auto v = juce::StringArray::fromTokens(steps[k], ",", "");
            if (v.size() != 12) continue;
            auto& e = part.steps[(size_t)k];
            e.enabled = v[0].getIntValue() != 0;
            e.explicitNote = juce::jlimit(-1, 127, v[1].getIntValue());
            e.velocity = juce::jlimit(0, 127, v[2].getIntValue());
            e.probability = juce::jlimit(0.0f, 1.0f, v[3].getFloatValue());
            e.ratchet = juce::jlimit(1, 8, v[4].getIntValue());
            e.gateLength = juce::jlimit(0.01f, 1.0f, v[5].getFloatValue());
            e.accentAmount = juce::jlimit(0.0f, 1.0f, v[6].getFloatValue());
            e.pitchSemitoneOffset = juce::jlimit(-48, 48, v[7].getIntValue());
            e.microtimingMs = juce::jlimit(-100, 100, v[8].getIntValue());
            e.condition.type = (ruidocore::TrigConditionType)juce::jlimit(0, 4, v[9].getIntValue());
            e.condition.ratioK = juce::jlimit(1, 16, v[10].getIntValue());
            e.condition.ratioN = juce::jlimit(1, 16, v[11].getIntValue());
        }
    }
    return p;
}
} // namespace

// The buses: a stereo output; a stereo input (audio in; the microphone in the Standalone) and a sidechain, both
// optional for the host. The input starts muted by its mix (rule 24), not by a disabled bus, so the Standalone
// can open the microphone without the user reconfiguring anything.
juce::AudioProcessor::BusesProperties UsinaProcessor::buses()
{
    return BusesProperties()
        .withInput("Entrada", juce::AudioChannelSet::stereo(), true)
        .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
        .withOutput("Saida", juce::AudioChannelSet::stereo(), true);
}

UsinaProcessor::UsinaProcessor()
    : AudioProcessor(buses()),
      params(*this, nullptr, "USINA", createLayout(distModeForText))
{
    pInGain = params.getRawParameterValue("in_gain");
    pInMix = params.getRawParameterValue("in_mix");
    pInFx = params.getRawParameterValue("in_fx");
    pCapLen = params.getRawParameterValue("cap_len");
    pCapNorm = params.getRawParameterValue("cap_norm");
    pFeedback = params.getRawParameterValue("feedback");
    pFbTime = params.getRawParameterValue("fb_time");
    pLevel = params.getRawParameterValue("level");
    pBendRange = params.getRawParameterValue("bend_range");
    pMpeMode = params.getRawParameterValue("mpe_mode");
    pMpeBendRange = params.getRawParameterValue("mpe_bend_range");
    pCeiling = params.getRawParameterValue("ceiling");
    pRevAmount = params.getRawParameterValue("rev_amount");
    pRevColor = params.getRawParameterValue("rev_color");
    pRevOn = params.getRawParameterValue("rev_on");
    pFxOn = params.getRawParameterValue("fx_on");
    pFxRoute = params.getRawParameterValue("fx_route");
    pAuxSend = params.getRawParameterValue("aux_send");
    pAuxPre = params.getRawParameterValue("aux_pre");
    pAuxReturn = params.getRawParameterValue("aux_return");
    for (int b = 0; b < kFxBuses; ++b) {
        for (int s = 0; s < kFxSlotsPerBus; ++s) pFxType[(size_t)b][(size_t)s] = params.getRawParameterValue(fxSlotTypeId(b, s));
        pBusOn[(size_t)b] = params.getRawParameterValue(fxBusOnId(b));
    }
    pBusMix[0] = params.getRawParameterValue("fx_a_mix");
    pBusMix[1] = params.getRawParameterValue("fx_b_mix");
    for (int i = 0; i < kTexParams; ++i) pTex[i] = params.getRawParameterValue(kTexIds[i]);
    static_assert(sizeof(kVapIds) / sizeof(kVapIds[0]) == kVapParams);
    static_assert(kVapKnobs == usina::fm::FD_VAP_REVERB - usina::fm::FD_VAP_POSITION + 1);
    for (int i = 0; i < kVapParams; ++i) pVap[i] = params.getRawParameterValue(kVapIds[i]);
    pDlyOn = params.getRawParameterValue("dly_on");
    pDlySync = params.getRawParameterValue("dly_sync");
    pDlyChar = params.getRawParameterValue("dly_character");
    pDlyRoute = params.getRawParameterValue("dly_routing");
    pDlyFreeze = params.getRawParameterValue("dly_freeze");
    pDlyOut = params.getRawParameterValue("dly_out");
    {
        const char* ids[7] = { "dly_time", "dly_feedback", "dly_lowcut", "dly_highcut", "dly_wow", "dly_mix",
                               "dly_flutter" };
        for (int i = 0; i < 7; ++i) pDlyKnob[(size_t)i] = params.getParameter(ids[i]);
    }
    pDistOn = params.getRawParameterValue("dist_on");
    pDistMode = params.getRawParameterValue("dist_mode");
    pDistOut = params.getRawParameterValue("dist_out");
    {
        const char* ids[5] = { "dist_drive", "dist_char", "dist_sym", "dist_tone", "dist_mix" };
        for (int i = 0; i < 5; ++i) pDistKnob[(size_t)i] = params.getParameter(ids[i]);
    }
    pIrOn = params.getRawParameterValue("ir_on");
    pIrOut = params.getRawParameterValue("ir_out");
    {
        const char* ids[3] = { "ir_mix", "ir_lowcut", "ir_highcut" };
        for (int i = 0; i < 3; ++i) pIrKnob[(size_t)i] = params.getParameter(ids[i]);
    }
    pSeqOn = params.getRawParameterValue("seq_on");
    pSeqBpm = params.getRawParameterValue("seq_bpm");
    pSeqPlay = params.getRawParameterValue("seq_play");
    for (auto& n : ctrlNum) n.store(-1);
    seq.setPatternNow(seqPatternMsg);
    const auto& s = usina::fm::schema();
    for (int i = 0; i < usina::fm::kNumValues; ++i) {
        pv[(size_t)i] = params.getRawParameterValue(s[(size_t)i].id);
        pp[(size_t)i] = params.getParameter(s[(size_t)i].id);
        jassert(pv[(size_t)i] != nullptr && pp[(size_t)i] != nullptr);
    }
    for (int m = 0; m < 4; ++m) pMacro[(size_t)m] = params.getRawParameterValue("macro" + juce::String(m + 1));
    for (auto& t : actType) t.store(-1);                  // (the CABINE's actions: none bound yet)
    osFactor.store(1 << juce::jlimit(0, 4, (int)params.getRawParameterValue("oversampling")->load()));
    params.addParameterListener("oversampling", this);
    params.addParameterListener("dist_mode", this);
    distModeForText->store(juce::roundToInt(pDistMode->load()));
    const int start = lib.indexOf("CHAO TREME");          // the first sound: the showcase's first
    selectPreset(start >= 0 ? start : 0);
    startTimerHz(60);                                     // (the CC maps' values onto their parameters)
    cabineModel = std::make_unique<Cabine>(*this);
    if (keepsGlobalMappings()) importMappings(globalMappingsFile());   // (the rig's, for every new instance)
    mapSignature = mappingsTree().toXmlString();
}

UsinaProcessor::~UsinaProcessor()
{
    cabineModel.reset();                                  // (the Launchpad let go, while everything it reads exists)
    stopTimer();
    params.removeParameterListener("oversampling", this);
    params.removeParameterListener("dist_mode", this);
    cancelPendingUpdate();
}

// --- MIDI learn on a parameter (the message thread's, but for what the audio thread records) ------------------------
void UsinaProcessor::midiLearn(const juce::String& paramId)
{
    ccLearn.store(paramId.isEmpty() ? nullptr : params.getParameter(paramId));
}

void UsinaProcessor::midiForget(const juce::String& paramId)
{
    auto* p = params.getParameter(paramId);
    for (auto& m : ccMaps)
        if (p != nullptr && m.param.load() == p) {
            m.param.store(nullptr);
            m.cc.store(-1);
            m.value.store(-1.0f);
        }
}

juce::String UsinaProcessor::midiLearning() const
{
    auto* p = ccLearn.load();
    return p != nullptr ? p->paramID : juce::String();
}

int UsinaProcessor::midiCcOf(const juce::String& paramId) const
{
    auto* p = params.getParameter(paramId);
    for (const auto& m : ccMaps)
        if (p != nullptr && m.param.load() == p && m.cc.load() >= 0) return m.cc.load();
    return -1;
}

// the values that came since the last tick onto their parameters, as a gesture each (the host records automation)
void UsinaProcessor::applyCcMaps()
{
    for (auto& m : ccMaps) {
        const float v = m.value.exchange(-1.0f);
        auto* p = m.param.load();
        if (v < 0.0f || p == nullptr) continue;
        if (std::fabs(p->getValue() - v) < 1.0e-6f) continue;
        p->beginChangeGesture();
        p->setValueNotifyingHost(v);
        p->endChangeGesture();
    }
}

// --- the CABINE's actions ------------------------------------------------------------------------------------------------
UsinaProcessor::ActionBinding UsinaProcessor::actionBinding(int a) const
{
    if (a < 0 || a >= kCabActions) return {};
    return { actType[(size_t)a].load(), actNum[(size_t)a].load(), actChan[(size_t)a].load() };
}

void UsinaProcessor::setActionBinding(int a, ActionBinding b)
{
    if (a < 0 || a >= kCabActions) return;
    actType[(size_t)a].store(juce::jlimit(-1, 1, b.type));
    actNum[(size_t)a].store(juce::jlimit(0, 127, b.number));
    actChan[(size_t)a].store(juce::jlimit(0, 16, b.channel));
}

bool UsinaProcessor::actionMatch(int a, int type, int number, int channel) const
{
    if (actType[(size_t)a].load() != type || actNum[(size_t)a].load() != number) return false;
    const int c = actChan[(size_t)a].load();
    return c == 0 || c == channel;
}

// (any thread: the audio thread's MIDI, the Launchpad's handed to the message thread)
void UsinaProcessor::queueAction(int a, bool pressed, float value)
{
    const int v = juce::jlimit(0, 127, juce::roundToInt(value * 127.0f));
    const double now = juce::Time::getMillisecondCounterHiRes();
    actFifo.write(1).forEach([&](int i) {
        actQueue[(size_t)i] = a | (pressed ? 1 : 0) << 8 | v << 9;
        actTime[(size_t)i] = now;
    });
}

void UsinaProcessor::cabineNote(int note, bool on, int velocity, bool fromMessageThread)
{
    const int q = fromMessageThread ? 1 : 0;
    const int e = juce::jlimit(0, 127, note) | juce::jlimit(0, 127, velocity) << 7 | (on ? 1 : 0) << 14;
    cabNoteFifo[(size_t)q].write(1).forEach([&](int i) { cabNoteQueue[(size_t)q][(size_t)i] = e; });
}

void UsinaProcessor::actionInput(int type, int number, int channel, bool pressed, float value)
{
    const int learn = actLearn.load();
    if (learn >= 0 && pressed) {
        setActionBinding(learn, { type, number, channel });
        actLearn.store(-1);
        return;
    }
    for (int a = 0; a < kCabActions; ++a)
        if (actionMatch(a, type, number, channel)) queueAction(a, pressed, value);
}

void UsinaProcessor::timerCallback()
{
    if (keepsGlobalMappings() && ++mapTicks % 60 == 0) {   // (once a second: a mapping a hand changed, kept)
        const auto now = mappingsTree().toXmlString();
        if (now != mapSignature) {
            mapSignature = now;
            exportMappings(globalMappingsFile());
        }
    }
    applyCcMaps();
    runPendingActions();
    engine.collectSample();                              // (SAMPLE: a recording the audio thread let go, freed here)
    // the latency the host compensates (28/10): the guard's and the effects' chain (the TEXTURE, the VAPOR behind
    // their converters), told only when it changes (a host may restart its graph)
    if (const int lat = guard.latencySamples() + fxChainLatency.load(std::memory_order_relaxed); lat != reportedLatency) {
        reportedLatency = lat;
        setLatencySamples(lat);
    }
    if (capWasArmed && (!capArmed.load() || capFull.load())) captureFinish();
}

// the actions that came: run on this (the message) thread, in order
void UsinaProcessor::runPendingActions()
{
    actFifo.read(actFifo.getNumReady()).forEach([this](int i) {
        const int e = actQueue[(size_t)i];
        if ((e & 0xFF) == kProgramEvent) {               // (a Program Change: the library's preset)
            selectPreset(e >> 9);                        // (out of the library's range: nothing)
            return;
        }
        if (cabineModel != nullptr)
            cabineModel->runAction(e & 0xFF, ((e >> 8) & 1) != 0, (float)(e >> 9) / 127.0f, actTime[(size_t)i]);
    });
}

void UsinaProcessor::parameterChanged(const juce::String& id, float value)
{
    if (id == "dist_mode") {                             // (CHARACTER's text follows the mode: any thread)
        distModeForText->store(juce::jlimit(0, usina::fx::Distort::kNumModes - 1, juce::roundToInt(value)));
        return;
    }
    if (id == "oversampling") {                          // (any thread: the work is the message thread's)
        osFactor.store(1 << juce::jlimit(0, 4, juce::roundToInt(value)));
        triggerAsyncUpdate();
    }
}

void UsinaProcessor::handleAsyncUpdate()
{
    if (hostRate <= 0.0) return;                         // (not prepared yet: prepareToPlay will use it)
    suspendProcessing(true);                             // (returns once no processBlock runs: the callback lock)
    prepareEngine();
    suspendProcessing(false);
}

// the engine at N x the host's rate, its buffers for N x the largest block, the decimators. At 1x the voice filter
// runs the firmware's way (its saturating types at 2x inside); from 2x everything runs at the engine's rate
void UsinaProcessor::prepareEngine()
{
    const int n = osFactor.load();
    engine.prepare(hostRate * n, n == 1, hostRate);
    std::array<int, usina::fm::kNumValues> v;
    readPatch(v);
    engineValues = v;
    int mac[3];
    for (int m = 0; m < 3; ++m) mac[m] = engineMacros[(size_t)m] = juce::jlimit(-64, 63, juce::roundToInt(pMacro[(size_t)m]->load() * 64.0f));
    engine.setPatch(usina::fm::Patch::fromArrayWithMacros(engineValues.data(), mac));
    engL.assign((size_t)juce::jmax(1, hostBlock) * (size_t)n, 0.0f);
    engR.assign(engL.size(), 0.0f);
    for (auto& d : decim) d.setFactor(n);
    preparedOs = n;
    {                                                    // (the routes as they are now: not the audio thread)
        const juce::SpinLock::ScopedLockType sl(routesLock);
        engine.setRoutes(routesMsg);
        for (int k = 0; k < 3; ++k) engine.setFunctionShape(k, funcMsg[(size_t)k]);
        routesDirty = false;
    }
    performanceMidi.configure(pMpeMode->load() > .5f, pBendRange->load(), pMpeBendRange->load());
    performanceMidi.reset();
}

// --- the modulation's routes (the message thread's) ------------------------------------------------------------------
int UsinaProcessor::addRoute(int src, int dst, float amount)
{
    if (src <= usina::fm::MS_NONE || src >= usina::fm::kNumSources || !usina::fm::isRouteTarget(dst)) return -1;
    auto& r = routesMsg;
    for (int i = 0; i < r.n; ++i)                       // (the same pair again: its amount moves)
        if (r.r[(size_t)i].src == src && r.r[(size_t)i].dst == dst) {
            r.r[(size_t)i].amount = juce::jlimit(-1.0f, 1.0f, amount);
            publishRoutes();
            return i;
        }
    if (r.n >= usina::fm::kMaxRoutes) return -1;
    r.r[(size_t)r.n] = { src, dst, juce::jlimit(-1.0f, 1.0f, amount) };
    ++r.n;                                               // (counted before it is published: the audio thread gets it now,
    publishRoutes();                                     // not at the next edit - a route added and left as it was
    return r.n - 1;                                      // stayed silent until then; found by ProcessorTest, 21/10)
}

void UsinaProcessor::setRouteAmount(int index, float amount)
{
    if (index < 0 || index >= routesMsg.n) return;
    routesMsg.r[(size_t)index].amount = juce::jlimit(-1.0f, 1.0f, amount);
    publishRoutes();
}

void UsinaProcessor::removeRoute(int index)
{
    auto& r = routesMsg;
    if (index < 0 || index >= r.n) return;
    for (int i = index; i + 1 < r.n; ++i) r.r[(size_t)i] = r.r[(size_t)i + 1];
    --r.n;
    publishRoutes();
}

void UsinaProcessor::publishRoutes()
{
    const juce::SpinLock::ScopedLockType sl(routesLock);   // (the audio thread only try-locks: it never waits)
    routesPending = routesMsg;
    funcPending = funcMsg;                               // (FUNC's points travel with the routes)
    routesDirty = true;
}

void UsinaProcessor::setFuncShape(int k, const usina::fm::FuncShape& s)
{
    if (k < 0 || k >= 3) return;
    funcMsg[(size_t)k] = usina::fm::sanitizeFuncShape(s);
    publishRoutes();
}

void UsinaProcessor::readPatch(std::array<int, usina::fm::kNumValues>& v) const
{
    for (int i = 0; i < usina::fm::kNumValues; ++i) v[(size_t)i] = (int)std::lround(pv[(size_t)i]->load());
}

void UsinaProcessor::selectPreset(int index)
{
    if (index < 0 || index >= lib.size())
        return;
    std::vector<std::string> unknown;
    const auto v = usina::fm::Patch::valuesFrom(lib[index].values, &unknown);
    for ([[maybe_unused]] const auto& u : unknown)        // (never silently: docs/IA_PATCHES.md)
        DBG("Usina: preset " << lib[index].name << " has an unknown value name: " << u);
    // its values into the parameters (the host hears of each, so its automation lanes and undo follow)
    for (int i = 0; i < usina::fm::kNumValues; ++i)
        pp[(size_t)i]->setValueNotifyingHost(pp[(size_t)i]->convertTo0to1((float)v[(size_t)i]));
    routesMsg = usina::fm::Routes {};                    // (a preset is its modulation too: the library's have none)
    for (int k = 0; k < 3; ++k) funcMsg[(size_t)k] = usina::fm::defaultFuncShape(k);
    publishRoutes();
    current.store(index, std::memory_order_relaxed);
    userName.clear();                                    // (the library's sound now, not a user preset)
}

juce::AudioProcessorValueTreeState::ParameterLayout UsinaProcessor::createLayout(std::shared_ptr<std::atomic<int>> distMode)
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    // IDs are stable forever (rule 19); the version in ParameterID is the parameter's own, not the state's
    l.add(std::make_unique<P>(juce::ParameterID { "in_gain", 1 }, "Input Gain", R(-36.0f, 24.0f, 0.1f), 0.0f,
                              juce::AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<P>(juce::ParameterID { "in_mix", 1 }, "Input Mix", R(0.0f, 1.0f, 0.001f), 0.0f));
    // INPUT -> FX: the input into the effects with the voices (off: it joins after them, as before)
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "in_fx", 1 }, "Input to Effects", false));
    l.add(std::make_unique<P>(juce::ParameterID { "feedback", 1 }, "Feedback", R(0.0f, 1.5f, 0.001f), 0.0f));
    l.add(std::make_unique<P>(juce::ParameterID { "fb_time", 1 }, "Feedback Time",
                              R(1.0f, 2000.0f, 0.1f, 0.35f), 120.0f,
                              juce::AudioParameterFloatAttributes().withLabel("ms")));
    l.add(std::make_unique<P>(juce::ParameterID { "level", 1 }, "Level", R(-36.0f, 6.0f, 0.1f), 0.0f,
                              juce::AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<P>(juce::ParameterID { "ceiling", 1 }, "Ceiling", R(-24.0f, -0.3f, 0.1f), -1.0f,
                              juce::AudioParameterFloatAttributes().withLabel("dB")));
    // the voices' room (fx/RoomReverb.h): one knob, as the module it ports (mix and time from it), and its colour.
    // At 0 the room stops: a session without it plays bit for bit as before
    l.add(std::make_unique<P>(juce::ParameterID { "rev_amount", 1 }, "Reverb", R(0.0f, 1.0f, 0.001f), 0.0f,
                              juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                                  return juce::String(juce::roundToInt(v * 100.0f)) + " %";
                              })));
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "rev_color", 1 }, "Reverb Color",
                                                       juce::StringArray { "CLEAN", "TAPE" }, 0));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "rev_on", 1 }, "Reverb On", true));
    // SAMPLE's capture (28/10): its LENGTH (FREE, or bars from the host's next bar) and its AUTO LEVEL - the rig's, not
    // the sound's (a preset loaded keeps them)
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "cap_len", 1 }, "Capture Length",
                                                       juce::StringArray { "FREE", "1 BAR", "2 BARS", "4 BARS" }, 0,
                                                       juce::AudioParameterChoiceAttributes().withAutomatable(false)));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "cap_norm", 1 }, "Capture Auto Level", true));
    // the effects' slots (the FX view): FX A and FX B in series (A > B) or in parallel (A + B), an AUX (a send, the
    // effect wet only, a return), the loop's insert; each slot picks an effect - one effect in one slot at a time, its
    // parameters its own (the user, 21/10). The lists are not automatable: an effect added to them later never moves
    // what a recorded automation means
    {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup>("effects", "EFFECTS", "|");
        const juce::StringArray kinds { "NONE", "TEXTURE", "ROOM", "DISTORT", "IR", "VAPOR", "DELAY" };   // (FxKind's
                                                                                                           // order)
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "fx_on", 1 }, "Effects", true));
        g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "fx_route", 1 }, "Effects Routing",
                                                                 juce::StringArray { "A > B", "A // B", "B > A" }, 0,
                                                                 juce::AudioParameterChoiceAttributes().withAutomatable(false)));
        const char* names[kFxBuses] = { "FX A", "FX B", "AUX", "Loop Insert" };
        for (int b = 0; b < kFxBuses; ++b) {
            for (int s = 0; s < kFxSlotsPerBus; ++s) {
                const int def = s > 0 ? 0 : b == kBusA ? 1 : b == kBusB ? 2 : 0;   // (FX A 1 the texture, FX B 1 the
                                                                                    // room: the path as it was)
                g->addChild(std::make_unique<juce::AudioParameterChoice>(
                    juce::ParameterID { fxSlotTypeId(b, s), 1 }, juce::String(names[b]) + " " + juce::String(s + 1), kinds, def,
                    juce::AudioParameterChoiceAttributes().withAutomatable(false)));
            }
            g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { fxBusOnId(b), 1 },
                                                                   juce::String(names[b]) + " On", true));
        }
        for (const auto& [id, name] : { std::pair<const char*, const char*> { "fx_a_mix", "FX A Mix" }, { "fx_b_mix", "FX B Mix" } })
            g->addChild(std::make_unique<P>(juce::ParameterID { id, 1 }, name, R(0.0f, 1.0f, 0.001f), 1.0f,
                                            juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                                                return juce::String(juce::roundToInt(v * 100.0f)) + " %";
                                            })));
        g->addChild(std::make_unique<P>(juce::ParameterID { "aux_send", 1 }, "AUX Send", R(0.0f, 1.0f, 0.001f), 0.0f,
                                        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                                            return juce::String(juce::roundToInt(v * 100.0f)) + " %";
                                        })));
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "aux_pre", 1 }, "AUX Pre", false));
        g->addChild(std::make_unique<P>(juce::ParameterID { "aux_return", 1 }, "AUX Return", R(-36.0f, 6.0f, 0.1f), 0.0f,
                                        juce::AudioParameterFloatAttributes().withLabel("dB")));
        l.add(std::move(g));
    }
    // the texture (fx/Texture.h): the module's panel. Its knobs are positions 0..1, read through the module's own laws
    {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup>("texture", "TEXTURE", "|");
        const auto pctText = [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; };
        const auto amtText = [](float v, int) {
            const float a = texAmount(v);
            if (a == 0.0f) return juce::String("0");
            return juce::String(a > 0.0f ? "WALK " : "JITTER ") + juce::String(juce::roundToInt(std::fabs(a) * 100.0f)) + " %";
        };
        const auto knob = [&](const char* id, const char* name, float def, std::function<juce::String(float, int)> txt) {
            g->addChild(std::make_unique<P>(juce::ParameterID { id, 1 }, name, R(0.0f, 1.0f, 0.001f), def,
                                            juce::AudioParameterFloatAttributes().withStringFromValueFunction(txt)));
        };
        knob("tex_time", "Texture Time", 0.5f, pctText);
        knob("tex_size", "Texture Size", 0.5f, [](float v, int) {
            return v > 0.99f ? juce::String("DELAY") : juce::String(juce::roundToInt(v * 100.0f)) + " %";
        });
        knob("tex_shape", "Texture Shape", 0.5f, pctText);
        knob("tex_pitch", "Texture Pitch", 0.5f, [](float v, int) {
            const float s = texPitchSemitones(v);
            return (s > 0.0f ? "+" : "") + juce::String(s, std::fabs(s - std::round(s)) < 0.01f ? 0 : 1) + " st";
        });
        knob("tex_time_amt", "Texture Time Random", 0.5f, amtText);
        knob("tex_size_amt", "Texture Size Random", 0.5f, amtText);
        knob("tex_shape_amt", "Texture Shape Random", 0.5f, amtText);
        knob("tex_pitch_amt", "Texture Pitch Random", 0.5f, amtText);
        knob("tex_density", "Texture Density", 0.3f, [](float v, int) {
            if (v > 0.48f && v < 0.52f) return juce::String("NONE");
            return juce::String(v < 0.5f ? "EVEN " : "RANDOM ") + juce::String(juce::roundToInt(std::fabs(v - 0.5f) * 200.0f)) + " %";
        });
        knob("tex_feedback", "Texture Feedback", 0.0f, pctText);
        knob("tex_drywet", "Texture Dry/Wet", 0.5f, pctText);
        knob("tex_reverb", "Texture Reverb", 0.0f, pctText);
        knob("tex_gain", "Texture Input Gain", 0.0f, [](float v, int) {
            if (v <= 0.0f) return juce::String("AUTO");
            const float s = (float)(1 + juce::roundToInt(v * 254.0f)) / 255.0f;   // (gain_control.cc: the manual gain)
            return "x" + juce::String(1.0f + 39.0f * s * s * s * s, 2);
        });
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "tex_freeze", 1 }, "Texture Freeze", false));
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "tex_mono", 1 }, "Texture Mono", false));
        g->addChild(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { "tex_media", 1 }, "Texture Media",
            juce::StringArray { "DIGITAL", "VINTAGE", "TAPE", "WORN TAPE" }, 0));
        g->addChild(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { "tex_seed", 1 }, "Texture Seed",
            juce::StringArray { "INTERNAL", "NOTES", "NOTES DIV", "HOST CLOCK" }, 0));
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "tex_on", 1 }, "Texture", false));
        l.add(std::move(g));
    }
    // the distortion (fx/Distort.h): on, its mode (not automatable: a mode added later never moves what a recorded
    // automation means), DRIVE, CHARACTER (the mode's own), SYMMETRY, TONE, MIX, OUTPUT
    {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup>("distort", "DISTORT", "|");
        const auto pctText = [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; };
        const auto bipText = [](float v, int) {
            const int p = juce::roundToInt(v * 100.0f);
            return (p > 0 ? "+" : "") + juce::String(p);
        };
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "dist_on", 1 }, "Distort", true));
        juce::StringArray modes;
        for (int m = 0; m < usina::fx::Distort::kNumModes; ++m) modes.add(usina::fx::Distort::modeName(m));
        g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "dist_mode", 1 }, "Distort Mode",
                                                                 modes, 0,
                                                                 juce::AudioParameterChoiceAttributes().withAutomatable(false)));
        g->addChild(std::make_unique<P>(juce::ParameterID { "dist_drive", 1 }, "Distort Drive", R(0.0f, 48.0f, 0.1f), 12.0f,
                                        juce::AudioParameterFloatAttributes().withLabel("dB")));
        // (CHARACTER's text as the mode reads it: bits in CRUSH, BITS, COMPAND; half to full in RECTIFY; a share else)
        g->addChild(std::make_unique<P>(juce::ParameterID { "dist_char", 1 }, "Distort Character", R(0.0f, 1.0f, 0.001f),
                                        0.5f, juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                                                  [distMode](float v, int) {
                                                      return juce::String(usina::fx::Distort::characterText(
                                                          distMode->load(std::memory_order_relaxed), v));
                                                  })));
        g->addChild(std::make_unique<P>(juce::ParameterID { "dist_sym", 1 }, "Distort Symmetry", R(-1.0f, 1.0f, 0.001f),
                                        0.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction(bipText)));
        g->addChild(std::make_unique<P>(juce::ParameterID { "dist_tone", 1 }, "Distort Tone", R(-1.0f, 1.0f, 0.001f), 0.0f,
                                        juce::AudioParameterFloatAttributes().withStringFromValueFunction(bipText)));
        g->addChild(std::make_unique<P>(juce::ParameterID { "dist_mix", 1 }, "Distort Mix", R(0.0f, 1.0f, 0.001f), 1.0f,
                                        juce::AudioParameterFloatAttributes().withStringFromValueFunction(pctText)));
        g->addChild(std::make_unique<P>(juce::ParameterID { "dist_out", 1 }, "Distort Output", R(-24.0f, 12.0f, 0.1f), 0.0f,
                                        juce::AudioParameterFloatAttributes().withLabel("dB")));
        l.add(std::move(g));
    }
    // the IR (IrEffect.h): on, MIX, LOW CUT and HIGH CUT (at their ends: off), OUTPUT. The file is the session's
    {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup>("ir", "IR", "|");
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "ir_on", 1 }, "IR", true));
        g->addChild(std::make_unique<P>(juce::ParameterID { "ir_mix", 1 }, "IR Mix", R(0.0f, 1.0f, 0.001f), 1.0f,
                                        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                                            return juce::String(juce::roundToInt(v * 100.0f)) + " %";
                                        })));
        g->addChild(std::make_unique<P>(juce::ParameterID { "ir_lowcut", 1 }, "IR Low Cut", R(20.0f, 2000.0f, 1.0f, 0.3f), 20.0f,
                                        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                                            return v <= 20.5f ? juce::String("OFF") : juce::String(juce::roundToInt(v)) + " Hz";
                                        })));
        g->addChild(std::make_unique<P>(juce::ParameterID { "ir_highcut", 1 }, "IR High Cut", R(500.0f, 20000.0f, 1.0f, 0.3f),
                                        20000.0f,
                                        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                                            return v >= 19999.0f ? juce::String("OFF")
                                                                 : juce::String(v / 1000.0f, 1) + " kHz";
                                        })));
        g->addChild(std::make_unique<P>(juce::ParameterID { "ir_out", 1 }, "IR Output", R(-24.0f, 12.0f, 0.1f), 0.0f,
                                        juce::AudioParameterFloatAttributes().withLabel("dB")));
        l.add(std::move(g));
    }
    // VAPOR (fx/Vapor.h): the module's panel - its knobs are positions 0..1, read through its own laws; what each
    // does depends on the mode (the panel names them so). MODE and QUALITY not automatable (a mode added later never
    // moves what a recorded automation means; a change restarts the processor, faded)
    {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup>("vapor", "VAPOR", "|");
        const auto pctText = [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; };
        const char* names[kVapKnobs] = { "Vapor Position", "Vapor Size", "Vapor Pitch", "Vapor Density", "Vapor Texture",
                                         "Vapor Dry/Wet", "Vapor Spread", "Vapor Feedback", "Vapor Reverb" };
        const float defs[kVapKnobs] = { 0.2f, 0.5f, 0.5f, 0.7f, 0.5f, 0.5f, 0.3f, 0.0f, 0.3f };
        for (int i = 0; i < kVapKnobs; ++i) {
            std::function<juce::String(float, int)> txt = pctText;
            if (i == 2)
                txt = [](float v, int) {
                    const float s = usina::fx::Vapor::pitchSemitones(v);
                    return (s > 0.0f ? "+" : "") + juce::String(s, std::fabs(s - std::round(s)) < 0.05f ? 0 : 1) + " st";
                };
            g->addChild(std::make_unique<P>(juce::ParameterID { kVapIds[i], 1 }, names[i], R(0.0f, 1.0f, 0.001f), defs[i],
                                            juce::AudioParameterFloatAttributes().withStringFromValueFunction(txt)));
        }
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "vap_on", 1 }, "Vapor", true));
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "vap_freeze", 1 }, "Vapor Freeze", false));
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "vap_reverse", 1 }, "Vapor Reverse", false));
        // LIMIT (fx/Vapor.h, 28/10): the fed-back signal held near the input's level; off by default (the user)
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "vap_limit", 1 }, "Vapor Limit", false));
        juce::StringArray modes, qualities;
        for (int m = 0; m < usina::fx::Vapor::kNumModes; ++m) modes.add(usina::fx::Vapor::modeName(m));
        for (int q = 0; q < usina::fx::Vapor::kNumQualities; ++q) qualities.add(usina::fx::Vapor::qualityName(q));
        g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "vap_mode", 1 }, "Vapor Mode", modes, 0,
                                                                 juce::AudioParameterChoiceAttributes().withAutomatable(false)));
        g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "vap_quality", 1 }, "Vapor Quality",
                                                                 qualities, 0,
                                                                 juce::AudioParameterChoiceAttributes().withAutomatable(false)));
        g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "vap_trig", 1 }, "Vapor Trigger",
                                                                 juce::StringArray { "OFF", "NOTES", "HOST CLOCK" }, 1));
        l.add(std::move(g));
    }
    // DELAY (fx/Delay.h): on, TIME (free) or SYNC (a division of the host's tempo), FEEDBACK, the loop's LOW CUT and HIGH
    // CUT (at their ends: off), WOW (TAPE's), MIX, OUTPUT; CHARACTER and ROUTING (not automatable: a change resets
    // the time's glide, as a list is moved, not swept), FREEZE
    {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup>("delay", "DELAY", "|");
        const auto pctText = [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; };
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "dly_on", 1 }, "Delay", true));
        g->addChild(std::make_unique<P>(juce::ParameterID { "dly_time", 1 }, "Delay Time", R(1.0f, 4000.0f, 0.1f, 0.3f),
                                        375.0f,
                                        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                                            return v < 1000.0f ? juce::String(v, v < 100.0f ? 1 : 0) + " ms"
                                                               : juce::String(v / 1000.0f, 2) + " s";
                                        })));
        g->addChild(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { "dly_sync", 1 }, "Delay Sync",
            juce::StringArray { "FREE", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D",
                                "1/2", "1 BAR", "2 BARS", "4 BARS" },
            0));
        g->addChild(std::make_unique<P>(juce::ParameterID { "dly_feedback", 1 }, "Delay Feedback", R(0.0f, 1.1f, 0.001f),
                                        0.4f, juce::AudioParameterFloatAttributes().withStringFromValueFunction(pctText)));
        g->addChild(std::make_unique<P>(juce::ParameterID { "dly_lowcut", 1 }, "Delay Low Cut", R(20.0f, 2000.0f, 1.0f, 0.3f),
                                        20.0f,
                                        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                                            return v <= 20.5f ? juce::String("OFF") : juce::String(juce::roundToInt(v)) + " Hz";
                                        })));
        g->addChild(std::make_unique<P>(juce::ParameterID { "dly_highcut", 1 }, "Delay High Cut",
                                        R(500.0f, 20000.0f, 1.0f, 0.3f), 20000.0f,
                                        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                                            return v >= 19999.0f ? juce::String("OFF")
                                                                 : juce::String(v / 1000.0f, 1) + " kHz";
                                        })));
        g->addChild(std::make_unique<P>(juce::ParameterID { "dly_wow", 1 }, "Delay Wow", R(0.0f, 1.0f, 0.001f), 0.3f,
                                        juce::AudioParameterFloatAttributes().withStringFromValueFunction(pctText)));
        g->addChild(std::make_unique<P>(juce::ParameterID { "dly_flutter", 1 }, "Delay Flutter", R(0.0f, 1.0f, 0.001f),
                                        0.2f, juce::AudioParameterFloatAttributes().withStringFromValueFunction(pctText)));
        g->addChild(std::make_unique<P>(juce::ParameterID { "dly_mix", 1 }, "Delay Mix", R(0.0f, 1.0f, 0.001f), 0.35f,
                                        juce::AudioParameterFloatAttributes().withStringFromValueFunction(pctText)));
        g->addChild(std::make_unique<P>(juce::ParameterID { "dly_out", 1 }, "Delay Output", R(-24.0f, 12.0f, 0.1f), 0.0f,
                                        juce::AudioParameterFloatAttributes().withLabel("dB")));
        juce::StringArray chars, routes;
        for (int c = 0; c < usina::fx::Delay::kNumCharacters; ++c) chars.add(usina::fx::Delay::characterName(c));
        for (int r = 0; r < usina::fx::Delay::kNumRoutings; ++r) routes.add(usina::fx::Delay::routingName(r));
        g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "dly_character", 1 }, "Delay Character",
                                                                 chars, 0,
                                                                 juce::AudioParameterChoiceAttributes().withAutomatable(false)));
        g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "dly_routing", 1 }, "Delay Routing",
                                                                 routes, 0,
                                                                 juce::AudioParameterChoiceAttributes().withAutomatable(false)));
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "dly_freeze", 1 }, "Delay Freeze", false));
        l.add(std::move(g));
    }
    // the sequencer (src/seq): on; in a host it follows the host's transport and tempo, always; in the Standalone, its
    // own clock: its tempo and PLAY (not automatable: the Standalone has no automation, and a host never reads them).
    // (The CLOCK choice of 21/10 is gone, 22/10: a host's sequencer follows the host - a session that saved it loads
    // without it)
    {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup>("sequencer", "SEQUENCER", "|");
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "seq_on", 1 }, "Sequencer", false));
        g->addChild(std::make_unique<P>(juce::ParameterID { "seq_bpm", 1 }, "Sequencer Tempo", R(20.0f, 300.0f, 0.1f), 120.0f,
                                        juce::AudioParameterFloatAttributes().withLabel("BPM").withAutomatable(false)));
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "seq_play", 1 }, "Sequencer Play", false,
                                                               juce::AudioParameterBoolAttributes().withAutomatable(false)));
        l.add(std::move(g));
    }
    // the four macros: bipolar, the centre = the patch as it is. 1..3 move what the patch's A1..A3 point at (as the
    // device's A1..A3 knobs: any value of the patch, or the modulators' ratio, FB, the carriers' detune); 4: the
    // modulation matrix's (its stage)
    for (int m = 1; m <= 4; ++m)
        l.add(std::make_unique<P>(juce::ParameterID { "macro" + juce::String(m), 1 }, "Macro " + juce::String(m),
                                  R(-1.0f, 1.0f, 0.001f), 0.0f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                                      const int p = juce::roundToInt(v * 100.0f);
                                      return (p > 0 ? "+" : "") + juce::String(p) + " %";
                                  })));
    // the engine's oversampling: everything in the voice at N x (FM, folds, shapers, effects, filters), one
    // decimation after. Not automatable (a change re-prepares the engine). 1x: the voice filter the firmware's way
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "oversampling", 1 }, "Oversampling", juce::StringArray { "1x", "2x", "4x", "8x", "16x" }, 2,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)));

    // the patch: its 256 values, as the preset language names them, grouped as the host lists them (each operator,
    // then the patch's); the IDs from the names (FmSchema: "op1_level", "mx_1to3") never change (rule 19)
    const auto& s = usina::fm::schema();
    std::array<std::unique_ptr<juce::AudioProcessorParameterGroup>, 8> groups;
    for (int k = 0; k < 6; ++k)
        groups[(size_t)k] = std::make_unique<juce::AudioProcessorParameterGroup>("op" + juce::String(k + 1),
                                                                                "OP" + juce::String(k + 1), "|");
    groups[6] = std::make_unique<juce::AudioProcessorParameterGroup>("patch", "PATCH", "|");
    groups[7] = std::make_unique<juce::AudioProcessorParameterGroup>("mod", "MOD", "|");   // (the Usina's own)
    std::set<std::string> ids;
    for (int i = 0; i < usina::fm::kNumValues; ++i) {
        const auto& d = s[(size_t)i];
        [[maybe_unused]] const bool fresh = ids.insert(d.id).second;
        jassert(fresh);                                  // (two names may never make one ID)
        groups[(size_t)(i < 6 * usina::fm::kOpValues ? i / usina::fm::kOpValues : i < usina::fm::kFwValues ? 6 : 7)]->addChild(
            std::make_unique<juce::AudioParameterInt>(juce::ParameterID { d.id, 1 }, d.name, d.min, d.max, d.def));
    }
    for (auto& g : groups) l.add(std::move(g));
    // Append performance parameters: preserve existing parameter/port order as well as stable IDs (LV2 sessions).
    l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { "bend_range", 1 }, "Pitch Bend Range (st)", 1, 48, 2));
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "mpe_mode", 1 }, "MPE Input", juce::StringArray { "OFF", "LOWER ZONE (CH 1 MASTER)" }, 0));
    l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { "mpe_bend_range", 1 }, "MPE Note Bend Range (st)", 1, 96, 48));
    return l;
}

bool UsinaProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    for (const auto& in : layouts.inputBuses)            // each input: off, mono or stereo
        if (!in.isDisabled() && in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
            return false;
    return true;
}

void UsinaProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    for (auto& line : fbLine)                            // everything sized here, nothing in processBlock (rule 6)
        line.assign((size_t)(kMaxFbSeconds * sampleRate) + 2, 0.0f);
    fbPos = 0;
    for (auto& g : fbGuard)
        g.prepare(sampleRate);
    guard.prepare(sampleRate, maximumExpectedSamplesPerBlock);
    howl.prepare(sampleRate);
    howlIn.prepare(sampleRate);
    inGainBuf.assign((size_t)juce::jmax(1, maximumExpectedSamplesPerBlock), 0.0f);
    inFxBuf.assign(inGainBuf.size(), 0.0f);
    room.prepare(sampleRate);
    texture.prepare(sampleRate, maximumExpectedSamplesPerBlock);
    distort.prepare(sampleRate);
    ir.prepare(sampleRate, maximumExpectedSamplesPerBlock);
    vapor.prepare(sampleRate, maximumExpectedSamplesPerBlock);
    delayFx.prepare(sampleRate);
    for (auto* b : { &fxDryL, &fxDryR, &fxAL, &fxAR, &fxBL, &fxBR, &fxAuxL, &fxAuxR, &fxLoopL, &fxLoopR, &fxParL, &fxParR })
        b->assign((size_t)juce::jmax(1, maximumExpectedSamplesPerBlock), 0.0f);
    {                                                    // (the buses' match: up to 3 latent effects in a bus)
        const int maxLatency = juce::jmax(4096, (int)std::ceil(0.08 * sampleRate));
        for (auto* a : { &matchDry[0], &matchDry[1], &matchPad[0], &matchPad[1], &matchPar }) a->prepare(sampleRate, maxLatency);
    }
    capArmed.store(false);                               // (SAMPLE's capture: kCaptureSeconds at this rate)
    capBuf.assign((size_t)(kCaptureSeconds * sampleRate) + 1, 0.0f);
    capPos.store(0);
    capRate = sampleRate;
    seq.prepare(sampleRate);
    seqEvents.assign(1024, {});
    seqMidi.ensureSize(32768);                          // (the block's merged MIDI: never grown on the audio thread)
    ctrlMidi.ensureSize(32768);
    cabMidi.ensureSize(32768);
    texGateLen = juce::jmax(1, maximumExpectedSamplesPerBlock);
    texGate.reset(new bool[(size_t)texGateLen]());
    vapGate.reset(new bool[(size_t)texGateLen]());
    hostRate = sampleRate;
    hostBlock = maximumExpectedSamplesPerBlock;
    prepareEngine();
    setLatencySamples(guard.latencySamples());           // rule 8

    const double ramp = 0.03;
    inGain.reset(sampleRate, ramp);
    inMix.reset(sampleRate, ramp);
    inFxMix.reset(sampleRate, ramp);
    feedback.reset(sampleRate, ramp);
    level.reset(sampleRate, ramp);
    fbTimeSamples.reset(sampleRate, 0.2);                // (a delay time glides, it does not jump)
    inGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(pInGain->load()));
    inMix.setCurrentAndTargetValue(pInMix->load());
    inFxMix.setCurrentAndTargetValue(pInFx->load() > 0.5f ? 1.0f : 0.0f);
    for (int b = 0; b < 3; ++b) {
        busGain[(size_t)b].reset(sampleRate, 0.02);
        busGain[(size_t)b].setCurrentAndTargetValue(busTarget(b));
    }
    feedback.setCurrentAndTargetValue(pFeedback->load());
    level.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(pLevel->load()));
    fbTimeSamples.setCurrentAndTargetValue((float)(pFbTime->load() * 0.001 * sampleRate));
}

void UsinaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;                 // rule 9
    bypassed = false;
    const int n = buffer.getNumSamples();
    const double fs = getSampleRate();

    // the input bus, read before the output overwrites the shared channels
    auto input = getBusBuffer(buffer, true, 0);
    const int inCh = getBus(true, 0) != nullptr && getBus(true, 0)->isEnabled() ? input.getNumChannels() : 0;
    auto out = getBusBuffer(buffer, false, 0);

    // the patch's parameters and the macros: a change (an edit, automation, a preset, a macro turned) rebuilds the
    // patch, nothing allocated; the held notes keep sounding (rule 12)
    {
        std::array<int, usina::fm::kNumValues> now;
        readPatch(now);
        int mac[3];
        for (int m = 0; m < 3; ++m) mac[m] = juce::jlimit(-64, 63, juce::roundToInt(pMacro[(size_t)m]->load() * 64.0f));
        if (now != engineValues || mac[0] != engineMacros[0] || mac[1] != engineMacros[1] || mac[2] != engineMacros[2]) {
            engineValues = now;
            for (int m = 0; m < 3; ++m) engineMacros[(size_t)m] = mac[m];
            engine.setPatch(usina::fm::Patch::fromArrayWithMacros(engineValues.data(), mac));
        }
        float m4[4];                                     // the macros as modulation sources (-1..1)
        for (int m = 0; m < 4; ++m) m4[m] = pMacro[(size_t)m]->load();
        engine.setMacroSources(m4);
    }
    {                                                    // new routes: taken if the lock is free now (never waits)
        const juce::SpinLock::ScopedTryLockType tl(routesLock);
        if (tl.isLocked() && routesDirty) {
            engine.setRoutes(routesPending);             // (the voices' routes: the engine keeps only those)
            routesAudio = routesPending;                 // (and the effects' knobs: kFxDst, moved here)
            for (int k = 0; k < 3; ++k) engine.setFunctionShape(k, funcPending[(size_t)k]);
            routesDirty = false;
        }
    }
    {                                                    // the host's transport, read fresh each block (rule 10)
        double bpm = 120.0, ppq = 0.0;
        bool playing = false, ppqValid = false;
        if (auto* ph = getPlayHead())
            if (const auto pos = ph->getPosition()) {
                if (const auto b = pos->getBpm()) bpm = *b;
                if (const auto q = pos->getPpqPosition()) {
                    ppq = *q;
                    ppqValid = true;
                }
                if (const auto ts = pos->getTimeSignature(); ts && ts->numerator > 0 && ts->denominator > 0)
                    tpBarBeats = ts->numerator * 4.0 / ts->denominator;
                playing = pos->getIsPlaying();
            }
        engine.setTransport(bpm, ppq, playing);
        tpBpmNow = bpm;
        tempoForUi.store(bpm);
        tpPpqNow = ppq;
        tpPlayingNow = playing;
        tpPpqValidNow = ppqValid;
    }
    // the sequencer (src/seq): its notes join the MIDI that came in, before anything reads it (the voices, the
    // texture's SEED); its modulation lanes are the matrix's SEQ 1..4. Clocked by the host's transport, or (INTERNAL)
    // by its own tempo from the moment it is switched on
    // the controllers (CTRL 1..8): their values from what came in (a CC's value, a note's velocity while held), LEARN
    // taking the next CC or note; the notes mapped to one taken out of what plays (a pad that is a controller does not
    // also play a note). Once a block: the matrix reads its sources once a control block anyway
    const juce::MidiBuffer* incoming = &midi;
    {
        const auto slotOf = [this](bool cc, int num, int ch, int i) {
            if (ctrlNum[(size_t)i].load() != num || ctrlType[(size_t)i].load() != (cc ? 0 : 1)) return false;
            const int c = ctrlChan[(size_t)i].load();
            return c == 0 || c == ch;
        };
        bool consumes = false;
        for (const auto meta : midi) {
            const auto msg = meta.getMessage();
            const bool cc = msg.isController(), on = msg.isNoteOn(), off = msg.isNoteOff();
            if (msg.isProgramChange()) {                 // (a preset: bank * 128 + program; run by the message thread)
                const int index = pcBank * 128 + msg.getProgramChangeNumber();
                actFifo.write(1).forEach([&](int i) {
                    actQueue[(size_t)i] = kProgramEvent | 1 << 8 | index << 9;
                    actTime[(size_t)i] = 0.0;
                });
                continue;
            }
            if (cc && msg.getControllerNumber() == 0) pcBank = juce::jlimit(0, 127, msg.getControllerValue());
            if (msg.isAftertouch()) {                    // (a pad's pressure: a held macro follows it - Cabine.h)
                for (int a : { (int)Cabine::kMacro1Hold, (int)Cabine::kMacro2Hold })
                    if (actionMatch(a, 1, msg.getNoteNumber(), msg.getChannel()))
                        queueAction(a | Cabine::kPressureFlag, true, msg.getAfterTouchValue() / 127.0f);
                continue;
            }
            if (!cc && !on && !off) continue;
            const int num = cc ? msg.getControllerNumber() : msg.getNoteNumber(), ch = msg.getChannel();
            const int learn = ctrlLearnSlot.load();
            if (learn >= 0 && (cc || on)) {
                ctrlType[(size_t)learn].store(cc ? 0 : 1);
                ctrlNum[(size_t)learn].store(num);
                ctrlChan[(size_t)learn].store(ch);
                ctrlLearnSlot.store(-1);
            }
            // MIDI learn on a parameter: the CC waited for binds (its old map, if it had one, replaced); a mapped CC's
            // value recorded for the message thread (applyCcMaps)
            if (cc) {
                if (auto* lp = ccLearn.load()) {
                    CcMap* slot = nullptr;
                    for (auto& m : ccMaps)
                        if (m.param.load() == lp) slot = &m;
                    for (auto& m : ccMaps)
                        if (slot == nullptr && m.param.load() == nullptr) slot = &m;
                    if (slot != nullptr) {
                        slot->cc.store(num);
                        slot->channel.store(ch);
                        slot->param.store(lp);
                    }
                    ccLearn.store(nullptr);
                }
                for (auto& m : ccMaps)
                    if (m.cc.load() == num && m.param.load() != nullptr) {
                        const int c = m.channel.load();
                        if (c == 0 || c == ch) m.value.store(msg.getControllerValue() / 127.0f);
                    }
            }
            for (int i = 0; i < kCtrlSlots; ++i)
                if (slotOf(cc, num, ch, i)) {
                    ctrlVal[i] = cc ? msg.getControllerValue() / 127.0f : on ? msg.getVelocity() / 127.0f : 0.0f;
                    consumes = consumes || !cc;
                }
            // the CABINE's actions: LEARN takes this one; a bound one queued for the message thread (pressed: a note
            // on, a CC above 0; let go: the note off, the CC at 0 - the held actions need both); a note that is an
            // action does not play
            {
                const int type = cc ? 0 : 1;
                const bool pressed = cc ? msg.getControllerValue() > 0 : on;
                const float val = cc ? msg.getControllerValue() / 127.0f : on ? msg.getVelocity() / 127.0f : 0.0f;
                const int al = actLearn.load();
                if (al >= 0 && pressed) {
                    actType[(size_t)al].store(type);
                    actNum[(size_t)al].store(num);
                    actChan[(size_t)al].store(ch);
                    actLearn.store(-1);
                }
                for (int a = 0; a < kCabActions; ++a)
                    if (actionMatch(a, type, num, ch)) {
                        queueAction(a, pressed, val);
                        consumes = consumes || !cc;
                    }
            }
        }
        if (consumes) {
            ctrlMidi.clear();
            for (const auto meta : midi) {
                const auto msg = meta.getMessage();
                bool mapped = false;
                if (msg.isNoteOnOrOff()) {
                    for (int i = 0; i < kCtrlSlots && !mapped; ++i) mapped = slotOf(false, msg.getNoteNumber(), msg.getChannel(), i);
                    for (int a = 0; a < kCabActions && !mapped; ++a) mapped = actionMatch(a, 1, msg.getNoteNumber(), msg.getChannel());
                }
                if (!mapped) ctrlMidi.addEvent(msg, meta.samplePosition);
            }
            incoming = &ctrlMidi;
        }
        engine.setCtrlSources(ctrlVal);
    }
    // the CABINE's notes (DRUMS, KEYS: the Launchpad's, the screen's) join what came, at the block's start
    if (cabNoteFifo[0].getNumReady() + cabNoteFifo[1].getNumReady() > 0) {
        cabMidi.clear();
        cabMidi.addEvents(*incoming, 0, -1, 0);
        for (int q = 0; q < 2; ++q)
            cabNoteFifo[(size_t)q].read(cabNoteFifo[(size_t)q].getNumReady()).forEach([&](int i) {
                const int e = cabNoteQueue[(size_t)q][(size_t)i];
                const int note = e & 0x7F, vel = (e >> 7) & 0x7F;
                cabMidi.addEvent((e >> 14) & 1 ? juce::MidiMessage::noteOn(1, note, (juce::uint8)juce::jmax(1, vel))
                                               : juce::MidiMessage::noteOff(1, note),
                                 0);
            });
        incoming = &cabMidi;
    }
    const juce::MidiBuffer* events = incoming;
    {
        usina::seq::Transport t;
        const bool seqOn = pSeqOn->load() > 0.5f;
        // its own clock in the Standalone, which has no transport to follow: PLAY runs it (from the top each time); in
        // a host, the host's transport does
        if (seqOn && seqInternalClock()) {
            const double b = pSeqBpm->load();
            const bool play = pSeqPlay->load() > 0.5f;
            if (!internalWasOn) internalPpq = 0.0;
            internalWasOn = play;
            t.bpm = b;
            t.ppq = internalPpq;
            t.ppqValid = true;
            t.playing = play;
            if (play) internalPpq += n / fs * b / 60.0;
        } else {
            internalWasOn = false;
            t.bpm = tpBpmNow;
            t.ppq = tpPpqNow;
            t.ppqValid = tpPpqValidNow;
            t.playing = seqOn && tpPlayingNow;
        }
        const int k = seq.process(n, t, seqEvents.data(), (int)seqEvents.size());
        if (k > 0) {
            seqMidi.clear();
            seqMidi.addEvents(*incoming, 0, n, 0);
            for (int i = 0; i < k; ++i) {
                const auto& e = seqEvents[(size_t)i];
                seqMidi.addEvent(e.on ? juce::MidiMessage::noteOn(1, e.note, (juce::uint8)e.velocity)
                                      : juce::MidiMessage::noteOff(1, e.note),
                                 e.sample);
            }
            events = &seqMidi;
        }
        float lanes[4];
        for (int m = 0; m < 4; ++m) lanes[m] = seq.laneValue(m);
        engine.setSeqSources(lanes);
    }
    {                                                    // the input's level (after its gain), followed: COMB INPUT
        float pk = 0.0f;                                 // (the block's peak; up at once, down over ~150 ms)
        for (int c = 0; c < juce::jmin(inCh, 2); ++c)
            pk = juce::jmax(pk, input.getMagnitude(c, 0, n));
        pk = juce::jmin(1.0f, pk * juce::Decibels::decibelsToGain(pInGain->load()));
        const float rel = std::exp(-(float)n / (float)(0.15 * fs));
        inputEnv = pk > inputEnv ? pk : pk + (inputEnv - pk) * rel;
        engine.setInputLevel(inputEnv);
    }
    // the engine over the block at N x the host's rate, split at each MIDI event (sample-accurate: an event at
    // sample s starts at N s), then decimated to the host's rate. A host block longer than prepareToPlay announced
    // breaks its contract: the engine renders what was prepared, the rest stays silent
    const int os = preparedOs;                           // (the factor the engine and buffers were prepared for)
    const int capHost = juce::jmin(n, (int)engL.size() / os);
    std::fill(engL.begin(), engL.begin() + capHost * os, 0.0f);
    std::fill(engR.begin(), engR.begin() + capHost * os, 0.0f);
    jassert(n <= capHost);
    int done = 0;
    const auto renderTo = [&](int upTo) {                // (upTo: the host's samples)
        upTo = juce::jmin(upTo, capHost) * os;
        if (upTo > done) {
            engine.render(engL.data() + done, engR.data() + done, upTo - done);
            done = upTo;
        }
    };
    performanceMidi.configure(pMpeMode->load() > .5f, pBendRange->load(), pMpeBendRange->load());
    for (const auto meta : *events) {
        const auto msg = meta.getMessage();
        renderTo(juce::jlimit(0, n, meta.samplePosition));
        const auto* bytes = msg.getRawData();
        if (msg.getRawDataSize() >= 2 && bytes[0] < 0xf0)
            performanceMidi.process(bytes[0], bytes[1], msg.getRawDataSize() >= 3 ? bytes[2] : 0);
    }
    renderTo(n);
    for (int i = 0; i < capHost; ++i) {                  // N x down to the host's rate, in place (i <= i N)
        engL[(size_t)i] = decim[0].process(engL.data() + (size_t)i * os);
        engR[(size_t)i] = decim[1].process(engR.data() + (size_t)i * os);
    }
    const int cap = capHost;
    // the texture, then the voices' room, at the host's rate (each converts to its own: fx/Texture.h, RoomReverb.h)
    const int notesHeldAtStart = texNotesHeld;          // (the count the TRIG inputs start the block from)
    {
        usina::fx::Texture::Knobs k;
        float* const f[13] = { &k.time, &k.size, &k.shape, &k.pitch, &k.timeAmt, &k.sizeAmt, &k.shapeAmt,
                               &k.pitchAmt, &k.density, &k.feedback, &k.dryWet, &k.reverb, &k.gain };
        for (int i = 0; i < 13; ++i) *f[i] = pTex[i]->load();
        // the matrix on the texture's knobs (FmMod.h kFxDst): each route moves its knob by amount x the source, on
        // the knob's whole travel, from the sources as the newest voice has them (the free LFOs, RAND, the macros
        // and the controllers move it with no note too); read once a host block - the module's own knob smoothing
        // (pot_channel.h) takes the steps out
        {
            float* const dsts[usina::fm::FD_TEX_REVERB + 1] = { &k.time, &k.size, &k.shape, &k.pitch, &k.density,
                                                        &k.feedback, &k.dryWet, &k.reverb };
            bool moved = false;
            for (int i = 0; i < routesAudio.n; ++i) {
                const auto& r = routesAudio.r[(size_t)i];
                if (!usina::fm::isFxDst(r.dst) || r.dst > usina::fm::kFxDst + usina::fm::FD_TEX_REVERB) continue;
                *dsts[r.dst - usina::fm::kFxDst] += r.amount * engine.liveSource(r.src);
                moved = true;
            }
            if (moved)
                for (auto* d : dsts) *d = juce::jlimit(0.0f, 1.0f, *d);
        }
        k.freeze = pTex[13]->load() > 0.5f;
        k.mono = pTex[14]->load() > 0.5f;
        k.media = usina::fx::Texture::kDigital - juce::jlimit(0, 3, juce::roundToInt(pTex[15]->load()));
        k.seedMode = juce::jlimit(0, 3, juce::roundToInt(pTex[16]->load()));
        const bool on = pTex[17]->load() > 0.5f;
        // its SEED input: a note held (NOTES, NOTES DIV), or the host's beats (HOST CLOCK: high over the first tenth
        // of each beat, while the transport plays). The notes held are counted always (the count stays true while
        // the texture is off)
        const bool needGate = (on || texture.active()) && k.seedMode != usina::fx::Texture::kSeedInternal &&
                              cap <= texGateLen;
        const bool notesGate = needGate && k.seedMode != usina::fx::Texture::kSeedHostClock;
        int at = 0;
        for (const auto meta : *events) {
            const int s = juce::jlimit(0, cap, meta.samplePosition);
            if (notesGate)
                for (; at < s; ++at) texGate[(size_t)at] = texNotesHeld > 0;
            const auto msg = meta.getMessage();
            if (msg.isNoteOn()) ++texNotesHeld;
            else if (msg.isNoteOff()) texNotesHeld = juce::jmax(0, texNotesHeld - 1);
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) texNotesHeld = 0;
        }
        if (notesGate)
            for (; at < cap; ++at) texGate[(size_t)at] = texNotesHeld > 0;
        if (needGate && !notesGate) {
            const double beatsPerSample = tpBpmNow / 60.0 / fs;
            for (int i = 0; i < cap; ++i) {
                const double q = tpPpqNow + i * beatsPerSample;
                texGate[(size_t)i] = tpPlayingNow && q - std::floor(q) < 0.1;
            }
        }
        texKnobs = k;                                    // (the slots run it wherever it sits: runFx)
        texOnNow = on;
        texGateNow = needGate ? texGate.get() : nullptr;
    }
    // the distortion's settings of the block, the matrix on its knobs as on the texture's (each on its 0..1 travel;
    // the effect glides them inside the block: fx/Distort.h)
    {
        float v[5];
        for (int i = 0; i < 5; ++i) v[i] = pDistKnob[(size_t)i]->getValue();
        const int first = usina::fm::kFxDst + usina::fm::FD_DIST_DRIVE;
        for (int i = 0; i < routesAudio.n; ++i) {
            const auto& r = routesAudio.r[(size_t)i];
            if (r.dst >= first && r.dst < first + 5) v[r.dst - first] += r.amount * engine.liveSource(r.src);
        }
        float* const to[5] = { &distNow.driveDb, &distNow.character, &distNow.symmetry, &distNow.tone, &distNow.mix };
        for (int i = 0; i < 5; ++i) *to[i] = pDistKnob[(size_t)i]->convertFrom0to1(juce::jlimit(0.0f, 1.0f, v[i]));
        distNow.mode = juce::jlimit(0, usina::fx::Distort::kNumModes - 1, juce::roundToInt(pDistMode->load()));
        distNow.outputDb = pDistOut->load();
        distOnNow = pDistOn->load() > 0.5f;
    }
    // the IR's, the same way
    {
        float v[3];
        for (int i = 0; i < 3; ++i) v[i] = pIrKnob[(size_t)i]->getValue();
        const int first = usina::fm::kFxDst + usina::fm::FD_IR_MIX;
        for (int i = 0; i < routesAudio.n; ++i) {
            const auto& r = routesAudio.r[(size_t)i];
            if (r.dst >= first && r.dst < first + 3) v[r.dst - first] += r.amount * engine.liveSource(r.src);
        }
        float* const to[3] = { &irNow.mix, &irNow.lowCutHz, &irNow.highCutHz };
        for (int i = 0; i < 3; ++i) *to[i] = pIrKnob[(size_t)i]->convertFrom0to1(juce::jlimit(0.0f, 1.0f, v[i]));
        irNow.outputDb = pIrOut->load();
        irOnNow = pIrOn->load() > 0.5f;
    }
    // VAPOR's, the same way (its knobs 0..1, the module's own smoothing takes the steps out: fx/Vapor.h), and its TRIG
    // input as the texture's SEED: a note held (NOTES), or the host's beats (HOST CLOCK: high over the first tenth of
    // each beat, while the transport plays)
    {
        usina::fx::Vapor::Knobs k;
        float* const f[kVapKnobs] = { &k.position, &k.size, &k.pitch, &k.density, &k.texture, &k.dryWet, &k.spread,
                                      &k.feedback, &k.reverb };
        for (int i = 0; i < kVapKnobs; ++i) *f[i] = pVap[i]->load();
        const int first = usina::fm::kFxDst + usina::fm::FD_VAP_POSITION;
        for (int i = 0; i < routesAudio.n; ++i) {
            const auto& r = routesAudio.r[(size_t)i];
            if (r.dst >= first && r.dst < first + kVapKnobs) *f[r.dst - first] += r.amount * engine.liveSource(r.src);
        }
        for (auto* v : f) *v = juce::jlimit(0.0f, 1.0f, *v);
        k.freeze = pVap[kVapFreeze]->load() > 0.5f;
        k.reverse = pVap[kVapReverse]->load() > 0.5f;
        k.limit = pVap[kVapLimit]->load() > 0.5f;
        k.mode = juce::jlimit(0, usina::fx::Vapor::kNumModes - 1, juce::roundToInt(pVap[kVapMode]->load()));
        k.quality = juce::jlimit(0, usina::fx::Vapor::kNumQualities - 1, juce::roundToInt(pVap[kVapQuality]->load()));
        const bool on = pVap[kVapOn]->load() > 0.5f;
        const int trig = juce::jlimit(0, 2, juce::roundToInt(pVap[kVapTrig]->load()));
        const bool needGate = (on || vapor.active()) && trig != 0 && cap <= texGateLen;
        if (needGate && trig == 1) {
            int held = notesHeldAtStart, at = 0;
            for (const auto meta : *events) {
                const int s = juce::jlimit(0, cap, meta.samplePosition);
                for (; at < s; ++at) vapGate[(size_t)at] = held > 0;
                const auto msg = meta.getMessage();
                if (msg.isNoteOn()) ++held;
                else if (msg.isNoteOff()) held = juce::jmax(0, held - 1);
                else if (msg.isAllNotesOff() || msg.isAllSoundOff()) held = 0;
            }
            for (; at < cap; ++at) vapGate[(size_t)at] = held > 0;
        } else if (needGate) {
            const double beatsPerSample = tpBpmNow / 60.0 / fs;
            for (int i = 0; i < cap; ++i) {
                const double q = tpPpqNow + i * beatsPerSample;
                vapGate[(size_t)i] = tpPlayingNow && q - std::floor(q) < 0.1;
            }
        }
        vapKnobs = k;
        vapOnNow = on;
        vapGateNow = needGate ? vapGate.get() : nullptr;
    }
    // DELAY's, as the distortion's (the knobs on their 0..1 travel, the effect glides them), SYNC on the host's tempo
    {
        float v[7];
        for (int i = 0; i < 7; ++i) v[i] = pDlyKnob[(size_t)i]->getValue();
        const int first = usina::fm::kFxDst + usina::fm::FD_DLY_TIME;
        for (int i = 0; i < routesAudio.n; ++i) {
            const auto& r = routesAudio.r[(size_t)i];
            if (r.dst >= first && r.dst < first + 7) v[r.dst - first] += r.amount * engine.liveSource(r.src);
        }
        float* const to[7] = { &dlyNow.timeMs, &dlyNow.feedback, &dlyNow.lowCutHz, &dlyNow.highCutHz, &dlyNow.wow,
                               &dlyNow.mix, &dlyNow.flutter };
        for (int i = 0; i < 7; ++i) *to[i] = pDlyKnob[(size_t)i]->convertFrom0to1(juce::jlimit(0.0f, 1.0f, v[i]));
        dlyNow.sync = juce::jlimit(0, 14, juce::roundToInt(pDlySync->load()));
        dlyNow.bpm = tpBpmNow;
        dlyNow.character = juce::jlimit(0, usina::fx::Delay::kNumCharacters - 1, juce::roundToInt(pDlyChar->load()));
        dlyNow.routing = juce::jlimit(0, usina::fx::Delay::kNumRoutings - 1, juce::roundToInt(pDlyRoute->load()));
        dlyNow.freeze = pDlyFreeze->load() > 0.5f;
        dlyNow.outputDb = pDlyOut->load();
        dlyOnNow = pDlyOn->load() > 0.5f;
    }
    // the input's gain over the block, sample by sample (the send into the effects and the per-sample loop below
    // both read it), and INPUT -> FX: its share into the effects, crossfaded (30 ms) with the path after them. Sent,
    // it passes its own howl guard first: a microphone howling never reaches the effects unnotched
    inGain.setTargetValue(juce::Decibels::decibelsToGain(pInGain->load()));
    inMix.setTargetValue(pInMix->load());
    inFxMix.setTargetValue(pInFx->load() > 0.5f ? 1.0f : 0.0f);
    const int gainLen = juce::jmin(n, (int)inGainBuf.size());
    for (int i = 0; i < gainLen; ++i) {
        inGainBuf[(size_t)i] = inGain.getNextValue() * inMix.getNextValue();
        inFxBuf[(size_t)i] = inFxMix.getNextValue();
    }
    const float* inL = inCh > 0 ? input.getReadPointer(0) : nullptr;
    const float* inR = inCh > 1 ? input.getReadPointer(1) : inL;
    int capFrom = 0;                                     // (where in this block the capture starts)
    if (capWait.load(std::memory_order_acquire)) {       // armed with a LENGTH: from the host's next bar (or at once)
        const double perBeat = 60.0 / juce::jmax(20.0, tpBpmNow) * getSampleRate();
        bool go = true;
        if (tpPlayingNow && tpPpqValidNow) {
            const double nextBar = std::ceil(tpPpqNow / tpBarBeats - 1e-9) * tpBarBeats;
            const int at = (int)std::ceil((nextBar - tpPpqNow) * perBeat - 1e-6);   // (the bar's first sample)
            go = at < n;
            capFrom = juce::jlimit(0, juce::jmax(0, n - 1), at);
        }
        if (go) {
            const int bars = capBars.load(std::memory_order_relaxed);
            capTarget.store(juce::jmin((int)capBuf.size(), (int)std::lround(bars * tpBarBeats * perBeat)),
                            std::memory_order_relaxed);
            capPos.store(0, std::memory_order_relaxed);
            capWait.store(false, std::memory_order_relaxed);
            capArmed.store(true, std::memory_order_release);
        }
    }
    if (capArmed.load(std::memory_order_acquire) && inL != nullptr) {   // SAMPLE's capture: the input after IN GAIN,
        const float g = 0.5f * juce::Decibels::decibelsToGain(pInGain->load());   // mono (before IN MIX)
        int p = capPos.load(std::memory_order_relaxed);
        const int end = juce::jmin((int)capBuf.size(), capTarget.load(std::memory_order_relaxed));
        const int room = juce::jmin(n - capFrom, end - p);
        for (int i = 0; i < room; ++i) capBuf[(size_t)(p + i)] = (inL[capFrom + i] + inR[capFrom + i]) * g;
        p += juce::jmax(0, room);
        capPos.store(p, std::memory_order_release);
        if (p >= end) {                                  // (full, or its bars done: stopped by itself)
            capFull.store(true, std::memory_order_release);
            capArmed.store(false, std::memory_order_release);
        }
    }
    const bool toFx = inL != nullptr && (inFxMix.isSmoothing() || inFxMix.getTargetValue() > 0.0f);
    const int toFxLen = toFx ? juce::jmin(gainLen, cap, (int)fxDryL.size()) : 0;
    for (int i = 0; i < toFxLen; ++i) {
        const float g = inGainBuf[(size_t)i] * inFxBuf[(size_t)i];
        float xl = inL[i] * g, xr = inR[i] * g;
        howlIn.process(xl, xr);
        engL[(size_t)i] += xl;
        engR[(size_t)i] += xr;
    }
    if (!toFx && howlIn.activeNotches() > 0) howlIn.reset();   // (not sent: its notches let go)
    // the effects' buses (the FX view): FX A, FX B, the AUX, the loop's insert - three slots each, run in order. Each
    // effect in one slot at a time (scanning A, B, AUX, LOOP, a later slot asking for one already placed gets none:
    // the editor moves an effect, never copies it). FX A and FX B in series (A > B, B > A) or in parallel (A // B: what
    // each adds to the dry, summed - the dry once), each with its MIX (the bus's dry/wet) and its power (faded, 20 ms);
    // the AUX: a send (before or after A and B), its effects wet only, a return; the loop's insert below. An effect in
    // no running bus does not run (it stops, emptied). Effects off: none of them
    int kinds[kFxBuses][kFxSlotsPerBus];
    bool busRuns[kFxBuses];
    {
        const bool fxOn = pFxOn->load() > 0.5f;
        bool seen[kNumFxKinds] {};
        for (int b = 0; b < kFxBuses; ++b) {
            bool any = false;
            for (int s = 0; s < kFxSlotsPerBus; ++s) {
                int k = fxOn ? juce::jlimit(0, (int)kNumFxKinds - 1, juce::roundToInt(pFxType[(size_t)b][(size_t)s]->load()))
                             : (int)kFxNone;
                if (k != kFxNone) {
                    if (seen[k]) k = kFxNone;
                    else seen[k] = true;
                }
                kinds[b][s] = k;
                any = any || k != kFxNone;
            }
            if (b < 3) {                                 // (a bus with nothing in it: its gain where it is going)
                auto& g = busGain[(size_t)b];
                if (!any) g.setCurrentAndTargetValue(busTarget(b));
                else g.setTargetValue(busTarget(b));
                busRuns[b] = any && (g.isSmoothing() || g.getTargetValue() > 0.0f);
            } else {
                busRuns[b] = any && pBusOn[(size_t)b]->load() > 0.5f;
            }
        }
    }
    {
        const auto placed = [&](int kind) {
            for (int b = 0; b < kFxBuses; ++b)
                for (int s = 0; s < kFxSlotsPerBus; ++s)
                    if (busRuns[b] && kinds[b][s] == kind) return true;
            return false;
        };
        if (!placed(kFxTexture) && texture.active()) texture.reset();
        if (!placed(kFxRoom) && room.active()) room.reset();
        if (!placed(kFxDistort) && distort.active()) distort.reset();
        if (!placed(kFxIr) && ir.active()) ir.reset();
        if (!placed(kFxVapor) && vapor.active()) vapor.reset();
        if (!placed(kFxDelay) && delayFx.active()) delayFx.reset();
        const int m = juce::jmin(cap, (int)fxDryL.size());
        // a bus on l / r, in place: its effects in order, then its MIX against the dry (dryL / dryR; copyDry: taken
        // here first). At MIX 100 % and on, nothing mixed: the effects' output as it is
        // what FX A's and FX B's effects do to the signal they pass, the ones running now (read once, before they
        // run): how late (fx/Texture.h, Vapor.h: behind their converters), and whether DISTORT runs there (its DC
        // blocker; in an oversampled mode, its round trip too: fx/Distort.h)
        int busLat[2] = {}, busDist[2] = {};
        bool distPlaced = false;
        for (int b = 0; b < 2; ++b)
            if (busRuns[b])
                for (int s = 0; s < kFxSlotsPerBus; ++s) {
                    busLat[b] += fxLatency(kinds[b][s]);
                    distPlaced = distPlaced || kinds[b][s] == kFxDistort;
                    busDist[b] |= kinds[b][s] == kFxDistort && distort.active() && distOnNow;
                }
        const bool distOver = usina::fx::Distort::oversampled(distNow.mode);
        // what the chain delays the voices by, for the host (28/10; the message thread reports it - timerCallback): in
        // series the two buses' sum, in parallel the later one (the other padded to it); the AUX is wet only
        fxChainLatency.store(juce::roundToInt(pFxRoute->load()) == 1 ? std::max(busLat[0], busLat[1]) : busLat[0] + busLat[1],
                             std::memory_order_relaxed);
        // a bus on l / r, in place: its effects in order, then its MIX against its input (the dry) made as its effects
        // leave a signal (fx/BusMatch.h: a MIX below 100 % with a plain dry combed). The dry's match runs always, so
        // what comes finds its past there. At MIX 100 % and on, nothing mixed
        const auto applyBus = [&](int b, float* l, float* r) {
            auto& g = busGain[(size_t)b];
            std::copy(l, l + m, fxDryL.data());
            std::copy(r, r + m, fxDryR.data());
            if (!busRuns[b]) {
                matchDry[(size_t)b].process(fxDryL.data(), fxDryR.data(), m, busLat[b], busDist[b] != 0, distOver, distPlaced);
                return;
            }
            runBus(kinds[b], l, r, m, false);
            // (the TEXTURE in this bus: its dry twin - the same converters as its signal, 28/10 - after it ran, so a
            // texture just switched on and its twin start together; the rest of the bus's lateness a plain delay)
            int lat = busLat[b];
            bool hasTexture = false;
            for (int s = 0; s < kFxSlotsPerBus; ++s) hasTexture = hasTexture || kinds[b][s] == kFxTexture;
            if (hasTexture && texture.dryTwin(fxDryL.data(), fxDryR.data(), m))
                lat = std::max(0, lat - texture.latencySamples());
            matchDry[(size_t)b].process(fxDryL.data(), fxDryR.data(), m, lat, busDist[b] != 0, distOver, distPlaced);
            if (!g.isSmoothing() && g.getTargetValue() == 1.0f) return;
            for (int i = 0; i < m; ++i) {
                const float w = g.getNextValue();
                l[i] = fxDryL[(size_t)i] + w * (l[i] - fxDryL[(size_t)i]);
                r[i] = fxDryR[(size_t)i] + w * (r[i] - fxDryR[(size_t)i]);
            }
        };
        const bool aux = busRuns[kBusAux], pre = pAuxPre->load() > 0.5f;
        const float send = pAuxSend->load();
        if (aux && pre)
            for (int i = 0; i < m; ++i) {
                fxAuxL[(size_t)i] = engL[(size_t)i] * send;
                fxAuxR[(size_t)i] = engR[(size_t)i] * send;
            }
        const int route = juce::roundToInt(pFxRoute->load());
        if (route != 1) {                                // A > B, B > A
            const int first = route == 2 ? (int)kBusB : (int)kBusA;
            applyBus(first, engL.data(), engR.data());
            applyBus(first == kBusA ? kBusB : kBusA, engL.data(), engR.data());
        } else if (busRuns[kBusA] || busRuns[kBusB]) {   // A // B
            for (int i = 0; i < m; ++i) {
                fxAL[(size_t)i] = fxBL[(size_t)i] = fxParL[(size_t)i] = engL[(size_t)i];
                fxAR[(size_t)i] = fxBR[(size_t)i] = fxParR[(size_t)i] = engR[(size_t)i];
            }
            applyBus(kBusA, fxAL.data(), fxAR.data());
            applyBus(kBusB, fxBL.data(), fxBR.data());
            // each bus matched with itself; then each given what the other does (the earlier held back to the later,
            // the other's all-pass), the dry both (what each adds, summed, the dry once - all alike)
            const int la = busLat[kBusA], lb = busLat[kBusB], d = std::max(la, lb);
            matchPad[0].process(fxAL.data(), fxAR.data(), m, d - la, busDist[kBusB] != 0, distOver, distPlaced);
            matchPad[1].process(fxBL.data(), fxBR.data(), m, d - lb, busDist[kBusA] != 0, distOver, distPlaced);
            matchPar.process(fxParL.data(), fxParR.data(), m, d, (busDist[kBusA] | busDist[kBusB]) != 0, distOver,
                             distPlaced);
            for (int i = 0; i < m; ++i) {
                engL[(size_t)i] = fxAL[(size_t)i] + fxBL[(size_t)i] - fxParL[(size_t)i];
                engR[(size_t)i] = fxAR[(size_t)i] + fxBR[(size_t)i] - fxParR[(size_t)i];
            }
        }
        if (aux) {
            if (!pre)
                for (int i = 0; i < m; ++i) {
                    fxAuxL[(size_t)i] = engL[(size_t)i] * send;
                    fxAuxR[(size_t)i] = engR[(size_t)i] * send;
                }
            runBus(kinds[kBusAux], fxAuxL.data(), fxAuxR.data(), m, true);
            const float ret = juce::Decibels::decibelsToGain(pAuxReturn->load());
            auto& g = busGain[(size_t)kBusAux];
            for (int i = 0; i < m; ++i) {
                const float w = g.getNextValue() * ret;
                engL[(size_t)i] += fxAuxL[(size_t)i] * w;
                engR[(size_t)i] += fxAuxR[(size_t)i] * w;
            }
        }
    }

    feedback.setTargetValue(pFeedback->load());
    level.setTargetValue(juce::Decibels::decibelsToGain(pLevel->load()));
    fbTimeSamples.setTargetValue((float)(pFbTime->load() * 0.001 * fs));

    const int lineLen = (int)fbLine[0].size();
    // the loop's insert: what goes into the loop's line passes through its effect first, a block at a time - so,
    // with an insert, the loop is at least one block long (its line written after the block)
    const bool loopFx = busRuns[kBusLoop] && n <= (int)fxLoopL.size();
    const int loopStart = fbPos;
    const float minDelay = loopFx ? (float)(n + 1) : 1.0f;
    float* L = out.getWritePointer(0);
    float* R = out.getNumChannels() > 1 ? out.getWritePointer(1) : nullptr;

    for (int i = 0; i < n; ++i) {
        // (the input's gain read above; what went into the effects not added again here)
        float gIn = i < gainLen ? inGainBuf[(size_t)i] : inGain.getNextValue() * inMix.getNextValue();
        if (i < toFxLen) gIn *= 1.0f - inFxBuf[(size_t)i];
        const float fbAmt = feedback.getNextValue();
        const float lvl = level.getNextValue();
        const float dly = juce::jlimit(minDelay, (float)(lineLen - 2), fbTimeSamples.getNextValue());

        float x[2] = { inL != nullptr ? inL[i] * gIn : 0.0f, inR != nullptr ? inR[i] * gIn : 0.0f };

        // the feedback loop: read the line 'dly' samples back (linear interpolation), through its guard
        for (int c = 0; c < 2; ++c) {
            float rp = (float)fbPos - dly;
            if (rp < 0.0f) rp += (float)lineLen;
            const int i0 = (int)rp;
            const float fr = rp - (float)i0;
            const auto& line = fbLine[(size_t)c];
            const float back = line[(size_t)i0] + fr * (line[(size_t)((i0 + 1) % lineLen)] - line[(size_t)i0]);
            x[c] += fbGuard[(size_t)c].process(back, fbAmt);
        }
        // what came from outside the synth (the input, the loop's return): a howl found there is notched there
        // (docs/SEGURANCA.md, item 4; the voices never pass here: a held note is not a howl)
        howl.process(x[0], x[1]);

        if (i < cap) {                                     // the engines' voices
            x[0] += engL[(size_t)i];
            x[1] += engR[(size_t)i];
        }

        const float yl = x[0] * lvl, yr = x[1] * lvl;
        if (loopFx) {                                    // (through the insert below, then into the line)
            fxLoopL[(size_t)i] = yl;
            fxLoopR[(size_t)i] = yr;
        } else {
            fbLine[0][(size_t)fbPos] = yl;               // the loop takes the signal before the output guard
            fbLine[1][(size_t)fbPos] = yr;
        }
        fbPos = (fbPos + 1) % lineLen;
        L[i] = yl;
        if (R != nullptr) R[i] = yr;
    }
    if (loopFx) {
        runBus(kinds[kBusLoop], fxLoopL.data(), fxLoopR.data(), n, false);
        for (int i = 0; i < n; ++i) {
            const int p = (loopStart + i) % lineLen;
            fbLine[0][(size_t)p] = fxLoopL[(size_t)i];
            fbLine[1][(size_t)p] = fxLoopR[(size_t)i];
        }
    }
    midi.clear();

    // the last stage, always (rule 22)
    guard.process(out.getArrayOfWritePointers(), out.getNumChannels(), n, pCeiling->load());
    if (guard.takeRunawayFlag()) {                       // an explosion: empty the loop, so it does not come back
        room.reset();                                    // (nor through the room's tail, nor the texture's memory)
        texture.reset();
        for (auto& line : fbLine)
            std::fill(line.begin(), line.end(), 0.0f);
        for (auto& g : fbGuard)
            g.reset();
    }
}

const char* UsinaProcessor::fxSlotTypeId(int bus, int slot)
{
    static const char* const I[kFxBuses][kFxSlotsPerBus] = { { "fx_a_type", "fx_a2_type", "fx_a3_type" },
                                                             { "fx_b_type", "fx_b2_type", "fx_b3_type" },
                                                             { "aux_type", "aux2_type", "aux3_type" },
                                                             { "loop_type", "loop2_type", "loop3_type" } };
    return bus >= 0 && bus < kFxBuses && slot >= 0 && slot < kFxSlotsPerBus ? I[bus][slot] : "";
}

bool UsinaProcessor::irLoad(const juce::File& f)
{
    if (!ir.load(f)) return false;
    irPath = f.getFullPathName();
    irMissing = false;
    return true;
}

void UsinaProcessor::irClear()
{
    ir.clear();
    irPath.clear();
    irMissing = false;
}

// --- SAMPLE ---------------------------------------------------------------------------------------------------------
juce::File UsinaProcessor::sampleFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Usina").getChildFile("Samples");
}

void UsinaProcessor::sampleTake(std::unique_ptr<usina::fm::SampleData> s)
{
    // the drawing: 256 columns, each its lowest and highest
    peaks.assign(512, 0.0f);
    sampleLength = 0.0;
    if (s != nullptr && !s->data.empty()) {
        const size_t n = s->data.size();
        sampleLength = (double)n / s->rate;
        for (size_t c = 0; c < 256; ++c) {
            const size_t a = c * n / 256, b = juce::jmax(a + 1, (c + 1) * n / 256);
            float lo = 0.0f, hi = 0.0f;
            for (size_t i = a; i < b && i < n; ++i) {
                lo = juce::jmin(lo, s->data[i]);
                hi = juce::jmax(hi, s->data[i]);
            }
            peaks[c * 2] = lo;
            peaks[c * 2 + 1] = hi;
        }
    } else {
        peaks.clear();
    }
    engine.setSample(std::move(s));
}

bool UsinaProcessor::sampleLoad(const juce::File& f)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(f));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0) return false;
    const int len = (int)juce::jmin<juce::int64>(reader->lengthInSamples, (juce::int64)(kSampleMaxSeconds * reader->sampleRate));
    const int ch = (int)juce::jmax(1u, reader->numChannels);
    juce::AudioBuffer<float> buf(ch, len);
    if (!reader->read(&buf, 0, len, 0, true, true)) return false;
    std::vector<float> data((size_t)len, 0.0f);          // (mono: the channels' average)
    for (int c = 0; c < ch; ++c) {
        const float* x = buf.getReadPointer(c);
        for (int i = 0; i < len; ++i) data[(size_t)i] += x[i] / (float)ch;
    }
    slotStore(slotNow, std::move(data), reader->sampleRate, f.getFullPathName(), false);
    return true;
}

void UsinaProcessor::sampleClear()
{
    slotStore(slotNow, {}, 0.0, {}, false);
}

void UsinaProcessor::sampleCapture(bool on)
{
    if (on) {
        if (capArmed.load() || capWait.load() || capBuf.empty()) return;
        capFull.store(false);
        capPos.store(0);
        capSlot = slotNow;                               // (the take goes where it was asked for, whatever comes after)
        capWasArmed = true;
        const int len = pCapLen != nullptr ? juce::jlimit(0, 3, (int)std::lround(pCapLen->load())) : 0;
        const int bars = len == 0 ? 0 : len == 1 ? 1 : len == 2 ? 2 : 4;
        capBars.store(bars);
        if (bars == 0) {
            capTarget.store((int)capBuf.size());
            capArmed.store(true, std::memory_order_release);
        } else {
            capWait.store(true, std::memory_order_release);   // (the audio thread starts it on the bar)
        }
    } else {
        if (capWait.exchange(false)) { capWasArmed = false; return; }   // (still waiting: nothing taken, cancelled)
        capArmed.store(false, std::memory_order_release);
    }
}

// the capture stopped (by hand or full): what it took as a recording, and as a WAV file to be kept by its path
void UsinaProcessor::captureFinish()
{
    capWasArmed = false;
    const int n = capPos.load(std::memory_order_acquire);
    if (n < (int)(0.01 * capRate)) return;               // (less than 10 ms: nothing to keep)
    auto s = std::make_unique<usina::fm::SampleData>();
    s->rate = capRate;
    s->data.assign(capBuf.begin(), capBuf.begin() + n);
    if (pCapNorm == nullptr || pCapNorm->load() > 0.5f) {   // AUTO LEVEL: the peak to -3 dBFS, at most +30 dB
        float peak = 0.0f;
        for (float x : s->data) peak = juce::jmax(peak, std::fabs(x));
        if (peak > 1e-6f) {
            const float g = juce::jmin(juce::Decibels::decibelsToGain(-3.0f) / peak, juce::Decibels::decibelsToGain(30.0f));
            for (float& x : s->data) x *= g;
        }
    }
    const auto dir = sampleFolder();
    dir.createDirectory();
    const auto file = dir.getNonexistentChildFile("CAPTURA " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"), ".wav");
    bool written = false;
    if (std::unique_ptr<juce::OutputStream> os = file.createOutputStream()) {
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions {}.withSampleRate(capRate).withNumChannels(1).withBitsPerSample(24);
        if (auto writer = wav.createWriterFor(os, options)) {   // (the writer owns the stream now)
            const float* chans[1] = { s->data.data() };
            written = writer->writeFromFloatArrays(chans, 1, n);
        }
    }
    slotStore(capSlot, std::move(s->data), capRate, written ? file.getFullPathName() : juce::String(), false);
}

// a recording into slot i (the current one: handed to the engine at once)
void UsinaProcessor::slotStore(int i, std::vector<float> data, double rate, const juce::String& path, bool missing)
{
    auto& sl = slots[(size_t)juce::jlimit(0, kSampleSlots - 1, i)];
    sl.data = std::move(data);
    sl.rate = rate;
    sl.path = path;
    sl.missing = missing;
    if (i == slotNow) sampleSelectSlot(slotNow);
}

// A..D: the engine plays slot i from now (a copy: the slot keeps its own for the next switch)
void UsinaProcessor::sampleSelectSlot(int i)
{
    slotNow = juce::jlimit(0, kSampleSlots - 1, i);
    const auto& sl = slots[(size_t)slotNow];
    if (sl.data.empty()) {
        sampleTake(nullptr);
    } else {
        auto s = std::make_unique<usina::fm::SampleData>();
        s->rate = sl.rate;
        s->data = sl.data;
        sampleTake(std::move(s));
    }
    samplePath = sl.path;
    sampleMissing = sl.missing;
}

const char* UsinaProcessor::fxBusOnId(int bus)
{
    static const char* const I[kFxBuses] = { "fx_a_on", "fx_b_on", "aux_on", "loop_fx_on" };
    return bus >= 0 && bus < kFxBuses ? I[bus] : "";
}

// where a bus's gain goes: off, 0; on, FX A's and FX B's MIX (the AUX: 1)
float UsinaProcessor::busTarget(int bus) const
{
    if (pBusOn[(size_t)bus]->load() < 0.5f) return 0.0f;
    return bus < 2 ? juce::jlimit(0.0f, 1.0f, pBusMix[(size_t)bus]->load()) : 1.0f;
}

// a bus's three slots, in order, in place (the audio thread's)
int UsinaProcessor::fxLatency(int kind) const
{
    if (kind == kFxTexture) return texture.active() ? texture.latencySamples() : 0;
    if (kind == kFxVapor) return vapor.active() ? vapor.latencySamples() : 0;
    return 0;
}

void UsinaProcessor::runBus(const int* kinds, float* l, float* r, int n, bool wetOnly)
{
    for (int s = 0; s < kFxSlotsPerBus; ++s)
        if (kinds[s] != kFxNone) runFx(kinds[s], l, r, n, wetOnly);
}

// one effect, in place, on n samples (the audio thread's): the texture with this block's knobs, the room with its own;
// wetOnly (the AUX): the effect alone, its dry out of it
void UsinaProcessor::runFx(int kind, float* l, float* r, int n, bool wetOnly)
{
    if (kind == kFxTexture) {
        auto k = texKnobs;
        if (wetOnly) k.dryWet = 1.0f;
        texture.process(l, r, n, k, texGateNow, texOnNow);
        if (wetOnly && !texture.active()) {              // (off: no texture - wet only, that is silence)
            std::fill(l, l + n, 0.0f);
            std::fill(r, r + n, 0.0f);
        }
    } else if (kind == kFxRoom) {
        const float amount = pRevOn->load() > 0.5f ? pRevAmount->load() : 0.0f;
        room.process(l, r, n, amount, juce::roundToInt(pRevColor->load()), wetOnly);
    } else if (kind == kFxDistort) {
        auto s = distNow;
        if (wetOnly) s.mix = 1.0f;
        distort.process(l, r, n, s, distOnNow);
        if (wetOnly && !distOnNow) {                     // (off: wet only, that is silence)
            std::fill(l, l + n, 0.0f);
            std::fill(r, r + n, 0.0f);
        }
    } else if (kind == kFxIr) {
        ir.process(l, r, n, irNow, irOnNow, wetOnly);
        if (wetOnly && !irOnNow) {
            std::fill(l, l + n, 0.0f);
            std::fill(r, r + n, 0.0f);
        }
    } else if (kind == kFxVapor) {
        auto k = vapKnobs;
        if (wetOnly && k.mode != usina::fx::Vapor::kResonator) k.dryWet = 1.0f;   // (the resonator has no dry: its
                                                                                   // DRY/WET knob is its distortion)
        vapor.process(l, r, n, k, vapGateNow, vapOnNow);
        if (wetOnly && !vapor.active()) {                // (off: wet only, that is silence)
            std::fill(l, l + n, 0.0f);
            std::fill(r, r + n, 0.0f);
        }
    } else if (kind == kFxDelay) {
        auto s = dlyNow;
        if (wetOnly) s.mix = 1.0f;
        delayFx.process(l, r, n, s, dlyOnNow);
        if (wetOnly && !delayFx.active()) {              // (off: wet only, that is silence)
            std::fill(l, l + n, 0.0f);
            std::fill(r, r + n, 0.0f);
        }
    }
}

void UsinaProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // bypassed, the input passes dry - and through the OutputGuard: nothing ever reaches the output without the end of
    // the chain (rule 22). The engine and the loop stop: the voices are let go (the notes held now do not come back
    // when the bypass ends) and the loop is emptied, so no feedback runs while the plugin is out of the way.
    // (The guard's look-ahead also keeps the bypassed path's latency the one declared to the host)
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    auto input = getBusBuffer(buffer, true, 0);
    auto out = getBusBuffer(buffer, false, 0);
    const int inCh = getBus(true, 0) != nullptr && getBus(true, 0)->isEnabled() ? input.getNumChannels() : 0;
    // the buffer is shared in place: output channel c already holds input channel c. An output channel past the
    // input's: a mono input's copy, or silence when there is no input
    for (int c = inCh; c < out.getNumChannels(); ++c) {
        if (inCh > 0) out.copyFrom(c, 0, input, 0, 0, out.getNumSamples());
        else out.clear(c, 0, out.getNumSamples());
    }
    for (int c = out.getNumChannels(); c < buffer.getNumChannels(); ++c)   // (the sidechain's: never an output)
        buffer.clear(c, 0, buffer.getNumSamples());
    guard.process(out.getArrayOfWritePointers(), out.getNumChannels(), out.getNumSamples(), pCeiling->load());
    if (!bypassed) {                                     // (once, as the bypass starts)
        bypassed = true;
        engine.allNotesOff();
        performanceMidi.reset();
        room.reset();
        texture.reset();
        vapor.reset();
        delayFx.reset();
        texNotesHeld = 0;
        {                                                // (the sequencer stops: its notes end)
            usina::seq::Transport stopped;
            seq.process(0, stopped, seqEvents.data(), (int)seqEvents.size());
        }
        for (auto& line : fbLine)
            std::fill(line.begin(), line.end(), 0.0f);
        for (auto& g : fbGuard)
            g.reset();
    }
}

juce::AudioProcessorEditor* UsinaProcessor::createEditor()
{
    return new UsinaEditor(*this);
}

void UsinaProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = stateTree().createXml())
        copyXmlToBinary(*xml, destData);
}

juce::ValueTree UsinaProcessor::stateTree()
{
    auto st = params.copyState();
    st.setProperty("stateVersion", kStateVersion, nullptr);          // rule 18
    const int cur = currentPreset();
    st.setProperty("preset", cur >= 0 ? lib[cur].name : juce::String(), nullptr);
    st.setProperty("userPreset", userName, nullptr);    // (a user preset's name: what the editor shows)
    // the routes: each by its source's ID and its value's name (both stable: rule 19), its amount
    juce::ValueTree mods("MODS");
    for (int i = 0; i < routesMsg.n; ++i) {
        const auto& r = routesMsg.r[(size_t)i];
        juce::ValueTree t("ROUTE");
        t.setProperty("src", usina::fm::modSourceId(r.src), nullptr);
        t.setProperty("dst", juce::String(usina::fm::routeTargetName(r.dst)), nullptr);
        t.setProperty("amount", r.amount, nullptr);
        mods.appendChild(t, nullptr);
    }
    st.removeChild(st.getChildWithName("MODS"), nullptr);
    st.appendChild(mods, nullptr);
    // FUNC 1..3's points (v5): x, y, the curve into each
    juce::ValueTree funcs("FUNCS");
    for (int k = 0; k < 3; ++k) {
        const auto& f = funcMsg[(size_t)k];
        juce::ValueTree t("FUNC");
        juce::String pts;
        for (int i = 0; i < f.n; ++i)
            pts << juce::String(f.p[(size_t)i].x, 5) << ' ' << juce::String(f.p[(size_t)i].y, 5) << ' '
                << juce::String(f.p[(size_t)i].curve, 4) << (i + 1 < f.n ? ";" : "");
        t.setProperty("points", pts, nullptr);
        funcs.appendChild(t, nullptr);
    }
    st.removeChild(st.getChildWithName("FUNCS"), nullptr);
    st.appendChild(funcs, nullptr);
    st.removeChild(st.getChildWithName("SEQ"), nullptr);   // the sequencer's pattern (v6)
    st.appendChild(seqToTree(seqPatternMsg), nullptr);
    // the mappings (CTRLS v6, CCMAP and CABACT 22/10) as children of the state, as before; the CABINE's own (22/10)
    const auto maps = mappingsTree();
    for (const char* name : { "CTRLS", "CCMAP", "CABACT", "LAUNCHPAD", "CABINE", "PLAY" }) st.removeChild(st.getChildWithName(name), nullptr);
    for (int i = 0; i < maps.getNumChildren(); ++i) st.appendChild(maps.getChild(i).createCopy(), nullptr);
    if (cabineModel != nullptr) st.appendChild(cabineModel->toTree(), nullptr);
    st.appendChild(playTree.createCopy(), nullptr);    // the PLAY view's (27/10): snapshots, XY, layout
    st.setProperty("irFile", irPath, nullptr);         // the IR's file, by its path (v6; the file itself is the user's)
    st.setProperty("sampleFile", samplePath, nullptr); // SAMPLE's recording, by its path (27/10; a capture is a file too)
    juce::StringArray files;                           // the four (28/10): A..D by their paths, and which one plays
    for (const auto& sl : slots) files.add(sl.path);
    st.setProperty("sampleFiles", files.joinIntoString("\n"), nullptr);
    st.setProperty("sampleSlot", slotNow, nullptr);
    return st;
}

// --- the mappings ---------------------------------------------------------------------------------------------------
juce::ValueTree UsinaProcessor::mappingsTree() const
{
    juce::ValueTree maps("MAPPINGS");
    juce::ValueTree ctrls("CTRLS");                     // the controllers' mapping (v6)
    for (int i = 0; i < kCtrlSlots; ++i) {
        juce::ValueTree c("CTRL");
        c.setProperty("type", ctrlType[(size_t)i].load(), nullptr);
        c.setProperty("num", ctrlNum[(size_t)i].load(), nullptr);
        c.setProperty("ch", ctrlChan[(size_t)i].load(), nullptr);
        ctrls.appendChild(c, nullptr);
    }
    maps.appendChild(ctrls, nullptr);
    juce::ValueTree ccmap("CCMAP");                     // MIDI learn on parameters (22/10): each a CC, its channel, the
    for (const auto& m : ccMaps)                        // parameter by its ID
        if (auto* p = m.param.load(); p != nullptr && m.cc.load() >= 0) {
            juce::ValueTree c("MAP");
            c.setProperty("id", p->paramID, nullptr);
            c.setProperty("cc", m.cc.load(), nullptr);
            c.setProperty("ch", m.channel.load(), nullptr);
            ccmap.appendChild(c, nullptr);
        }
    maps.appendChild(ccmap, nullptr);
    juce::ValueTree cabact("CABACT");                   // the CABINE's actions' triggers (22/10), by the action's index
    for (int a = 0; a < kCabActions; ++a) {
        juce::ValueTree c("ACT");
        c.setProperty("type", actType[(size_t)a].load(), nullptr);
        c.setProperty("num", actNum[(size_t)a].load(), nullptr);
        c.setProperty("ch", actChan[(size_t)a].load(), nullptr);
        cabact.appendChild(c, nullptr);
    }
    maps.appendChild(cabact, nullptr);
    if (cabineModel != nullptr) maps.appendChild(cabineModel->portsTree(), nullptr);   // (the Launchpad's ports)
    return maps;
}

void UsinaProcessor::applyMappings(const juce::ValueTree& maps)
{
    const auto ctrls = maps.getChildWithName("CTRLS");
    for (int i = 0; i < kCtrlSlots; ++i) {
        const auto c = ctrls.getChild(i);
        ctrlType[(size_t)i].store(juce::jlimit(0, 1, (int)c.getProperty("type", 0)));
        ctrlNum[(size_t)i].store(juce::jlimit(-1, 127, (int)c.getProperty("num", -1)));
        ctrlChan[(size_t)i].store(juce::jlimit(0, 16, (int)c.getProperty("ch", 0)));
    }
    const auto ccmap = maps.getChildWithName("CCMAP");
    for (int i = 0; i < kCcMaps; ++i) {
        auto& m = ccMaps[(size_t)i];
        const auto c = ccmap.getChild(i);
        auto* p = c.isValid() ? params.getParameter(c.getProperty("id").toString()) : nullptr;   // (an unknown ID: left out)
        m.param.store(p);
        m.cc.store(p != nullptr ? juce::jlimit(0, 127, (int)c.getProperty("cc", 0)) : -1);
        m.channel.store(juce::jlimit(0, 16, (int)c.getProperty("ch", 0)));
        m.value.store(-1.0f);
    }
    const auto cabact = maps.getChildWithName("CABACT");
    for (int a = 0; a < kCabActions; ++a) {
        const auto c = cabact.getChild(a);
        setActionBinding(a, { c.isValid() ? (int)c.getProperty("type", -1) : -1, (int)c.getProperty("num", 0),
                              (int)c.getProperty("ch", 0) });
    }
    const auto lp = maps.getChildWithName("LAUNCHPAD");
    if (lp.isValid() && cabineModel != nullptr && keepsGlobalMappings()) cabineModel->connectLater(lp);
}

bool UsinaProcessor::mappingsEmpty(const juce::ValueTree& maps)
{
    const auto ctrls = maps.getChildWithName("CTRLS");
    for (int i = 0; i < ctrls.getNumChildren(); ++i)
        if ((int)ctrls.getChild(i).getProperty("num", -1) >= 0) return false;
    if (maps.getChildWithName("CCMAP").getNumChildren() > 0) return false;
    const auto cabact = maps.getChildWithName("CABACT");
    for (int i = 0; i < cabact.getNumChildren(); ++i)
        if ((int)cabact.getChild(i).getProperty("type", -1) >= 0) return false;
    return maps.getChildWithName("LAUNCHPAD").getProperty("in").toString().isEmpty();
}

juce::File UsinaProcessor::globalMappingsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Ruido").getChildFile("Usina").getChildFile("mappings.xml");
}

bool UsinaProcessor::exportMappings(const juce::File& file)
{
    auto xml = mappingsTree().createXml();
    return xml != nullptr && file.getParentDirectory().createDirectory() && xml->writeTo(file);
}

bool UsinaProcessor::importMappings(const juce::File& file)
{
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr) return false;
    const auto maps = juce::ValueTree::fromXml(*xml);
    if (!maps.hasType("MAPPINGS")) return false;
    applyMappings(maps);                                 // (a hand's change: the timer keeps it for new instances too)
    return true;
}

// --- the user's presets ---------------------------------------------------------------------------------------------
namespace {
// the rig's parameters (a user preset neither saves nor loads them: they stay as the session has them)
const char* const kRigIds[] = { "in_gain", "in_mix", "in_fx", "feedback", "fb_time", "level", "ceiling", "oversampling",
                                "seq_bpm", "seq_play", "bend_range", "mpe_mode", "mpe_bend_range", "cap_len", "cap_norm" };
// (PLAY, 28/10: the performance surface - snapshots, XY, the keys' layout - is the player's, not the sound's: a preset
// neither carries it nor replaces it; found by the web's import test, which refused presets saved since 27/10)
const char* const kRigChildren[] = { "CTRLS", "CCMAP", "CABACT", "LAUNCHPAD", "CABINE", "PLAY" };
bool isRigParam(const juce::ValueTree& c)
{
    for (const char* id : kRigIds)
        if (c.getProperty("id").toString() == id) return true;
    return false;
}
} // namespace

juce::File UsinaProcessor::userPresetFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Usina").getChildFile("Presets");
}

const juce::String UsinaProcessor::getProgramName(int index)
{
    return index >= 0 && index < lib.size() ? lib[index].name : juce::String("INIT");
}

bool UsinaProcessor::saveUserPreset(const juce::File& file)
{
    auto st = stateTree();
    for (int i = st.getNumChildren(); --i >= 0;)
        if (isRigParam(st.getChild(i))) st.removeChild(i, nullptr);
    for (const char* c : kRigChildren) st.removeChild(st.getChildWithName(c), nullptr);
    const auto name = file.getFileNameWithoutExtension();
    st.setProperty("usinaPreset", 1, nullptr);          // (what says the file is a preset, not a session)
    st.setProperty("userPreset", name, nullptr);
    st.removeProperty("preset", nullptr);
    auto xml = st.createXml();
    if (xml == nullptr || !file.getParentDirectory().createDirectory() || !xml->writeTo(file)) return false;
    userName = name;
    readPatch(userValues);
    userRoutes = routesMsg.n;
    current.store(-1, std::memory_order_relaxed);       // (the sound is the file's now, not the library's)
    return true;
}

bool UsinaProcessor::loadUserPreset(const juce::File& file)
{
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr) return false;
    auto st = juce::ValueTree::fromXml(*xml);
    if (!st.hasType(params.state.getType()) || !st.hasProperty("usinaPreset")) return false;
    // the rig's as they are now: the session's parameters and maps replace whatever the file has of them
    const auto now = stateTree();
    for (int i = st.getNumChildren(); --i >= 0;)
        if (isRigParam(st.getChild(i))) st.removeChild(i, nullptr);
    for (int i = 0; i < now.getNumChildren(); ++i)
        if (isRigParam(now.getChild(i))) st.appendChild(now.getChild(i).createCopy(), nullptr);
    for (const char* c : kRigChildren) {
        st.removeChild(st.getChildWithName(c), nullptr);
        st.appendChild(now.getChildWithName(c).createCopy(), nullptr);
    }
    st.setProperty("userPreset", file.getFileNameWithoutExtension(), nullptr);
    st.removeProperty("preset", nullptr);
    st.removeProperty("usinaPreset", nullptr);
    applyStateTree(st);
    return true;
}

// --- the sequencer's editing (the message thread's) -----------------------------------------------------------------
void UsinaProcessor::seqEditLane(int lane, const std::function<void(ruidocore::Part&)>& edit)
{
    if (lane < 0 || lane >= usina::seq::kLanes) return;
    auto& part = seqPatternMsg.parts[(size_t)lane];
    edit(part);
    ruidocore::ensureStepStorage(part);
    seqUnsent[(size_t)lane] = !seq.submitLane(lane, part);   // (the mailbox full: seqFlush sends it on the next tick)
}

void UsinaProcessor::seqSetGrid(int stepsPerBeat, float swingPercent)
{
    seqPatternMsg.resolutionStepsPerBeat = juce::jlimit(1, 16, stepsPerBeat);
    seqPatternMsg.swingPercent = juce::jlimit(0.0f, 100.0f, swingPercent);
    seq.setGlobals(seqPatternMsg.resolutionStepsPerBeat, seqPatternMsg.swingPercent);
}

UsinaProcessor::CtrlSlot UsinaProcessor::ctrlSlot(int i) const
{
    if (i < 0 || i >= kCtrlSlots) return {};
    return { ctrlType[(size_t)i].load(), ctrlNum[(size_t)i].load(), ctrlChan[(size_t)i].load() };
}

void UsinaProcessor::setCtrlSlot(int i, CtrlSlot s)
{
    if (i < 0 || i >= kCtrlSlots) return;
    ctrlType[(size_t)i].store(juce::jlimit(0, 1, s.type));
    ctrlNum[(size_t)i].store(juce::jlimit(-1, 127, s.number));
    ctrlChan[(size_t)i].store(juce::jlimit(0, 16, s.channel));
}

void UsinaProcessor::seqFlush()
{
    seq.collect();
    for (int lane = 0; lane < usina::seq::kLanes; ++lane)
        if (seqUnsent[(size_t)lane]) seqUnsent[(size_t)lane] = !seq.submitLane(lane, seqPatternMsg.parts[(size_t)lane]);
}

void UsinaProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        applyStateTree(juce::ValueTree::fromXml(*xml));
}

void UsinaProcessor::applyStateTree(juce::ValueTree st)
{
    {
        if (st.hasType(params.state.getType())) {
            // v1 had no preset: it keeps the current one; v2 names it (its values: the preset's); v3 holds the
            // patch's values themselves (the preset's name only says where they came from)
            const int ver = (int)st.getProperty("stateVersion", 1);
            const juce::String name = ver >= 2 ? st.getProperty("preset").toString() : juce::String();
            st.removeProperty("preset", nullptr);
            const juce::String user = st.getProperty("userPreset").toString();   // (22/10; before: none)
            st.removeProperty("userPreset", nullptr);
            // the routes (v4; before: none). An unknown source or value is left out, never guessed
            usina::fm::Routes rt;
            const auto mods = st.getChildWithName("MODS");
            for (int i = 0; i < mods.getNumChildren() && rt.n < usina::fm::kMaxRoutes; ++i) {
                const auto t = mods.getChild(i);
                int src = -1;
                for (int s = 1; s < usina::fm::kNumSources; ++s)
                    if (t.getProperty("src").toString() == usina::fm::modSourceId(s)) src = s;
                const int dst = usina::fm::routeTargetIndex(t.getProperty("dst").toString().toStdString());
                if (src > 0 && usina::fm::isRouteTarget(dst))
                    rt.r[(size_t)rt.n++] = { src, dst, juce::jlimit(-1.0f, 1.0f, (float)t.getProperty("amount", 0.0f)) };
            }
            st.removeChild(mods, nullptr);
            routesMsg = rt;
            // FUNC 1..3's points (v5; before: the defaults). A malformed list keeps the default curve
            const auto funcs = st.getChildWithName("FUNCS");
            for (int k = 0; k < 3; ++k) {
                auto f = usina::fm::defaultFuncShape(k);
                const auto pts = juce::StringArray::fromTokens(funcs.getChild(k).getProperty("points").toString(), ";", "");
                if (pts.size() >= 2 && pts.size() <= usina::fm::kMaxFuncPoints) {
                    usina::fm::FuncShape g;
                    g.n = 0;
                    for (const auto& p : pts) {
                        const auto v = juce::StringArray::fromTokens(p, " ", "");
                        if (v.size() != 3) { g.n = 0; break; }
                        g.p[(size_t)g.n++] = { v[0].getFloatValue(), v[1].getFloatValue(), v[2].getFloatValue() };
                    }
                    if (g.n >= 2) f = g;
                }
                funcMsg[(size_t)k] = usina::fm::sanitizeFuncShape(f);
            }
            st.removeChild(funcs, nullptr);
            publishRoutes();
            // the sequencer's pattern (v6; before: the empty one). Taken whole, with processing suspended (the
            // player's whole-pattern change is not real-time; a lane edit is)
            seqPatternMsg = seqFromTree(st.getChildWithName("SEQ"));
            st.removeChild(st.getChildWithName("SEQ"), nullptr);
            seqUnsent.fill(false);
            suspendProcessing(true);
            seq.setPatternNow(seqPatternMsg);
            suspendProcessing(false);
            // the mappings (CTRLS v6, CCMAP and CABACT 22/10; before: none). A session with none keeps the ones there
            // are (the rig's: the user's file, read when the instance started); a session's own are not a hand's change
            {
                juce::ValueTree maps("MAPPINGS");
                for (const char* name : { "CTRLS", "CCMAP", "CABACT", "LAUNCHPAD" }) {
                    const auto c = st.getChildWithName(name);
                    if (c.isValid()) maps.appendChild(c.createCopy(), nullptr);
                    st.removeChild(c, nullptr);
                }
                if (!mappingsEmpty(maps)) applyMappings(maps);
                mapSignature = mappingsTree().toXmlString();
            }
            // the CABINE's own (22/10; before: none): its layer, mode, scale, the drums' mutes; the Launchpad again
            const auto cab = st.getChildWithName("CABINE");
            if (cab.isValid() && cabineModel != nullptr) cabineModel->fromTree(cab);
            st.removeChild(cab, nullptr);
            // the PLAY view's (27/10; before: none, its defaults). Copied into the same tree: the view holds it
            const auto play = st.getChildWithName("PLAY");
            if (play.isValid()) playTree.copyPropertiesAndChildrenFrom(play, nullptr);
            st.removeChild(play, nullptr);
            const juce::String irFile = st.getProperty("irFile").toString();   // (the IR's file; before: none)
            st.removeProperty("irFile", nullptr);
            const juce::String sampleFile = st.getProperty("sampleFile").toString();   // (SAMPLE's; before: none)
            // the four (28/10; before: the one, as A)
            juce::StringArray files;
            files.addLines(st.getProperty("sampleFiles", sampleFile).toString());
            const int slotWanted = juce::jlimit(0, kSampleSlots - 1, (int)st.getProperty("sampleSlot", 0));
            for (const char* p : { "sampleFile", "sampleFiles", "sampleSlot" }) st.removeProperty(p, nullptr);
            for (int i = 0; i < kSampleSlots; ++i) {
                const juce::String f = i < files.size() ? files[i] : juce::String();
                if (f == slots[(size_t)i].path && !slots[(size_t)i].missing) continue;   // (the same file: kept)
                slotNow = i;                             // (loaded into its own slot; the one that plays chosen after)
                if (f.isEmpty()) {
                    slotStore(i, {}, 0.0, {}, false);
                } else {                                 // (a file that moved or went: kept named, marked missing)
                    const bool ok = juce::File::isAbsolutePath(f) && sampleLoad(juce::File(f));
                    if (!ok) slotStore(i, {}, 0.0, f, true);
                }
            }
            sampleSelectSlot(slotWanted);
            params.replaceState(st);
            if (irFile.isEmpty()) {
                irClear();
            } else {                                     // (a file that moved or went: kept named, marked missing)
                const bool ok = juce::File::isAbsolutePath(irFile) && irLoad(juce::File(irFile));
                irPath = irFile;
                irMissing = !ok;
            }
            // the feedback opens disarmed: a session saved with it on loads with it at 0 (docs/FEEDBACK.md, option 5;
            // the user, 20/10: off by default until the safety study is done). The loop is armed by a hand, never
            // by a file
            if (auto* fb = params.getParameter("feedback"))
                fb->setValueNotifyingHost(fb->convertTo0to1(0.0f));
            const int idx = name.isNotEmpty() ? lib.indexOf(name) : -1;
            if (ver < 3 && idx >= 0)
                selectPreset(idx);
            else if (ver >= 3)
                current.store(idx, std::memory_order_relaxed);
            userName = user;                             // (after selectPreset, which forgets it)
            readPatch(userValues);
            userRoutes = routesMsg.n;
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UsinaProcessor();
}
