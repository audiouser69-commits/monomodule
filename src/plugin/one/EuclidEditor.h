// Monomodule Euclid editor: Six's editor (SYNTH tab) and the generative sequencer (SEQUENCER tab) in one window,
// both drawn in the Monomachine's LCD style. The tab bar also holds the global sequencer settings (seed, loop,
// evolve, root, scale) so they are in view on both tabs.
//
// SEQUENCER tab, top to bottom:
//   the six tracks: track key (click selects the track, the box switches its sequencer on), the Euclidean step strip
//     (hits, accents, the play position; a click mutes or unmutes a step by hand) and a readout (rate, last note);
//   beside them the selected track's four PARAMETER LANES (target knob, MIN-MAX window with the live lock value,
//     shape, length, chance);
//   the selected track's pages RHYTHM, MOTION and PITCH, and its pattern drawn as a necklace (the Euclidean polygon);
//   a help line that explains whatever the mouse is over.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "OneEditor.h"
#include "EuclidParams.h"
#include "EuclidPanel.h"

namespace mnm::plugin::one {

// Drag, wheel, double-click and list-menu handling for one parameter; subclasses draw it.
class SeqControl : public juce::Component {
public:
    SeqControl() = default;
    void bind(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId);
    void unbind() { m_attach.reset(); m_param = nullptr; repaint(); }
    juce::RangedAudioParameter* param() const { return m_param; }
    juce::String valueText() const;                    // caps; textFn overrides the parameter's own text
    std::function<juce::String(int)> textFn;          // value (denormalised, int) -> text
    std::function<juce::String()> helpFn;              // the help line for this control
    int intValue() const { return m_param ? int(std::lround(m_param->convertFrom0to1(m_param->getValue()))) : 0; }
    void setIntValue(int v);                           // a complete gesture
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
protected:
    virtual bool clickOpensMenu() const;   // a short list: a click opens it
    void showMenu();
    juce::RangedAudioParameter* m_param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> m_attach;
    float m_dragStart = 0.0f;
    bool m_dragged = false;
    float m_wheelAcc = 0.0f;
};

// A knob cell (32 x 32 LCD px): label, dial, value, like the synth pages.
class SeqKnob : public SeqControl {
public:
    juce::String label;
    std::function<bool()> dimFn;   // greyed out (does nothing in the current mode)
    void paint(juce::Graphics&) override;
};

// "LABEL VALUE" on one line of LCD text (tab bar, lanes).
class SeqField : public SeqControl {
public:
    juce::String label;
    bool inverted = false;
    void paint(juce::Graphics&) override;
};

// A page: inverted title bar over a 2 x 4 grid of knob cells.
class SeqKnobPage : public juce::Component {
public:
    static constexpr int kLcdW = 4 * kCell + 1, kLcdH = 11 + 2 * kCell + 1;
    explicit SeqKnobPage(const char* title);
    std::array<SeqKnob, 8> knobs;
    juce::String title;
    void paint(juce::Graphics&) override;
    void resized() override;
};

// A lane's MIN..MAX window: drag a handle (or the bar), live lock value as a tick.
class RangeBar : public juce::Component {
public:
    void bind(juce::AudioProcessorValueTreeState& apvts, const juce::String& minId, const juce::String& maxId);
    std::function<int()> liveValue;   // -1 none
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
private:
    int valueAt(float x) const;
    juce::RangedAudioParameter* m_min = nullptr;
    juce::RangedAudioParameter* m_max = nullptr;
    std::unique_ptr<juce::ParameterAttachment> m_minAttach, m_maxAttach;
    juce::ParameterAttachment* m_dragging = nullptr;
};

class EuclidView;

// GRID editing state shared by the panel's trig keys, the keyboard and the LCD: the selected step of each track and the
// note and length a new trig gets.
struct GridEdit {
    std::array<int, mnm::gen::kTracks> step{};
    int lastNote = 60;
    int lastLength = 4;   // quarter steps
};

// The six tracks: key, ON box, step strip, readout.
class StepGrid : public juce::Component {
public:
    static constexpr int kRowH = 14, kKeyW = 40, kStripX = 44, kCellW = 7, kInfoX = kStripX + 32 * kCellW + 3, kLcdW = kInfoX + 40;
    explicit StepGrid(MnmOneProcessor& p) : m_proc(p) {}
    std::function<void(int)> onSelect;
    GridEdit* edit = nullptr;
    int selected = 0;
    void tick();   // timer: flashes and the play position
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { m_hover = {-1, -1}; repaint(); }
    juce::String helpAt(juce::Point<int> local) const;
private:
    int param(int t, const char* name) const;
    MnmOneProcessor& m_proc;
    std::array<int, mnm::gen::kTracks> m_lastTrigs{};
    std::array<double, mnm::gen::kTracks> m_flashUntil{};
    juce::Point<int> m_hover{-1, -1};   // track, step
};

// The selected track's pattern as a necklace: the steps round a circle, the hits joined into the Euclidean polygon.
class Necklace : public juce::Component {
public:
    explicit Necklace(MnmOneProcessor& p) : m_proc(p) {}
    int track = 0;
    void paint(juce::Graphics&) override;
private:
    MnmOneProcessor& m_proc;
};

// GRID mode: the selected step on the LCD (STEP, NOTE, LEN, TRIG) and whole-pattern tools (OCT, SHIFT, EUC>GRID, CLEAR).
class GridStepPage : public juce::Component {
public:
    static constexpr int kLcdW = 4 * kCell + 1, kLcdH = 11 + 2 * kCell + 1;
    explicit GridStepPage(MnmOneProcessor& p) : m_proc(p) {}
    int track = 0;
    GridEdit* edit = nullptr;
    std::function<void()> onChanged;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { m_hover = -1; repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String helpAt(juce::Point<int> local) const;
private:
    int cellAt(juce::Point<int> local) const;
    void nudge(int cell, int delta);
    MnmOneProcessor& m_proc;
    int m_down = -1, m_hover = -1, m_applied = 0;
    float m_wheel = 0.0f;
};

// Copies the track's Euclidean pattern into its GRID steps (notes from its range and scale), and clears or shifts them.
void captureEuclidToGrid(MnmOneProcessor& p, int t);
void clearGrid(MnmOneProcessor& p, int t);
void shiftGrid(MnmOneProcessor& p, int t, int by);
void transposeGrid(MnmOneProcessor& p, int t, int semitones);

// The four parameter lanes of the selected track.
class LanePanel : public juce::Component {
public:
    static constexpr int kLcdW = 141, kRowH = 18, kLcdH = 11 + 4 * kRowH + 1;
    explicit LanePanel(MnmOneProcessor& p);
    void setTrack(int t);
    void paint(juce::Graphics&) override;
    void resized() override;
    void tick() { for (auto& r : m_range) r.repaint(); }
private:
    juce::String targetName(int target) const;   // SYN knobs named by the track's machine
    MnmOneProcessor& m_proc;
    int m_track = 0;
    std::array<SeqField, mnm::gen::kLanes> m_target, m_mode, m_len, m_chance;
    std::array<RangeBar, mnm::gen::kLanes> m_range;
};

class SeqTabPage : public juce::Component {
public:
    static constexpr int kLcdW = 459, kLcdH = 196;
    explicit SeqTabPage(MnmOneProcessor& p);
    void setTrack(int t);
    int track() const { return m_track; }
    std::function<void(int)> onTrackSelected;
    void tick();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;   // the help line (listens to every child)
    void mouseExit(const juce::MouseEvent&) override { setHelp({}); }
    void setGridEdit(GridEdit* e) { m_grid.edit = e; m_step.edit = e; }
    void setHelp(const juce::String& s) { if (s != m_help) { m_help = s; repaint(m_helpArea); } }
    void showHint(const juce::String& s) { setHelp(s); }
private:
    bool gridMode() const;
    void shake();        // a new musical rhythm for the selected track
    void clearMutes();
    MnmOneProcessor& m_proc;
    int m_track = 0;
    StepGrid m_grid;
    LanePanel m_lanes;
    SeqKnobPage m_rhythm{"RHYTHM"}, m_motion{"MOTION"}, m_pitch{"PITCH"};
    GridStepPage m_step;
    Necklace m_necklace;
    LcdButton m_shake{"SHAKE"}, m_clear{"CLEAR"};
    juce::String m_help;
    juce::Rectangle<int> m_helpArea;
};

// MAKU on the LCD (MakuPage.cpp): its own 240 x 240 colour screen as it draws it, the four parts (name, the voice the
// world gave it, LEVEL and its three macros: drag or wheel to change, click the name to focus it), the world (key,
// tempo, seed, likes) and its settings in the status line (CLOCK, KEY link, LEVEL, ON); NEW WORLD, LIKE, FLAT and
// SHARP as LCD buttons. The panel around it plays the verbs (EuclidView).
class MakuPage : public juce::Component {
public:
    static constexpr int kLcdW = SeqTabPage::kLcdW, kLcdH = 14 + SeqTabPage::kLcdH;
    explicit MakuPage(MnmOneProcessor& p);
    void tick();   // timer: a fresh copy of MAKU's state
    const maku::MakuUi& ui() const { return m_ui; }
    void setHelp(const juce::String& s) { if (s != m_help) { m_help = s; repaint(); } }
    std::function<void(int part)> onFocus;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { m_hover = -1; setHelp({}); repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    static void fifth(MnmOneProcessor& p, int dir);   // FLAT / SHARP: the shared key round the circle of fifths
private:
    int cellAt(juce::Point<int> local) const;          // part * 8 + column (0 name, 1 level, 2..4 macros), -1 none
    juce::Rectangle<int> cellRect(int cell) const;      // in LCD px
    int cellValue(int cell) const;
    void setCellValue(int cell, int v);
    juce::String helpOf(int cell) const;
    MnmOneProcessor& m_proc;
    maku::MakuUi m_ui;
    juce::Image m_screen{juce::Image::RGB, maku::kScreen, maku::kScreen, true};
    uint32_t m_screenSerial = ~0u;
    SeqField m_clock, m_key, m_level, m_on;
    LcdButton m_newWorld{"NEW WORLD"}, m_like{"LIKE"}, m_flat{"FLAT"}, m_sharp{"SHARP"};
    int m_hover = -1, m_drag = -1, m_dragStart = 0, m_dragValue = -1;
    float m_wheel = 0.0f;
    juce::String m_help;
};

// The window's content: a hardware-style front panel. The LCD in the middle shows Six's pages (SYNTH) or the sequencer
// (SEQ); around it the track keys, the function keys (SYNTH, SEQ, GRID, REC, CLEAR), eight data entry encoders with
// their page keys, the 16 trig keys and a two-octave keyboard.
class EuclidView : public juce::Component, private juce::Timer {
public:
    explicit EuclidView(MnmOneProcessor& p);
    ~EuclidView() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;   // the glass over the LCD
    void resized() override;
    void childBoundsChanged(juce::Component*) override;
    void showSequencer(bool seq);
    bool sequencerShown() const { return m_seqShown; }
    void showMaku();                     // MAKU on the LCD, the panel playing it
    bool makuShown() const { return m_makuShown; }
    MakuPage& makuPage() { return m_maku; }
    void selectTrack(int t);
    int selectedTrack() const { return m_track; }
    void setEncoderPage(int page);
    OneEditor& synthEditor() { return m_synth; }
    SeqTabPage& seqPage() { return m_seq; }
    GridEdit& gridEdit() { return m_edit; }
    hw::TrigRow& trigRow() { return m_trigs; }
    enum EncPage { SynPage = 0, AmpPage, FiltPage, EfxPage, Lfo1Page, Lfo2Page, Lfo3Page, RhythmPage, MotionPage, NumEncPages };

private:
    void timerCallback() override;
    void layoutSize();
    void bindEncoders();
    juce::String encoderLabel(int k) const;
    bool gridMode() const;
    void setMode(bool grid);
    void writeNote(int note, bool advance);
    void setHint(const juce::String& s);
    void leaveMaku();                    // the panel back to the tracks (labels, held keys let go)
    void makuButtons();                  // the verb keys held now -> MAKU's buttons
    void makuNote(int note, bool on);
    static constexpr int kMargin = 26, kHeaderH = 64, kLeftW = 176, kRightW = 320, kGap = 18, kBezel = 16;
    static constexpr int kStatusH = 14 * kScale, kTrigH = 128, kKeysH = 100;

    MnmOneProcessor& m_proc;
    OneLookAndFeel m_lnf;
    OneEditor m_synth;
    SeqTabPage m_seq;
    MakuPage m_maku;
    SeqField m_seed, m_loop, m_evolve, m_root, m_scale, m_run;
    LcdButton m_dice{"DICE"};
    GridEdit m_edit;

    std::array<std::unique_ptr<hw::HwKey>, 6> m_trackKeys;
    hw::HwKey m_synthKey{"Synth", hw::colour::ledGreen}, m_seqKey{"Seq", hw::colour::ledGreen}, m_gridKey{"Grid", hw::colour::ledAmber},
              m_recKey{"Rec"}, m_clearKey{"Clear"}, m_captureKey{"Euc > Grid", hw::colour::ledAmber},
              m_makuKey{"Maku", hw::colour::ledGreen}, m_makuOnKey{"Maku On", hw::colour::ledGreen};
    std::array<std::unique_ptr<hw::HwKey>, NumEncPages> m_pageKeys;
    hw::HwKey m_stepPage1{"1 - 16"}, m_stepPage2{"17 - 32"}, m_octDown{"Oct -"}, m_octUp{"Oct +"};
    std::array<hw::HwEncoder, 8> m_enc;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 8> m_encAttach;
    hw::TrigRow m_trigs;
    hw::HwKeyboard m_keys;

    juce::Rectangle<int> m_lcd, m_bezel, m_trigArea, m_keysArea, m_encArea, m_transportArea, m_hintArea, m_statusArea;
    bool m_seqShown = true, m_makuShown = false;
    uint32_t m_mkKeys = 0, m_mkButtons = 0;   // MAKU: its keyboard keys and buttons held from the panel
    int m_track = 0, m_encPage = SynPage, m_lastSynthTrack = 0, m_boundTrack = -1, m_boundPage = -1, m_boundSlot = -1;
    std::array<int, 6> m_lastTrigs{};
    std::array<float, 6> m_trackFlash{};
    juce::String m_transport{"STOP"}, m_hint;
};

} // namespace mnm::plugin::one
