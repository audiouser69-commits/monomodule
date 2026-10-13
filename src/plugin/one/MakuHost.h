// Monomodule Euclid + MAKU: the ambient generator MAKU (github.com/okumurari2/maku, GPL-3.0-only, a Felucca firmware
// for the M-VAVE FM-1 by okumurari, after Leo Kuroshita's Felucca) runs inside the plugin beside the six Monomachine
// tracks. Its firmware is C written for one device, every bit of state in file-scope variables, so it is built as a
// shared library of its own (ext/maku, plugin/maku_plugin.c), embedded in the plugin binary, and each plugin instance
// loads a private copy of that file: as many MAKUs as instances, none sharing state.
//
//   MakuLib     the loaded library: the C calls, made from the audio thread only (or with the engine lock held)
//   MakuUi      what the editor shows, copied out of the audio thread a few times a second
//   MakuCommand what the editor asks for (a knob, a verb key, NEW WORLD ...), queued to the audio thread
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
#include "gen/Generative.h"

namespace mnm::plugin::one::maku {

constexpr int kParts = 4;                   // PULSE (kick), FLOOR (drone), HAZE (shimmer), VOICE (arp)
constexpr int kScreen = 240;                // MAKU's screen, 240 x 240 RGB565
constexpr int kKeys = 27;                   // MAKU's keyboard: F3 .. G5
constexpr int kFirstKeyNote = 53;           // F3
constexpr int kWorldBytes = 32;

// MAKU's buttons by their printed label (firmware panel.c B_FX .. B_OCTUP): the bits of mk buttons masks
enum Button { BFx = 0, BScl, BEnv, BLfo, BEdit, BGlo, BHome, BSave, BArp, BSeq, BPlay, BRec, BOctDn, BOctUp, NumButtons };

// The verbs on the panel: the six upper buttons held (each fades back on its own after letting go), DRIFT (PLAY, a
// tap switches it), NEW WORLD and LIKE (REC: a tap keeps the world now, held ~2 s forgets every like)
struct Verb { const char* name; const char* key; int button; const char* help; };
constexpr int kNumVerbs = 9;
extern const Verb kVerbs[kNumVerbs];

// MAKU's scales, params.c N_SCALE order, as bit masks of the twelve semitones from the root
constexpr int kNumMakuScales = 16;
extern const char* const kMakuScaleNames[kNumMakuScales];
extern const uint16_t kMakuScaleMasks[kNumMakuScales];
int makuScaleOf(int seqScale);              // a sequencer scale (gen::kScales) -> the nearest MAKU scale (not CHR)
int seqScaleOf(int makuScale);              // and back

// Plugin parameters
inline const char* kOnId = "mkon";
inline const char* kLevelId = "mklevel";
inline const char* kClockId = "mkclock";    // 0 HOST (MIDI clock from the host transport), 1 FREE (MAKU's own tempo)
inline const char* kKeyId = "mkkey";        // 0 SEQ > MAKU, 1 MAKU > SEQ, 2 OFF (each its own key)
enum class KeyLink { SeqToMaku = 0, MakuToSeq, Off };
enum class Clock { Host = 0, Free, FreeHostTempo };   // kClockId: HOST, FREE, FREE at the host's tempo (HOST BPM)
// the link MAKU > sequencer (gen::Link) and sequencer / DAW > MAKU
inline const char* kChordId = "mkchord";    // 0..100 %: melodic EUCLID notes moved to MAKU's chord
inline const char* kVerbsId = "mkverbs";    // 0..100 %: how far MAKU's gestures move the sequencer too
inline const char* kFeedId = "mkfeed";      // 0 OFF, 1..6: that track's notes melt into MAKU's VOICE phrase
// MIDI from the DAW: channels 11-14 play PULSE..VOICE, 15 the part in focus (and its notes melt into VOICE);
// on channel 15 CC 102-107 hold SINK..SCRAMBLE, 108 is DRIFT's key, 109 NEW WORLD, 110 LIKE (each >= 64 = pressed)
constexpr int kMidiFirstPartChannel = 11, kMidiFocusChannel = 15, kMidiVerbCC = 102;
void addLayout(juce::AudioProcessorValueTreeState::ParameterLayout& layout);

// The library. Missing (a build without MAKU, or a file that would not load): available() is false and the plugin
// is Monomodule Euclid alone.
class MakuLib {
public:
    MakuLib();
    ~MakuLib();
    bool available() const { return m_ok; }
    juce::String error() const { return m_error; }

    void boot(uint32_t seed);               // erased flash, a first world from seed, then the firmware's power-on
    void render(int n, const float*& left, const float*& right);   // n <= 1024 frames at 44.1 kHz
    void realtime(int status);              // MIDI realtime: 0xF8 clock, 0xFA start, 0xFC stop
    void clockMode(bool external);
    int keyGet() const;                     // root | scale << 8 (MAKU scale)
    void keySet(int root, int scale);
    void fifth(int dir);
    void note(int part, int note, int velocity, bool learn);   // part 0..3, 4 = in focus; velocity 0 = off
    void learn(int note);                   // melts into the VOICE phrase without playing
    void setTempo(int bpm);                 // MAKU's own clock
    uint16_t chordMask() const;             // FLOOR's chord now, pitch classes
    float verbAmount(int i) const;          // 0..1, i: SINK CASCADE SWELL WOBBLE LIFT SCRAMBLE
    void buttons(uint32_t mask);
    void keys(uint32_t mask);
    int focus() const;
    void setFocus(int part);
    int level(int part) const;
    void setLevel(int part, int v);
    int macro(int part, int j) const;
    void setMacro(int part, int j, int v);
    const char* partName(int part) const;
    const char* macroName(int part, int j) const;
    const char* voiceName(int part) const;
    int bpm() const;
    bool drift() const;
    uint32_t verbsHeld() const;
    uint32_t litButtons() const;
    uint32_t breathButtons() const;
    uint32_t litKeys() const;
    int worldNumber() const;
    uint32_t seed() const;
    bool playing() const;
    void newWorld();
    void like();
    void clearLikes();
    int likes() const;
    bool liked() const;
    juce::MemoryBlock packLikes() const;
    void unpackLikes(const juce::MemoryBlock&);
    juce::MemoryBlock packWorld() const;    // empty if there is no world to keep yet
    bool applyWorld(const juce::MemoryBlock&);
    const uint16_t* screen() const;         // 240 x 240, RGB565 high byte first
    uint32_t screenDraws() const;

private:
    struct Fns;
    std::unique_ptr<Fns> m_fn;
    std::unique_ptr<juce::DynamicLibrary> m_lib;
    juce::File m_file;
    bool m_ok = false;
    juce::String m_error;
};

// What the editor reads (a copy, taken under a spin lock at most ~25 times a second by the audio thread).
struct MakuUi {
    bool available = false, running = false, drift = false, liked = false, extClock = true;
    int focus = 0, bpm = 0, root = 0, scale = 2, likes = 0, world = 0;
    uint32_t seed = 0, verbsHeld = 0, litButtons = 0, breathButtons = 0, litKeys = 0;
    std::array<int, kParts> level{};
    std::array<std::array<int, 3>, kParts> macro{};
    using Name = std::array<char, 24>;      // (no allocation on the audio thread)
    std::array<Name, kParts> part{}, voice{};
    std::array<std::array<Name, 3>, kParts> macroName{};
    uint16_t chordMask = 0;
    std::array<float, 6> verb{};           // the upper verbs' amounts now
    uint32_t screenSerial = 0;
    std::array<uint16_t, kScreen * kScreen> screen{};
};

struct MakuCommand {
    enum Op : int { Buttons, Keys, Focus, Level, Macro, NewWorld, Like, LikesClear, Fifth } op = Buttons;
    int a = 0, b = 0, c = 0;
};

} // namespace mnm::plugin::one::maku
