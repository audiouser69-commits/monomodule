// Monomodule Euclid: the generative sequencer's parameters (gen/Generative.h), added to Six's parameter layout.
// Every sequencer setting is a host parameter, so a DAW can automate the density, the chances or the seed while the
// texture plays. IDs: q<track><name>, lanes q<track>l<lane><name>, globals q<name>. The per-step mutes are not
// parameters (32 bits per track, kept in the plugin state).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "gen/Generative.h"
#include "host/Machines.h"
#include "SpecData.h"

namespace mnm::plugin::one::euclid {

using namespace mnm::gen;

inline juce::String id(int t, const juce::String& name) { return "q" + juce::String(t + 1) + name; }
inline juce::String laneId(int t, int l, const juce::String& name) { return id(t, "l" + juce::String(l + 1) + name); }
inline juce::String gid(const juce::String& name) { return "q" + name; }

inline juce::String noteName(int n) { return juce::String(kNoteNames[((n % 12) + 12) % 12]) + juce::String(n / 12 - 1); }

// What a lane can lock: page * 8 + knob. SYN knobs are named by the track's machine in the editor.
inline juce::String lockTargetName(int target)
{
    if (target < 0) return "OFF";
    const int page = target / 8, k = target % 8;
    if (page == 0) return "SYN " + juce::String::charToString(juce::juce_wchar('A' + k));
    return juce::String(spec::kSharedPages[page - 1].name) + " " + spec::kSharedPages[page - 1].labels[k];
}

// The kit the plugin starts with: a texture that plays as soon as the transport runs.
struct LaneDefault { int target, min, max; LaneMode mode; int length, chance; };
struct TrackDefault {
    host::Machine machine;
    bool on; int steps, pulses, rotate, rate, accents, prob; Cond cond;
    int rchance, rmax; Shape rshape; int rpitch, gate, swing, drift;
    PitchMode mode; int scale /* 0 = global */, low, high, length, mutate, hold;
    std::array<LaneDefault, kLanes> lanes;
};
constexpr LaneDefault kNoLane{-1, 0, 127, LaneMode::Random, 8, 100};
inline const std::array<TrackDefault, kTracks>& defaults()
{
    static const std::array<TrackDefault, kTracks> d = {{
        // T1 kick: DPRO BBOX key 48, a 5-in-16 with rolls and the odd retrig smear
        {host::Machine::BBOX, true, 16, 5, 0, 4, 2, 100, Cond::Always, 12, 3, Shape::Decel, 0, 60, 0, 0,
         PitchMode::Fixed, 1, 48, 48, 8, 0, 0,
         {{{4, 0, 90, LaneMode::Random, 8, 25}, {0, 56, 72, LaneMode::Accent, 8, 100}, kNoLane, kNoLane}}},
        // T2 kit hits: BBOX keys 49-58 at random, 6 in 13 against T1, chancy, ratcheting, drifting
        {host::Machine::BBOX, true, 13, 6, 3, 4, 0, 80, Cond::Always, 35, 6, Shape::Random, 0, 40, 0, 20,
         PitchMode::Random, 1, 49, 58, 8, 0, 20,
         {{{1, 0, 70, LaneMode::Random, 8, 50}, {0, 44, 84, LaneMode::Turing, 8, 40}, kNoLane, kNoLane}}},
        // T3 metallic FM in quintuplets: a Turing melody that slowly mutates
        {host::Machine::FM_DYN, true, 11, 4, 0, 3, 3, 90, Cond::Always, 15, 4, Shape::Accel, 1, 30, 0, 0,
         PitchMode::Turing, 0, 55, 79, 7, 12, 0,
         {{{1, 40, 110, LaneMode::Random, 8, 100}, {6, 20, 90, LaneMode::Turing, 8, 70}, {21, 30, 100, LaneMode::Sine, 22, 100}, kNoLane}}},
        // T4 SID bass: a brownian walk in the low register, filter swept by a slow sine
        {host::Machine::SID, true, 16, 7, 2, 4, 2, 100, Cond::Always, 0, 1, Shape::Flat, 0, 75, 15, 0,
         PitchMode::Walk, 0, 31, 50, 2, 0, 30,
         {{{16, 30, 100, LaneMode::Sine, 48, 100}, {3, 0, 80, LaneMode::Accent, 8, 100}, kNoLane, kNoLane}}},
        // T5 SWAVE chords in eighths: a 5-note cycle phasing against a 32-step rhythm, delay throws
        {host::Machine::SAW, true, 32, 5, 1, 8, 0, 85, Cond::Always, 0, 1, Shape::Flat, 0, 90, 0, 10,
         PitchMode::Cycle, 0, 55, 79, 5, 8, 0,
         {{{28, 0, 90, LaneMode::Random, 8, 30}, {16, 40, 110, LaneMode::Triangle, 7, 100}, kNoLane, kNoLane}}},
        // T6 the FX REVERB on T5 (NEIBOR); its sequencer is off (FX machines need no notes)
        {host::Machine::REVERB, false, 16, 4, 0, 4, 0, 100, Cond::Always, 0, 1, Shape::Flat, 0, 50, 0, 0,
         PitchMode::Fixed, 0, 60, 60, 8, 0, 0,
         {{kNoLane, kNoLane, kNoLane, kNoLane}}},
    }};
    return d;
}

inline std::unique_ptr<juce::AudioProcessorParameterGroup> trackGroup(int t)
{
    using Int = juce::AudioParameterInt;
    using Choice = juce::AudioParameterChoice;
    using Bool = juce::AudioParameterBool;
    const auto& d = defaults()[size_t(t)];
    const juce::String pre = "SEQ" + juce::String(t + 1) + " ";
    auto pct = juce::AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return juce::String(v) + "%"; });
    auto note = juce::AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return noteName(v); });
    auto list = [](const char* const* names, int n) { juce::StringArray a; for (int i = 0; i < n; ++i) a.add(names[i]); return a; };
    juce::StringArray scales{"GLOBAL"};
    for (const auto& s : kScales) scales.add(s.name);
    juce::StringArray targets{"OFF"};
    for (int i = 0; i < kLockTargets; ++i) targets.add(lockTargetName(i));

    auto g = std::make_unique<juce::AudioProcessorParameterGroup>(id(t, "grp"), "SEQ T" + juce::String(t + 1), " ");
    auto add = [&](std::unique_ptr<juce::RangedAudioParameter> p) { g->addChild(std::move(p)); };
    add(std::make_unique<Bool>(juce::ParameterID{id(t, "on"), 1}, pre + "ON", d.on));
    add(std::make_unique<Choice>(juce::ParameterID{id(t, "mode"), 1}, pre + "MODE", list(kTrackModeNames, int(TrackMode::Count)), 0));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "steps"), 1}, pre + "STEPS", 1, kMaxSteps, d.steps));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "pulses"), 1}, pre + "PULSES", 0, kMaxSteps, d.pulses));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "rotate"), 1}, pre + "ROTATE", 0, kMaxSteps - 1, d.rotate));
    add(std::make_unique<Choice>(juce::ParameterID{id(t, "rate"), 1}, pre + "RATE", list(kRateNames, kNumRates), d.rate));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "accents"), 1}, pre + "ACCENTS", 0, kMaxSteps, d.accents));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "prob"), 1}, pre + "CHANCE", 0, 100, d.prob, pct));
    add(std::make_unique<Choice>(juce::ParameterID{id(t, "cond"), 1}, pre + "CONDITION", list(kCondNames, int(Cond::Count)), int(d.cond)));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "rchance"), 1}, pre + "RATCHET CHANCE", 0, 100, d.rchance, pct));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "rmax"), 1}, pre + "RATCHET MAX", 1, 8, d.rmax,
        juce::AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return v <= 1 ? juce::String("OFF") : "x" + juce::String(v); })));
    add(std::make_unique<Choice>(juce::ParameterID{id(t, "rshape"), 1}, pre + "RATCHET SHAPE", list(kShapeNames, int(Shape::Count)), int(d.rshape)));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "rpitch"), 1}, pre + "RATCHET PITCH", -4, 4, d.rpitch,
        juce::AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return v > 0 ? "+" + juce::String(v) : juce::String(v); })));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "gate"), 1}, pre + "GATE", 1, 100, d.gate, pct));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "swing"), 1}, pre + "SWING", 0, 100, d.swing, pct));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "drift"), 1}, pre + "DRIFT", 0, 100, d.drift, pct));
    add(std::make_unique<Choice>(juce::ParameterID{id(t, "pmode"), 1}, pre + "PITCH MODE", list(kPitchModeNames, int(PitchMode::Count)), int(d.mode)));
    add(std::make_unique<Choice>(juce::ParameterID{id(t, "scale"), 1}, pre + "SCALE", scales, d.scale));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "low"), 1}, pre + "LOW NOTE", 0, 127, d.low, note));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "high"), 1}, pre + "HIGH NOTE", 0, 127, d.high, note));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "len"), 1}, pre + "LENGTH", 1, 32, d.length));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "mutate"), 1}, pre + "MUTATE", 0, 100, d.mutate, pct));
    add(std::make_unique<Int>(juce::ParameterID{id(t, "hold"), 1}, pre + "HOLD", 0, 100, d.hold, pct));
    for (int l = 0; l < kLanes; ++l) {
        const auto& ld = d.lanes[size_t(l)];
        const juce::String lp = pre + "LANE" + juce::String(l + 1) + " ";
        add(std::make_unique<Choice>(juce::ParameterID{laneId(t, l, "tgt"), 1}, lp + "TARGET", targets, ld.target + 1));
        add(std::make_unique<Int>(juce::ParameterID{laneId(t, l, "min"), 1}, lp + "MIN", 0, 127, ld.min));
        add(std::make_unique<Int>(juce::ParameterID{laneId(t, l, "max"), 1}, lp + "MAX", 0, 127, ld.max));
        add(std::make_unique<Choice>(juce::ParameterID{laneId(t, l, "mode"), 1}, lp + "MODE", list(kLaneModeNames, int(LaneMode::Count)), int(ld.mode)));
        add(std::make_unique<Int>(juce::ParameterID{laneId(t, l, "len"), 1}, lp + "LENGTH", 1, 64, ld.length));
        add(std::make_unique<Int>(juce::ParameterID{laneId(t, l, "chance"), 1}, lp + "CHANCE", 0, 100, ld.chance, pct));
    }
    return g;
}

inline void addLayout(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    auto pct = juce::AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return juce::String(v) + "%"; });
    auto list = [](const char* const* names, int n) { juce::StringArray a; for (int i = 0; i < n; ++i) a.add(names[i]); return a; };
    juce::StringArray scales;
    for (const auto& s : kScales) scales.add(s.name);
    auto g = std::make_unique<juce::AudioProcessorParameterGroup>("qglobal", "SEQ", " ");
    g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{gid("seed"), 1}, "SEQ SEED", 1, 9999, 1977));
    g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{gid("loop"), 1}, "SEQ LOOP", list(kLoopNames, kNumLoops), 3));
    g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{gid("evolve"), 1}, "SEQ EVOLVE", 0, 100, 8, pct));
    g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{gid("root"), 1}, "SEQ ROOT", list(kNoteNames, 12), 2));
    g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{gid("scale"), 1}, "SEQ SCALE", scales, 4));
    g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{gid("run"), 1}, "SEQ RUN WITHOUT HOST", false));
    layout.add(std::move(g));
    for (int t = 0; t < kTracks; ++t) layout.add(trackGroup(t));
}

// The audio thread's view of the parameters.
class Bindings {
public:
    void bind(juce::AudioProcessorValueTreeState& s)
    {
        auto raw = [&](const juce::String& i) { auto* p = s.getRawParameterValue(i); jassert(p != nullptr); return p; };
        m_seed = raw(gid("seed")); m_loop = raw(gid("loop")); m_evolve = raw(gid("evolve"));
        m_root = raw(gid("root")); m_scale = raw(gid("scale")); m_run = raw(gid("run"));
        for (int t = 0; t < kTracks; ++t) {
            auto& b = m_tracks[size_t(t)];
            const char* names[] = {"on", "steps", "pulses", "rotate", "rate", "accents", "prob", "cond", "rchance", "rmax", "rshape",
                                   "rpitch", "gate", "swing", "drift", "pmode", "scale", "low", "high", "len", "mutate", "hold", "mode"};
            for (size_t i = 0; i < b.p.size(); ++i) b.p[i] = raw(id(t, names[i]));
            for (int l = 0; l < kLanes; ++l) {
                const char* ln[] = {"tgt", "min", "max", "mode", "len", "chance"};
                for (size_t i = 0; i < 6; ++i) b.lane[size_t(l)][i] = raw(laneId(t, l, ln[i]));
            }
        }
    }
    bool runWithoutHost() const { return m_run->load() >= 0.5f; }

    // Fills gp from the parameters; masks = the per-step mutes.
    using GridStore = std::array<std::array<std::atomic<uint32_t>, kMaxSteps>, kTracks>;
    void read(GlobalParams& gp, const std::array<std::atomic<uint32_t>, kTracks>& masks, const GridStore& grid) const
    {
        auto I = [](const std::atomic<float>* p) { return int(std::lround(p->load())); };
        gp.seed = I(m_seed); gp.loop = I(m_loop); gp.evolve = I(m_evolve); gp.root = I(m_root); gp.scale = I(m_scale);
        for (int t = 0; t < kTracks; ++t) {
            const auto& b = m_tracks[size_t(t)].p;
            auto& tp = gp.tracks[size_t(t)];
            tp.on = b[0]->load() >= 0.5f;
            tp.mode = TrackMode(juce::jlimit(0, int(TrackMode::Count) - 1, I(b[22])));
            for (int i = 0; i < kMaxSteps; ++i) tp.grid[size_t(i)] = grid[size_t(t)][size_t(i)].load(std::memory_order_relaxed);
            tp.steps = I(b[1]); tp.pulses = I(b[2]); tp.rotate = I(b[3]); tp.rate = I(b[4]); tp.accents = I(b[5]); tp.prob = I(b[6]);
            tp.cond = Cond(juce::jlimit(0, int(Cond::Count) - 1, I(b[7])));
            tp.ratchetChance = I(b[8]); tp.ratchetMax = I(b[9]);
            tp.ratchetShape = Shape(juce::jlimit(0, int(Shape::Count) - 1, I(b[10])));
            tp.ratchetPitch = I(b[11]); tp.gate = I(b[12]); tp.swing = I(b[13]); tp.drift = I(b[14]);
            tp.pitchMode = PitchMode(juce::jlimit(0, int(PitchMode::Count) - 1, I(b[15])));
            tp.scale = I(b[16]) - 1;   // -1 = the global scale
            tp.low = I(b[17]); tp.high = I(b[18]); tp.length = I(b[19]); tp.mutate = I(b[20]); tp.hold = I(b[21]);
            tp.stepMask = masks[size_t(t)].load();
            for (int l = 0; l < kLanes; ++l) {
                const auto& lb = m_tracks[size_t(t)].lane[size_t(l)];
                auto& lp = tp.lanes[size_t(l)];
                lp.target = I(lb[0]) - 1; lp.min = I(lb[1]); lp.max = I(lb[2]);
                lp.mode = LaneMode(juce::jlimit(0, int(LaneMode::Count) - 1, I(lb[3])));
                lp.length = I(lb[4]); lp.chance = I(lb[5]);
            }
        }
    }

private:
    struct TrackBind { std::array<std::atomic<float>*, 23> p{}; std::array<std::array<std::atomic<float>*, 6>, kLanes> lane{}; };
    std::atomic<float>* m_seed = nullptr; std::atomic<float>* m_loop = nullptr; std::atomic<float>* m_evolve = nullptr;
    std::atomic<float>* m_root = nullptr; std::atomic<float>* m_scale = nullptr; std::atomic<float>* m_run = nullptr;
    std::array<TrackBind, kTracks> m_tracks{};
};

} // namespace mnm::plugin::one::euclid
