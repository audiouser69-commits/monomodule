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

// The six tracks: key, ON box, step strip, readout.
class StepGrid : public juce::Component {
public:
    static constexpr int kRowH = 14, kKeyW = 40, kStripX = 44, kCellW = 7, kInfoX = kStripX + 32 * kCellW + 3, kLcdW = kInfoX + 40;
    explicit StepGrid(MnmOneProcessor& p) : m_proc(p) {}
    std::function<void(int)> onSelect;
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
private:
    void setHelp(const juce::String& s) { if (s != m_help) { m_help = s; repaint(m_helpArea); } }
    void shake();        // a new musical rhythm for the selected track
    void clearMutes();
    MnmOneProcessor& m_proc;
    int m_track = 0;
    StepGrid m_grid;
    LanePanel m_lanes;
    SeqKnobPage m_rhythm{"RHYTHM"}, m_motion{"MOTION"}, m_pitch{"PITCH"};
    Necklace m_necklace;
    LcdButton m_shake{"SHAKE"}, m_clear{"CLEAR"};
    juce::String m_help;
    juce::Rectangle<int> m_helpArea;
};

// The window's content: tab bar, then Six's editor or the sequencer page.
class EuclidView : public juce::Component, private juce::Timer {
public:
    static constexpr int kTabLcdH = 14;
    explicit EuclidView(MnmOneProcessor& p);
    ~EuclidView() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void childBoundsChanged(juce::Component*) override;
    void mouseDown(const juce::MouseEvent&) override;   // the tabs
    void mouseMove(const juce::MouseEvent&) override;
    void showSequencer(bool seq);
    bool sequencerShown() const { return m_seqShown; }
    OneEditor& synthEditor() { return m_synth; }
    SeqTabPage& seqPage() { return m_seq; }
private:
    void timerCallback() override;
    void layoutSize();
    juce::Rectangle<int> tabRect(int i) const;   // screen px
    MnmOneProcessor& m_proc;
    OneLookAndFeel m_lnf;
    OneEditor m_synth;
    SeqTabPage m_seq;
    SeqField m_seed, m_loop, m_evolve, m_root, m_scale, m_run;
    LcdButton m_dice{"DICE"};
    bool m_seqShown = true;
    int m_lastSynthTrack = 0;
    juce::String m_transport;
};

} // namespace mnm::plugin::one
