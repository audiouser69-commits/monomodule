// Monomodule Euclid: the front panel around the LCD, styled after a hardware groovebox (dark anodised chassis,
// silkscreened labels, rubber keys with LEDs, knurled encoders). Original artwork: no maker's logo or trade dress.
//   HwKey      a rubber key with an LED window and a silkscreen label
//   HwEncoder  an endless-style data entry knob (a rotary juce::Slider drawn as hardware)
//   TrigRow    the 16 trig keys: in GRID mode they write notes (click adds / selects / removes, drag up-down = pitch,
//              drag right = length), in EUCLID mode they show the hits and mute steps by hand
//   HwKeyboard two octaves of keys: plays the track, and writes the note into the selected step
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "OneProcessor.h"

namespace mnm::plugin::one::hw {

namespace colour {
inline const juce::Colour chassisTop{0xff26272a}, chassisBottom{0xff141517}, panelLine{0xff3a3b3f};
inline const juce::Colour silk{0xffd9d6cc}, silkDim{0xff8d8b85}, key{0xff2d2e31}, keyTop{0xff3a3b3f}, keyEdge{0xff0c0c0d};
inline const juce::Colour led{0xffff4a1c}, ledDim{0xff4a1a10}, ledGreen{0xff4cff7a}, ledAmber{0xffffb020};
inline const juce::Colour bezel{0xff08090a};
}

juce::Font font(float height, bool bold = false);
void drawScrew(juce::Graphics& g, juce::Point<float> c, float r);

class HwKey : public juce::Button {
public:
    explicit HwKey(const juce::String& label, juce::Colour ledColour = colour::led) : juce::Button(label), m_led(ledColour) {}
    void setLed(float level) { if (std::abs(level - m_level) > 0.01f) { m_level = level; repaint(); } }
    void setLedColour(juce::Colour c) { m_led = c; repaint(); }
    void setCaption(const juce::String& s) { if (s != m_caption) { m_caption = s; repaint(); } }   // small text on the key
    bool labelAbove = false;
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
private:
    juce::Colour m_led;
    float m_level = 0.0f;
    juce::String m_caption;
};

// The look of every encoder.
class EncoderLook : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

class HwEncoder : public juce::Slider {
public:
    HwEncoder();
    ~HwEncoder() override { setLookAndFeel(nullptr); }
private:
    EncoderLook m_look;
};

class TrigRow : public juce::Component {
public:
    explicit TrigRow(MnmOneProcessor& p);
    std::function<int()> track;               // the selected track
    std::function<int&(int)> selectedStep;    // per track
    std::function<int&()> lastNote, lastLength;
    std::function<void(const juce::String&)> onHint;
    int page = 0;                             // 0: steps 1-16, 1: 17-32
    void tick();
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { m_hover = -1; repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    bool gridMode() const;
    int steps() const;
    int stepAt(juce::Point<float> p) const;   // -1 outside the keys
    juce::Rectangle<float> keyRect(int k) const;   // k = 0..15 on the page
    MnmOneProcessor& m_proc;
    int m_downStep = -1, m_dragMode = 0;   // 0 undecided, 1 pitch, 2 length
    bool m_created = false, m_wasSelected = false, m_moved = false;
    uint32_t m_downCell = 0;
    float m_wheel = 0.0f;
    int m_hover = -1;
};

class HwKeyboard : public juce::Component {
public:
    std::function<void(int note, bool on)> onNote;
    int baseOctave = 3;                       // C3 at the left (MIDI 48)
    int highlighted = -1;                     // the selected step's note
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
private:
    int noteAt(juce::Point<float> p) const;
    juce::Rectangle<float> whiteRect(int i) const;
    int m_down = -1;
};

} // namespace mnm::plugin::one::hw
