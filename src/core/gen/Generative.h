// Generative Euclidean sequencer for Monomodule Euclid (no JUCE: the plugin feeds it parameters and the host
// transport each block and turns the events it returns into MIDI for the six tracks).
//
// Per track: a Euclidean rhythm (STEPS / PULSES / ROTATE) at its own RATE (straight, triplet and odd tuplets, so the
// tracks drift against each other polymetrically), a second Euclidean pattern for ACCENTS, trig PROBABILITY and an
// Elektron-style trig CONDITION (X:Y, PRE, NEI...), RATCHETS (retrigs inside the step, with a FLAT / ACCEL / DECEL /
// RANDOM timing shape and an optional pitch drift per repeat), GATE, SWING and DRIFT (timing jitter).
// Notes are always taken from a RANGE (LOW..HIGH) quantised to the global SCALE / ROOT, through a pitch MODE:
// FIXED, RANDOM, WALK (brownian), TURING (a shift register that mutates), CYCLE (a melody of LEN notes that phases
// against the rhythm). Four PARAMETER LANES per track p-lock any SYN/AMP/FILT/EFX knob of the Monomachine on every
// trig, inside a MIN..MAX window, with their own shapes; a lock lasts until the track's next trig, as on the hardware.
//
// Determinism: every random decision is a hash of (SEED, track, purpose, position, generation), so the same song
// position always plays the same thing and a bounce sounds like the playback. LOOP (bars) folds the position so the
// texture repeats; EVOLVE lets each step of the loop mutate with some chance every time the loop comes around.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace mnm::gen {

constexpr int kTracks = 6;
constexpr int kLanes = 4;
constexpr int kMaxSteps = 32;
constexpr int kEvolveSlots = 1024;   // loop positions with their own generation counter
constexpr int kLockTargets = 32;     // SYN A-H, AMP 1-8, FILT 1-8, EFX 1-8 (page * 8 + knob)

// ---- choice lists (indices are parameter values) ----
constexpr int kNumRates = 12;
constexpr const char* kRateNames[kNumRates] = {"1/32", "1/28", "1/24", "1/20", "1/16", "1/14", "1/12", "1/10", "1/8", "1/6", "1/4", "1/2"};
constexpr double kRateQuarters[kNumRates] = {0.125, 1.0 / 7.0, 1.0 / 6.0, 0.2, 0.25, 2.0 / 7.0, 1.0 / 3.0, 0.4, 0.5, 2.0 / 3.0, 1.0, 2.0};

enum class Cond : int { Always = 0, P1_2, P2_2, P1_3, P2_3, P3_3, P1_4, P2_4, P3_4, P4_4, Pre, NotPre, Nei, NotNei, First, NotFirst, Count };
constexpr const char* kCondNames[int(Cond::Count)] = {"ALWAYS", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4",
                                                      "PRE", "!PRE", "NEI", "!NEI", "1ST", "!1ST"};

enum class Shape : int { Flat = 0, Accel, Decel, Random, Count };
constexpr const char* kShapeNames[int(Shape::Count)] = {"FLAT", "ACCEL", "DECEL", "RANDOM"};

enum class PitchMode : int { Fixed = 0, Random, Walk, Turing, Cycle, Count };
constexpr const char* kPitchModeNames[int(PitchMode::Count)] = {"FIXED", "RANDOM", "WALK", "TURING", "CYCLE"};

enum class LaneMode : int { Random = 0, Cycle, RampUp, RampDown, Triangle, Sine, Accent, Pitch, Turing, Count };
constexpr const char* kLaneModeNames[int(LaneMode::Count)] = {"RANDOM", "CYCLE", "RAMP UP", "RAMP DN", "TRIANGLE", "SINE", "ACCENT", "PITCH", "TURING"};

constexpr int kNumLoops = 7;
constexpr const char* kLoopNames[kNumLoops] = {"OFF", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS", "32 BARS"};
constexpr int kLoopBars[kNumLoops] = {0, 1, 2, 4, 8, 16, 32};

struct Scale { const char* name; uint16_t mask; };   // bit n = semitone n above the root
constexpr int kNumScales = 22;
constexpr Scale kScales[kNumScales] = {
    {"CHROMATIC", 0xFFF}, {"MAJOR", 0xAB5}, {"MINOR", 0x5AD}, {"DORIAN", 0x6AD}, {"PHRYGIAN", 0x5AB}, {"LYDIAN", 0xAD5},
    {"MIXOLYDIAN", 0x6B5}, {"LOCRIAN", 0x56B}, {"HARM MINOR", 0x9AD}, {"MEL MINOR", 0xAAD}, {"PENT MAJOR", 0x295},
    {"PENT MINOR", 0x4A9}, {"BLUES", 0x4E9}, {"WHOLE TONE", 0x555}, {"HIRAJOSHI", 0x18D}, {"IN SEN", 0x4A3},
    {"PELOG", 0x1A3}, {"PROMETHEUS", 0x655}, {"MIN7 CHORD", 0x489}, {"MAJ7 CHORD", 0x891}, {"FIFTHS", 0x081}, {"OCTAVES", 0x001}};
constexpr const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// ---- GRID mode: notes written by hand ----
// A track is EUCLID (the generator decides hits and notes) or GRID (32 steps written by hand, Elektron style: each step
// a trig with its own note and length). Everything else - chance, condition, ratchets, swing, drift, the lanes - applies
// to both. A grid step is packed in 32 bits: bit 31 trig, bits 0-6 note, bits 8-15 length in quarter steps (1..128).
enum class TrackMode : int { Euclid = 0, Grid, Count };
constexpr const char* kTrackModeNames[int(TrackMode::Count)] = {"EUCLID", "GRID"};
constexpr uint32_t kGridTrig = 0x80000000u;
inline bool gridTrig(uint32_t c) { return (c & kGridTrig) != 0; }
inline int gridNote(uint32_t c) { return int(c & 0x7Fu); }
inline int gridLength(uint32_t c) { const int l = int((c >> 8) & 0xFFu); return l < 1 ? 4 : l; }   // quarter steps
inline uint32_t gridCell(bool trig, int note, int lengthQuarters)
{
    note = note < 0 ? 0 : note > 127 ? 127 : note;
    lengthQuarters = lengthQuarters < 1 ? 1 : lengthQuarters > 128 ? 128 : lengthQuarters;
    return (trig ? kGridTrig : 0u) | uint32_t(note) | (uint32_t(lengthQuarters) << 8);
}

// ---- parameters (read by the plugin from its parameter tree every block) ----
struct LaneParams {
    int target = -1;          // -1 off, else page * 8 + knob (page 0 SYN, 1 AMP, 2 FILT, 3 EFX)
    int min = 0, max = 127;   // raw kit bytes; min > max inverts the shape
    LaneMode mode = LaneMode::Random;
    int length = 8;           // period in trigs (CYCLE, RAMP, TRIANGLE) or in steps (SINE)
    int chance = 100;         // % of trigs that get the lock
};

struct TrackParams {
    bool on = false;
    TrackMode mode = TrackMode::Euclid;
    std::array<uint32_t, kMaxSteps> grid{};   // GRID mode
    int steps = 16, pulses = 4, rotate = 0;
    int rate = 4;             // index into kRateNames
    int accents = 0;          // pulses of the accent pattern (same steps, same rotation)
    int prob = 100;           // %
    Cond cond = Cond::Always;
    int ratchetChance = 0;    // %
    int ratchetMax = 1;       // 1 = off, else 2..8 repeats
    Shape ratchetShape = Shape::Flat;
    int ratchetPitch = 0;     // scale degrees per repeat (-4..+4)
    int gate = 50;            // % of the step (or of the ratchet's slice)
    int swing = 0;            // 0..100: delay of every second step, up to half a step
    int drift = 0;            // 0..100: random delay, up to half a step
    PitchMode pitchMode = PitchMode::Fixed;
    int scale = -1;           // index into kScales, -1 = the global scale
    int low = 48, high = 72;  // MIDI note range
    int length = 8;           // TURING / CYCLE length; WALK max jump in degrees
    int mutate = 10;          // % (TURING flips, CYCLE replacements)
    int hold = 0;             // % chance to repeat the previous note
    uint32_t stepMask = 0;    // bit n = step n muted by hand
    std::array<LaneParams, kLanes> lanes{};
};

// What another engine playing beside the sequencer feeds into it (Monomodule Euclid: MAKU). Live, not part of the
// song: the decisions it changes still come from the seed, so the same position with the same link plays the same.
struct Link {
    uint16_t chordMask = 0;   // pitch classes of the chord heard now (bit n = n semitones above C), 0 = none
    int chord = 0;            // % of the melodic EUCLID notes moved to the nearest note of that chord
    // the gestures held now, 0..1 each: SINK thins (the drums most), CASCADE adds rising ratchets, SWELL lengthens the
    // gates, WOBBLE loosens the timing, LIFT lifts melodic notes an octave, SCRAMBLE breaks rhythm and pitch
    float sink = 0.0f, cascade = 0.0f, swell = 0.0f, wobble = 0.0f, lift = 0.0f, scramble = 0.0f;
    // DRIFT (0..1): the EUCLID patterns wander slowly round their settings, a new place every four bars, reached bar by
    // bar: PULSES +-1.5, ROTATE +-3, a little less CHANCE
    float drift = 0.0f;
    bool active() const { return (chordMask && chord > 0) || sink > 0 || cascade > 0 || swell > 0 || wobble > 0 || lift > 0 || scramble > 0 || drift > 0; }
};
int nearestChordNote(int note, uint16_t chordMask, int low, int high);   // the chord note nearest note, inside [low, high] if one is

struct GlobalParams {
    Link link;
    int seed = 1;
    int loop = 0;             // index into kLoopBars
    int evolve = 0;           // % per loop step per cycle
    int root = 0;             // 0..11
    int scale = 2;            // index into kScales
    std::array<TrackParams, kTracks> tracks{};
};

struct Transport {
    bool playing = false;
    double ppq = 0.0;          // position of the block's first sample, in quarter notes
    double bpm = 120.0;
    double sampleRate = 44100.0;
    int numSamples = 0;
    double barQuarters = 4.0;  // time signature: numerator * 4 / denominator
};

// One short MIDI message (note on/off, CC, or a 5/7-byte p-lock sysex).
struct Event {
    int sample = 0;
    uint8_t prio = 0;          // order at equal sample: 0 all notes off, 1 note off, 2 lock, 3 note on
    uint8_t size = 0;
    uint8_t data[8] = {};
};

// P-lock sysex (manufacturer 0x7D, non-commercial): F0 7D 01 t F7 clears every lock of track t;
// F0 7D 02 t idx value F7 locks knob idx (page * 8 + knob) of track t to value until the next clear.
constexpr uint8_t kSysexId = 0x7D;
constexpr uint8_t kLockClear = 0x01, kLockSet = 0x02;

// Euclidean rhythm (Bresenham form, Bjorklund up to rotation): is step i (0..steps-1) a pulse? rotate moves the
// pattern to the right.
bool euclidHit(int steps, int pulses, int rotate, int i);
// The notes of a scale inside [low, high] (ascending), up to 128; returns the count.
int scaleNotes(int root, int scale, int low, int high, uint8_t* out);
uint64_t hash64(uint64_t a, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0, uint64_t e = 0);
inline double unit(uint64_t h) { return double(h >> 11) * (1.0 / 9007199254740992.0); }   // [0, 1)

class Generator {
public:
    static constexpr int kMaxEvents = 2048;

    Generator();
    // Plays one host block: decides the steps that start in it, emits the events due in it (sorted).
    void process(const Transport& tr, const GlobalParams& gp);
    int numEvents() const { return m_numEvents; }
    const Event& event(int i) const { return m_events[size_t(i)]; }
    void setTranspose(int semitones) { m_transpose = semitones; }
    void reset();   // forget everything scheduled (no events)

    // UI feedback (any thread)
    struct UiTrack {
        std::atomic<int> step{-1};        // pattern position of the last decided step, -1 stopped
        std::atomic<int> trigs{0};        // counts played trigs (the UI flashes on change)
        std::atomic<int> note{-1};        // last note
        std::atomic<int> ratchets{0};     // repeats of the last trig
        std::atomic<int> lock[kLanes];    // last lane values (-1 none)
    };
    std::array<UiTrack, kTracks> ui;
    std::atomic<bool> uiPlaying{false};
    std::atomic<double> uiPpq{0.0};   // play position of the last block

private:
    struct Pending { double time; uint8_t prio, size; uint8_t data[7]; };
    struct TrackState {
        std::array<Pending, 256> queue{};
        int count = 0;
        double nextPpq = 0.0;      // grid time of the next step to decide
        bool lastPlayed = false;
        int degree = -1;           // WALK position in the range (index), -1 = unset
        uint16_t turing = 0;       // TURING register
        bool turingSet = false;
        int lastNote = -1;
        int64_t trigCount = 0;     // trigs since the loop start (CYCLE / RAMP lanes)
        std::array<uint16_t, kEvolveSlots> gen{};
        std::array<int64_t, kEvolveSlots> genCycle{};   // the loop cycle that last evolved each slot
        bool sounding = false;
    };

    void decideStep(int t, int64_t s, double q, const Transport& tr, const GlobalParams& gp);
    void push(int t, double time, uint8_t prio, std::initializer_list<uint8_t> bytes);
    void emit(int sample, uint8_t prio, std::initializer_list<uint8_t> bytes);
    void allNotesOff(int sample, const GlobalParams& gp);
    int pickNote(int t, TrackState& st, const TrackParams& p, const GlobalParams& gp, uint64_t key, const uint8_t* notes, int n, int& idx);

    std::array<TrackState, kTracks> m_tracks{};
    std::array<Event, kMaxEvents> m_events{};
    int m_numEvents = 0;
    bool m_wasPlaying = false;
    double m_expectedPpq = 0.0;
    int m_transpose = 0;
};

} // namespace mnm::gen
