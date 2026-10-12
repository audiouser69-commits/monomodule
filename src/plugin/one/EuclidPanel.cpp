#include "EuclidPanel.h"
#include <cmath>

namespace mnm::plugin::one::hw {

using namespace mnm::gen;
namespace eu = euclid;

juce::Font font(float height, bool bold)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor(0.06f);
}

void drawScrew(juce::Graphics& g, juce::Point<float> c, float r)
{
    juce::ColourGradient grad(juce::Colour(0xff6a6b6f), c.x - r, c.y - r, juce::Colour(0xff1d1e20), c.x + r, c.y + r, false);
    g.setGradientFill(grad);
    g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour(juce::Colour(0xff0b0b0c));
    g.drawLine(c.x - r * 0.6f, c.y - r * 0.6f, c.x + r * 0.6f, c.y + r * 0.6f, 1.4f);
    g.drawLine(c.x - r * 0.6f, c.y + r * 0.6f, c.x + r * 0.6f, c.y - r * 0.6f, 1.4f);
}

static void drawKeyBody(juce::Graphics& g, juce::Rectangle<float> r, bool down, bool highlighted, bool disabled = false)
{
    g.setColour(colour::keyEdge);
    g.fillRoundedRectangle(r.translated(0.0f, 2.0f), 5.0f);
    auto body = down ? r.translated(0.0f, 1.5f) : r;
    juce::ColourGradient grad(disabled ? colour::key.darker(0.5f) : colour::keyTop, body.getX(), body.getY(),
                              disabled ? colour::key.darker(0.7f) : colour::key, body.getX(), body.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(body, 5.0f);
    g.setColour(juce::Colours::white.withAlpha(highlighted && !disabled ? 0.10f : 0.04f));
    g.drawRoundedRectangle(body.reduced(0.5f), 5.0f, 1.0f);
}

static void drawLed(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float level)
{
    g.setColour(juce::Colour(0xff120a08));
    g.fillRoundedRectangle(r, 1.5f);
    if (level <= 0.01f) return;
    const auto lit = c.withMultipliedBrightness(0.35f + 0.65f * level);
    if (level > 0.6f) {   // glow
        g.setColour(c.withAlpha(0.22f * level));
        g.fillRoundedRectangle(r.expanded(4.0f, 3.0f), 4.0f);
    }
    g.setColour(lit);
    g.fillRoundedRectangle(r, 1.5f);
    g.setColour(juce::Colours::white.withAlpha(0.35f * level));
    g.fillRoundedRectangle(r.withHeight(r.getHeight() * 0.4f).reduced(1.0f, 0.5f), 1.0f);
}

void HwKey::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto area = getLocalBounds().toFloat();
    const float labelH = 14.0f;
    auto label = labelAbove ? area.removeFromTop(labelH) : area.removeFromBottom(labelH);
    auto key = area.reduced(2.0f, 2.0f);
    drawKeyBody(g, key, down, highlighted, !isEnabled());
    drawLed(g, juce::Rectangle<float>(key.getX() + 7.0f, key.getY() + 6.0f, 12.0f, 5.0f).translated(0.0f, down ? 1.5f : 0.0f),
            m_led, juce::jmax(m_level, getToggleState() ? 1.0f : 0.0f));
    if (m_caption.isNotEmpty()) {
        g.setColour(colour::silk.withAlpha(0.85f));
        g.setFont(font(11.0f, true));
        g.drawText(m_caption, key.withTrimmedTop(12.0f).toNearestInt(), juce::Justification::centred, false);
    }
    g.setColour(isEnabled() ? colour::silk : colour::silkDim);
    g.setFont(font(10.5f, true));
    g.drawText(getButtonText().toUpperCase(), label.toNearestInt(), juce::Justification::centred, false);
}

// ---------------------------------------------------------------------------

void EncoderLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float>(float(x), float(y), float(w), float(h));
    const float r = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 6.0f;
    const auto c = bounds.getCentre();
    // scale: dots round the knob, the value as an arc of lit dots
    for (int i = 0; i <= 20; ++i) {
        const float a = start + (end - start) * float(i) / 20.0f;
        const float lit = s.isEnabled() && float(i) / 20.0f <= pos + 1e-4f ? 1.0f : 0.0f;
        const juce::Point<float> p(c.x + (r + 4.5f) * std::sin(a), c.y - (r + 4.5f) * std::cos(a));
        g.setColour(lit > 0.0f ? colour::led.withAlpha(0.9f) : colour::silkDim.withAlpha(0.6f));
        g.fillEllipse(p.x - 1.3f, p.y - 1.3f, 2.6f, 2.6f);
    }
    // body
    g.setColour(juce::Colour(0xff050505));
    g.fillEllipse(c.x - r, c.y - r + 2.0f, 2 * r, 2 * r);
    juce::ColourGradient grad(juce::Colour(0xff3c3d40), c.x, c.y - r, juce::Colour(0xff0e0e0f), c.x, c.y + r, false);
    g.setGradientFill(grad);
    g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
    // knurling
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    for (int i = 0; i < 36; ++i) {
        const float a = juce::MathConstants<float>::twoPi * float(i) / 36.0f;
        g.drawLine(c.x + (r - 3.0f) * std::sin(a), c.y - (r - 3.0f) * std::cos(a), c.x + r * std::sin(a), c.y - r * std::cos(a), 1.0f);
    }
    // cap
    const float cr = r * 0.66f;
    juce::ColourGradient cap(juce::Colour(0xff2b2c2f), c.x - cr, c.y - cr, juce::Colour(0xff121214), c.x + cr, c.y + cr, false);
    g.setGradientFill(cap);
    g.fillEllipse(c.x - cr, c.y - cr, 2 * cr, 2 * cr);
    // pointer
    const float a = start + (end - start) * pos;
    if (!s.isEnabled()) return;   // a knob the machine does not use: no pointer
    g.setColour(s.isMouseOverOrDragging() ? juce::Colours::white : colour::silk);
    g.drawLine(c.x + cr * 0.25f * std::sin(a), c.y - cr * 0.25f * std::cos(a), c.x + (r - 2.0f) * std::sin(a), c.y - (r - 2.0f) * std::cos(a), 2.4f);
}

HwEncoder::HwEncoder()
{
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters(juce::degreesToRadians(-145.0f), juce::degreesToRadians(145.0f), true);
    setMouseDragSensitivity(220);
    setVelocityBasedMode(false);
    setLookAndFeel(&m_look);
}

// ---------------------------------------------------------------------------
// TrigRow

TrigRow::TrigRow(MnmOneProcessor& p) : m_proc(p) {}

bool TrigRow::gridMode() const
{
    return int(std::lround(m_proc.apvts.getRawParameterValue(eu::id(track(), "mode"))->load())) == int(TrackMode::Grid);
}

int TrigRow::steps() const
{
    return juce::jlimit(1, kMaxSteps, int(std::lround(m_proc.apvts.getRawParameterValue(eu::id(track(), "steps"))->load())));
}

juce::Rectangle<float> TrigRow::keyRect(int k) const
{
    const float pitch = float(getWidth()) / 16.0f;
    return {pitch * float(k) + 5.0f, 22.0f, pitch - 10.0f, float(getHeight()) - 22.0f - 34.0f};
}

int TrigRow::stepAt(juce::Point<float> p) const
{
    const float pitch = float(getWidth()) / 16.0f;
    const int k = int(p.x / pitch);
    if (k < 0 || k > 15) return -1;
    return page * 16 + k;
}

void TrigRow::tick() { repaint(); }

void TrigRow::paint(juce::Graphics& g)
{
    const int t = track();
    const bool grid = gridMode();
    const int n = steps();
    auto* gen = m_proc.generator();
    const bool on = m_proc.apvts.getRawParameterValue(eu::id(t, "on"))->load() >= 0.5f;
    const int playing = gen && gen->uiPlaying.load() && on ? gen->ui[size_t(t)].step.load() : -1;
    const int pulses = int(std::lround(m_proc.apvts.getRawParameterValue(eu::id(t, "pulses"))->load()));
    const int rot = int(std::lround(m_proc.apvts.getRawParameterValue(eu::id(t, "rotate"))->load()));
    const uint32_t mask = m_proc.stepMask(t);
    const int sel = grid ? selectedStep(t) : -1;
    const float pitch = float(getWidth()) / 16.0f;

    for (int k = 0; k < 16; ++k) {
        const int s = page * 16 + k;
        const auto r = keyRect(k);
        const bool inside = s < n;
        bool trig = false;
        uint32_t cell = 0;
        if (grid) { cell = m_proc.gridCell(t, s); trig = gridTrig(cell); }
        else trig = euclidHit(n, pulses, rot, s) && !((mask >> s) & 1u);
        const bool mutedHit = !grid && euclidHit(n, pulses, rot, s) && ((mask >> s) & 1u);

        // the note written on the step, above the key
        if (inside && grid && trig) {
            g.setColour(s == sel ? juce::Colours::white : colour::silk);
            g.setFont(font(12.0f, true));
            g.drawText(eu::noteName(gridNote(cell)), juce::Rectangle<float>(r.getX() - 4.0f, 2.0f, r.getWidth() + 8.0f, 16.0f).toNearestInt(), juce::Justification::centred, false);
        } else if (inside && mutedHit) {
            g.setColour(colour::silkDim);
            g.setFont(font(11.0f, true));
            g.drawText("MUTE", juce::Rectangle<float>(r.getX() - 4.0f, 2.0f, r.getWidth() + 8.0f, 16.0f).toNearestInt(), juce::Justification::centred, false);
        }

        drawKeyBody(g, r, m_downStep == s, m_hover == s, !inside);
        float level = 0.0f;
        if (inside) {
            if (trig) level = grid ? 0.55f : 0.45f;
            if (s == playing) level = trig ? 1.0f : 0.25f;
        }
        drawLed(g, juce::Rectangle<float>(r.getCentreX() - 9.0f, r.getY() + 9.0f, 18.0f, 6.0f), colour::led, level);
        if (s == sel && inside) {
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.drawRoundedRectangle(r.expanded(2.5f), 6.0f, 1.6f);
        }
        // step number under the key, every fourth one brighter (beats)
        g.setColour(k % 4 == 0 ? colour::silk : colour::silkDim);
        g.setFont(font(11.0f, k % 4 == 0));
        g.drawText(juce::String(s + 1), juce::Rectangle<float>(r.getX(), float(getHeight()) - 16.0f, r.getWidth(), 14.0f).toNearestInt(), juce::Justification::centred, false);
    }

    // note lengths (note off): a bar from each trig, across the steps it holds
    if (grid) {
        const float y = float(getHeight()) - 26.0f;
        g.setColour(colour::panelLine);
        g.fillRect(0.0f, y + 1.0f, float(getWidth()), 1.0f);
        for (int s = 0; s < n; ++s) {
            const uint32_t cell = m_proc.gridCell(t, s);
            if (!gridTrig(cell)) continue;
            const float x0 = (float(s - page * 16)) * pitch + 5.0f;
            const float x1 = x0 + float(gridLength(cell)) / 4.0f * pitch - 10.0f + (gridLength(cell) > 4 ? 10.0f : 0.0f);
            const float a = juce::jmax(0.0f, x0), b = juce::jmin(float(getWidth()), x1);
            if (b <= a) continue;
            g.setColour((s == sel ? juce::Colours::white : colour::led).withAlpha(0.95f));
            g.fillRoundedRectangle(a, y - 3.0f, b - a, 6.0f, 3.0f);
            if (x1 <= float(getWidth())) g.fillRect(b - 2.0f, y - 6.0f, 2.0f, 12.0f);   // the note off
        }
    }
}

void TrigRow::mouseDown(const juce::MouseEvent& e)
{
    const int t = track();
    const int s = stepAt(e.position);
    m_downStep = -1;
    m_moved = false;
    m_dragMode = 0;
    if (s < 0 || s >= steps()) return;
    m_downStep = s;
    if (!gridMode()) {   // EUCLID: a click mutes or unmutes the step by hand
        m_proc.setStepMask(t, m_proc.stepMask(t) ^ (1u << s));
        repaint();
        return;
    }
    uint32_t cell = m_proc.gridCell(t, s);
    if (e.mods.isPopupMenu()) {   // right click: remove
        m_proc.setGridCell(t, s, cell & ~kGridTrig);
        m_downStep = -1;
        repaint();
        return;
    }
    m_wasSelected = selectedStep(t) == s;
    m_created = !gridTrig(cell);
    if (m_created) cell = gridCell(true, lastNote(), lastLength());
    m_proc.setGridCell(t, s, cell);
    m_downCell = cell;
    selectedStep(t) = s;
    m_proc.auditionNote(t, gridNote(cell), true);
    repaint();
}

void TrigRow::mouseDrag(const juce::MouseEvent& e)
{
    if (m_downStep < 0 || !gridMode()) return;
    const int t = track();
    const float dx = float(e.getDistanceFromDragStartX()), dy = float(e.getDistanceFromDragStartY());
    if (m_dragMode == 0) {
        if (std::abs(dy) > 6.0f && std::abs(dy) > std::abs(dx)) m_dragMode = 1;
        else if (std::abs(dx) > 8.0f) m_dragMode = 2;
        else return;
    }
    m_moved = true;
    uint32_t cell = m_proc.gridCell(t, m_downStep);
    if (m_dragMode == 1) {   // pitch: up = higher, one semitone per 7 pixels (Shift: an octave per 7 pixels)
        const int stepsMoved = int(std::lround(-dy / 7.0f)) * (e.mods.isShiftDown() ? 12 : 1);
        const int note = juce::jlimit(0, 127, gridNote(m_downCell) + stepsMoved);
        if (note != gridNote(cell)) {
            m_proc.auditionNote(t, gridNote(cell), false);
            m_proc.auditionNote(t, note, true);
        }
        cell = gridCell(true, note, gridLength(cell));
        lastNote() = note;
    } else {   // length: the note off follows the mouse, in quarter steps
        const float pitch = float(getWidth()) / 16.0f;
        const float startX = float(m_downStep - page * 16) * pitch;
        const int q = juce::jlimit(1, (steps() - m_downStep) * 4, int(std::lround((e.position.x - startX) / pitch * 4.0f)));
        cell = gridCell(true, gridNote(cell), q);
        lastLength() = q;
    }
    m_proc.setGridCell(t, m_downStep, cell);
    if (onHint) onHint("STEP " + juce::String(m_downStep + 1) + "   " + eu::noteName(gridNote(cell)) + "   LEN " + juce::String(gridLength(cell) / 4.0, 2) + " STEP");
    repaint();
}

void TrigRow::mouseUp(const juce::MouseEvent&)
{
    if (m_downStep >= 0 && gridMode()) {
        const int t = track();
        const uint32_t cell = m_proc.gridCell(t, m_downStep);
        m_proc.auditionNote(t, gridNote(cell), false);
        if (!m_moved && !m_created && m_wasSelected) m_proc.setGridCell(t, m_downStep, cell & ~kGridTrig);   // a click on the selected trig removes it
        else { lastNote() = gridNote(cell); lastLength() = gridLength(cell); }
    }
    m_downStep = -1;
    repaint();
}

void TrigRow::mouseMove(const juce::MouseEvent& e)
{
    const int s = stepAt(e.position);
    const int h = s >= 0 && s < steps() ? s : -1;
    if (h != m_hover) {
        m_hover = h;
        repaint();
        if (onHint)
            onHint(h < 0 ? juce::String()
                 : gridMode() ? "CLICK: AGGIUNGE / SELEZIONA   CLICK SUL SELEZIONATO O TASTO DESTRO: TOGLIE   TRASCINA SU-GIU': NOTA (SHIFT: OTTAVA)   TRASCINA A DESTRA: LUNGHEZZA"
                              : "EUCLID: CLICK SU UNO STEP PER SILENZIARLO O RIATTIVARLO A MANO. PREMI GRID PER SCRIVERE LE NOTE");
    }
    setMouseCursor(h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void TrigRow::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (!gridMode()) return;
    const int t = track();
    const int s = stepAt(e.position);
    if (s < 0 || s >= steps()) return;
    const uint32_t cell = m_proc.gridCell(t, s);
    if (!gridTrig(cell)) return;
    m_wheel += w.deltaY;
    const int d = int(m_wheel / 0.1f);
    if (d == 0) return;
    m_wheel -= float(d) * 0.1f;
    const int note = juce::jlimit(0, 127, gridNote(cell) + (w.isReversed ? -d : d));
    m_proc.setGridCell(t, s, gridCell(true, note, gridLength(cell)));
    selectedStep(t) = s;
    lastNote() = note;
    repaint();
}

// ---------------------------------------------------------------------------
// HwKeyboard: two octaves and a top C (15 white keys)

static constexpr int kWhites = 15;
static constexpr int kWhiteSemis[7] = {0, 2, 4, 5, 7, 9, 11};

juce::Rectangle<float> HwKeyboard::whiteRect(int i) const
{
    const float w = float(getWidth()) / float(kWhites);
    return {w * float(i) + 1.0f, 0.0f, w - 2.0f, float(getHeight())};
}

int HwKeyboard::noteAt(juce::Point<float> p) const
{
    const float w = float(getWidth()) / float(kWhites);
    const int base = (baseOctave + 1) * 12;
    if (p.y < float(getHeight()) * 0.6f) {   // black keys first
        for (int i = 0; i < kWhites - 1; ++i) {
            const int deg = i % 7;
            if (deg == 2 || deg == 6) continue;
            const juce::Rectangle<float> b(w * float(i + 1) - w * 0.3f, 0.0f, w * 0.6f, float(getHeight()) * 0.6f);
            if (b.contains(p)) return base + (i / 7) * 12 + kWhiteSemis[deg] + 1;
        }
    }
    const int i = juce::jlimit(0, kWhites - 1, int(p.x / w));
    return base + (i / 7) * 12 + kWhiteSemis[i % 7];
}

void HwKeyboard::paint(juce::Graphics& g)
{
    const float w = float(getWidth()) / float(kWhites);
    const int base = (baseOctave + 1) * 12;
    for (int i = 0; i < kWhites; ++i) {
        const int note = base + (i / 7) * 12 + kWhiteSemis[i % 7];
        auto r = whiteRect(i);
        const bool down = note == m_down, hi = note == highlighted;
        g.setColour(colour::keyEdge);
        g.fillRoundedRectangle(r.translated(0.0f, 2.0f), 3.0f);
        juce::ColourGradient grad(juce::Colour(0xffe9e7e0), r.getX(), r.getY(), juce::Colour(0xffc7c4bb), r.getX(), r.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(down ? r.translated(0.0f, 1.5f) : r, 3.0f);
        if (hi) { g.setColour(colour::led.withAlpha(0.85f)); g.fillRoundedRectangle(r.removeFromBottom(8.0f).reduced(4.0f, 1.0f), 2.0f); }
        if (note % 12 == 0) {
            g.setColour(juce::Colour(0xff55534d));
            g.setFont(font(10.0f, true));
            g.drawText(eu::noteName(note), whiteRect(i).removeFromBottom(24.0f).toNearestInt(), juce::Justification::centred, false);
        }
    }
    for (int i = 0; i < kWhites - 1; ++i) {
        const int deg = i % 7;
        if (deg == 2 || deg == 6) continue;
        const int note = base + (i / 7) * 12 + kWhiteSemis[deg] + 1;
        juce::Rectangle<float> b(w * float(i + 1) - w * 0.3f, 0.0f, w * 0.6f, float(getHeight()) * 0.6f);
        g.setColour(juce::Colour(0xff050506));
        g.fillRoundedRectangle(b.translated(0.0f, 1.5f), 2.5f);
        juce::ColourGradient grad(juce::Colour(0xff3a3b3e), b.getX(), b.getY(), juce::Colour(0xff111113), b.getX(), b.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(note == m_down ? b.translated(0.0f, 1.0f) : b, 2.5f);
        if (note == highlighted) { g.setColour(colour::led.withAlpha(0.9f)); g.fillRoundedRectangle(b.removeFromBottom(6.0f).reduced(3.0f, 1.0f), 2.0f); }
    }
}

void HwKeyboard::mouseDown(const juce::MouseEvent& e)
{
    m_down = noteAt(e.position);
    if (onNote) onNote(m_down, true);
    repaint();
}

void HwKeyboard::mouseDrag(const juce::MouseEvent& e)
{
    const int n = noteAt(e.position);
    if (n == m_down) return;
    if (onNote) { onNote(m_down, false); onNote(n, true); }
    m_down = n;
    repaint();
}

void HwKeyboard::mouseUp(const juce::MouseEvent&)
{
    if (onNote && m_down >= 0) onNote(m_down, false);
    m_down = -1;
    repaint();
}

} // namespace mnm::plugin::one::hw
