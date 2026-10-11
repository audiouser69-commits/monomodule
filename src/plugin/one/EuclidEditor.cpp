#include "EuclidEditor.h"
#include <cmath>
#include <iterator>

namespace mnm::plugin::one {

using namespace mnm::gen;
namespace eu = euclid;

namespace {

const spec::Font& tiny() { return spec::kFontTiny3x5; }
const spec::Font& small() { return spec::kFontSmall4x5; }
const spec::Font& bold() { return spec::kFontBold8; }

// Bresenham line in LCD pixels
void line(LcdCanvas& cv, int x0, int y0, int x1, int y1, bool on = true)
{
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 4096; ++guard) {
        cv.set(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void outline(LcdCanvas& cv, int x, int y, int w, int h, bool on = true)
{
    for (int i = 0; i < w; ++i) { cv.set(x + i, y, on); cv.set(x + i, y + h - 1, on); }
    for (int i = 0; i < h; ++i) { cv.set(x, y + i, on); cv.set(x + w - 1, y + i, on); }
}
// text cut to fit maxW LCD px
juce::String fit(const spec::Font& f, juce::String s, int maxW)
{
    while (s.isNotEmpty() && LcdCanvas::textWidth(f, s.toRawUTF8()) > maxW) s = s.dropLastCharacters(1);
    return s;
}
int toLcd(int screenPx) { return screenPx / kScale; }

juce::String helpOf(juce::Component* c)
{
    for (; c != nullptr; c = c->getParentComponent()) {
        if (auto* sc = dynamic_cast<SeqControl*>(c)) return sc->helpFn ? sc->helpFn() : juce::String();
        if (c->getProperties().contains("help")) return c->getProperties()["help"].toString();
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// SeqControl

void SeqControl::bind(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId)
{
    m_attach.reset();
    m_param = apvts.getParameter(paramId);
    jassert(m_param != nullptr);
    if (m_param) {
        m_attach = std::make_unique<juce::ParameterAttachment>(*m_param, [this](float) { repaint(); }, nullptr);
        m_attach->sendInitialUpdate();
    }
    repaint();
}

juce::String SeqControl::valueText() const
{
    if (!m_param) return {};
    return (textFn ? textFn(intValue()) : m_param->getCurrentValueAsText()).toUpperCase();
}

void SeqControl::setIntValue(int v)
{
    if (!m_param || !m_attach) return;
    const auto& r = m_param->getNormalisableRange();
    m_attach->setValueAsCompleteGesture(juce::jlimit(r.start, r.end, float(v)));
}

bool SeqControl::clickOpensMenu() const
{
    if (!m_param || dynamic_cast<juce::AudioParameterBool*>(m_param)) return false;
    if (dynamic_cast<juce::AudioParameterChoice*>(m_param)) return true;
    const auto& r = m_param->getNormalisableRange();
    return r.end - r.start <= 12.0f;
}

void SeqControl::showMenu()
{
    if (!m_param) return;
    const auto& r = m_param->getNormalisableRange();
    const int lo = int(r.start), hi = int(r.end), cur = intValue();
    juce::PopupMenu m;
    for (int v = lo; v <= hi; ++v)
        m.addItem(v - lo + 1, textFn ? textFn(v) : m_param->getText(m_param->convertTo0to1(float(v)), 32), true, v == cur);
    juce::Component::SafePointer<SeqControl> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMaximumNumColumns(hi - lo > 16 ? 2 : 1),
                    [safe, lo](int id) { if (safe && id > 0) safe->setIntValue(lo + id - 1); });
}

void SeqControl::mouseDown(const juce::MouseEvent&)
{
    if (!m_param || !m_attach) return;
    m_dragStart = float(intValue());
    m_dragged = false;
    m_attach->beginGesture();
}

void SeqControl::mouseDrag(const juce::MouseEvent& e)
{
    if (!m_param || !m_attach) return;
    const auto& r = m_param->getNormalisableRange();
    const float span = juce::jmax(1.0f, r.end - r.start);
    float pxPerStep = juce::jlimit(2.0f, 14.0f, 260.0f / span);
    if (e.mods.isShiftDown()) pxPerStep *= 4.0f;   // fine
    const float d = float(e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());
    if (std::abs(d) > 3.0f) m_dragged = true;
    if (!m_dragged) return;
    m_attach->setValueAsPartOfGesture(juce::jlimit(r.start, r.end, std::round(m_dragStart + d / pxPerStep)));
}

void SeqControl::mouseUp(const juce::MouseEvent&)
{
    if (!m_param || !m_attach) return;
    m_attach->endGesture();
    if (m_dragged) return;
    if (dynamic_cast<juce::AudioParameterBool*>(m_param)) setIntValue(intValue() ? 0 : 1);
    else if (clickOpensMenu()) showMenu();
}

void SeqControl::mouseDoubleClick(const juce::MouseEvent&)
{
    if (m_param && !dynamic_cast<juce::AudioParameterBool*>(m_param) && !clickOpensMenu())
        setIntValue(int(std::lround(m_param->convertFrom0to1(m_param->getDefaultValue()))));
}

void SeqControl::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (!m_param) return;
    m_wheelAcc += w.deltaY;
    const int steps = int(m_wheelAcc / 0.1f);
    if (steps == 0) return;
    m_wheelAcc -= float(steps) * 0.1f;
    setIntValue(intValue() + (w.isReversed ? -steps : steps));
}

// ---------------------------------------------------------------------------
// SeqKnob / SeqField / SeqKnobPage

void SeqKnob::paint(juce::Graphics& g)
{
    LcdCanvas cv(kCell, kCell);
    cv.dotsH(0, kCell - 1, 0);
    cv.dotsV(0, 0, kCell - 1);
    if (!m_param) { cv.draw(g, 0, 0); return; }
    const bool dim = dimFn && dimFn();
    cv.textCentred(tiny(), label.toRawUTF8(), 1, kCell - 1, kLabelY);
    const auto box = knobValueBox(0, 0);
    if (dim) {
        cv.textCentred(tiny(), "--", box.getX(), box.getWidth(), box.getY() + 2);
    } else {
        const int rx = 1 + (kCell - 1 - spec::kDialRing.w) / 2, ry = kContentY + (kContentH - spec::kDialRing.h) / 2;
        cv.blit(spec::kDialRing, rx, ry);
        cv.blit(*spec::kDialDot[juce::jlimit(0, 127, int(std::lround(m_param->getValue() * 127.0f)))], rx + kDotX, ry + kDotY);
        cv.textCentred(tiny(), fit(tiny(), valueText(), box.getWidth() - 2).toRawUTF8(), box.getX(), box.getWidth(), box.getY() + 2);
        if (isMouseOverOrDragging()) cv.invertRect(box.getX(), box.getY(), box.getWidth(), box.getHeight());
    }
    cv.draw(g, 0, 0);
}

void SeqField::paint(juce::Graphics& g)
{
    const int w = toLcd(getWidth()), h = toLcd(getHeight());
    LcdCanvas cv(w, h);
    if (inverted) cv.fillRect(0, 0, w, h);
    const juce::String text = (label.isNotEmpty() ? label + " " : juce::String()) + valueText();
    const int y = (h - tiny().h) / 2;
    cv.text(tiny(), fit(tiny(), text, w - 2).toRawUTF8(), 1, y, !inverted);
    if (isMouseOverOrDragging()) cv.dotsH(0, w - 1, h - 1);
    cv.draw(g, 0, 0);
}

SeqKnobPage::SeqKnobPage(const char* t) : title(t)
{
    for (auto& k : knobs) addChildComponent(k);
}

void SeqKnobPage::paint(juce::Graphics& g)
{
    LcdCanvas cv(kLcdW, kLcdH);
    cv.fillRect(0, 0, kLcdW, 10);
    cv.text(bold(), title.toRawUTF8(), 3, 1, false);
    cv.dotsH(0, kLcdW - 1, kLcdH - 1);
    cv.dotsV(kLcdW - 1, 11, kLcdH - 1);
    for (int k = 0; k < 8; ++k)   // empty cells keep their grid
        if (!knobs[size_t(k)].isVisible()) { cv.dotsH((k % 4) * kCell, (k % 4 + 1) * kCell, 11 + (k / 4) * kCell); cv.dotsV((k % 4) * kCell, 11 + (k / 4) * kCell, 11 + (k / 4 + 1) * kCell - 1); }
    cv.draw(g, 0, 0);
}

void SeqKnobPage::resized()
{
    for (int k = 0; k < 8; ++k) knobs[size_t(k)].setBounds((k % 4) * kCell * kScale, (11 + (k / 4) * kCell) * kScale, kCell * kScale, kCell * kScale);
}

// ---------------------------------------------------------------------------
// RangeBar

void RangeBar::bind(juce::AudioProcessorValueTreeState& apvts, const juce::String& minId, const juce::String& maxId)
{
    m_min = apvts.getParameter(minId);
    m_max = apvts.getParameter(maxId);
    m_minAttach = std::make_unique<juce::ParameterAttachment>(*m_min, [this](float) { repaint(); }, nullptr);
    m_maxAttach = std::make_unique<juce::ParameterAttachment>(*m_max, [this](float) { repaint(); }, nullptr);
    m_minAttach->sendInitialUpdate(); m_maxAttach->sendInitialUpdate();
    repaint();
}

int RangeBar::valueAt(float x) const
{
    const float w = float(getWidth() - kScale);
    return juce::jlimit(0, 127, int(std::lround((x - kScale * 0.5f) / juce::jmax(1.0f, w) * 127.0f)));
}

void RangeBar::paint(juce::Graphics& g)
{
    const int w = toLcd(getWidth()), h = toLcd(getHeight());
    LcdCanvas cv(w, h);
    if (!m_min || !m_max) { cv.draw(g, 0, 0); return; }
    auto xOf = [w](int v) { return int(std::lround(v / 127.0 * (w - 1))); };
    const int a = int(std::lround(m_min->convertFrom0to1(m_min->getValue()))), b = int(std::lround(m_max->convertFrom0to1(m_max->getValue())));
    const int x0 = xOf(std::min(a, b)), x1 = xOf(std::max(a, b));
    cv.dotsH(0, w - 1, h / 2);
    cv.fillRect(x0, h / 2 - 1, x1 - x0 + 1, 3);
    for (int y = 0; y < h; ++y) { cv.set(xOf(a), y); cv.set(xOf(b), y); }
    if (a > b) { cv.set(xOf(a) - 1, 1); cv.set(xOf(a) - 1, h - 2); }   // inverted window: a notch on MIN
    if (liveValue) if (const int v = liveValue(); v >= 0) cv.invertRect(juce::jlimit(0, w - 1, xOf(v)), 0, 1, h);
    cv.draw(g, 0, 0);
}

void RangeBar::mouseDown(const juce::MouseEvent& e)
{
    if (!m_min || !m_max) return;
    const int v = valueAt(e.position.x);
    const int a = int(std::lround(m_min->convertFrom0to1(m_min->getValue()))), b = int(std::lround(m_max->convertFrom0to1(m_max->getValue())));
    m_dragging = std::abs(v - a) <= std::abs(v - b) ? m_minAttach.get() : m_maxAttach.get();
    m_dragging->beginGesture();
    m_dragging->setValueAsPartOfGesture(float(v));
}

void RangeBar::mouseDrag(const juce::MouseEvent& e) { if (m_dragging) m_dragging->setValueAsPartOfGesture(float(valueAt(e.position.x))); }
void RangeBar::mouseUp(const juce::MouseEvent&) { if (m_dragging) m_dragging->endGesture(); m_dragging = nullptr; }
void RangeBar::mouseDoubleClick(const juce::MouseEvent&)
{
    if (m_minAttach) m_minAttach->setValueAsCompleteGesture(0.0f);
    if (m_maxAttach) m_maxAttach->setValueAsCompleteGesture(127.0f);
}

// ---------------------------------------------------------------------------
// StepGrid

int StepGrid::param(int t, const char* name) const
{
    if (auto* p = m_proc.apvts.getRawParameterValue(eu::id(t, name))) return int(std::lround(p->load()));
    return 0;
}

void StepGrid::tick()
{
    auto* gen = m_proc.generator();
    if (!gen) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (int t = 0; t < kTracks; ++t) {
        const int n = gen->ui[size_t(t)].trigs.load();
        if (n != m_lastTrigs[size_t(t)]) { m_lastTrigs[size_t(t)] = n; m_flashUntil[size_t(t)] = now + 90.0; }
    }
    repaint();
}

void StepGrid::paint(juce::Graphics& g)
{
    auto* gen = m_proc.generator();
    LcdCanvas cv(kLcdW, kTracks * kRowH);
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (int t = 0; t < kTracks; ++t) {
        const int y0 = t * kRowH;
        const bool sel = t == selected, on = param(t, "on") != 0;
        const int slot = juce::jlimit(0, spec::kNumMachines - 1, int(std::lround(m_proc.apvts.getRawParameterValue(machineId(t))->load())));
        // key: number, machine, the ON box
        if (sel) cv.fillRect(0, y0 + 1, kKeyW, kRowH - 1);
        else { cv.dotsH(0, kKeyW - 1, y0 + 1); cv.dotsH(0, kKeyW - 1, y0 + kRowH - 1); cv.dotsV(0, y0 + 1, y0 + kRowH - 1); }
        cv.text(bold(), juce::String(t + 1).toRawUTF8(), 3, y0 + 3, !sel);
        cv.text(tiny(), fit(tiny(), spec::kMachines[slot].name, 20).toRawUTF8(), 11, y0 + 2, !sel);
        cv.text(tiny(), fit(tiny(), spec::kMachines[slot].group, 20).toRawUTF8(), 11, y0 + 8, !sel);
        if (on) cv.fillRect(kKeyW - 8, y0 + 4, 6, 6, !sel);
        else outline(cv, kKeyW - 8, y0 + 4, 6, 6, !sel);

        // strip
        const int steps = juce::jlimit(1, kMaxSteps, param(t, "steps")), pulses = param(t, "pulses"), rot = param(t, "rotate"), acc = param(t, "accents");
        const uint32_t mask = m_proc.stepMask(t);
        const int playing = gen && on ? gen->ui[size_t(t)].step.load() : -1;
        const bool flash = now < m_flashUntil[size_t(t)];
        for (int i = 0; i < steps; ++i) {
            const int cx = kStripX + i * kCellW;
            const bool hit = euclidHit(steps, pulses, rot, i), accent = hit && acc > 0 && euclidHit(steps, acc, rot, i);
            const bool muted = (mask >> i) & 1u;
            if (hit && muted) { outline(cv, cx, y0 + 4, 6, 6); line(cv, cx, y0 + 9, cx + 5, y0 + 4); }
            else if (accent) cv.fillRect(cx, y0 + 2, 6, 8);
            else if (hit) cv.fillRect(cx, y0 + 4, 6, 6);
            else if (muted) { cv.set(cx + 1, y0 + 6); cv.set(cx + 4, y0 + 6); }
            else cv.fillRect(cx + 2, y0 + 6, 2, 2, on);
            if (!on && (hit || accent)) for (int yy = y0 + 2; yy < y0 + 10; ++yy) for (int xx = cx; xx < cx + 6; ++xx) if (((xx + yy) & 1) == 0) cv.set(xx, yy, false);   // hatched: track off
            if (i % 4 == 0) cv.set(cx, y0 + 12);
            if (i == playing) {
                cv.fillRect(cx, y0 + 11, 6, 2);
                if (flash && hit && !muted) cv.invertRect(cx - 1, y0 + 1, 8, 10);
            }
            if (m_hover.x == t && m_hover.y == i) outline(cv, cx - 1, y0 + 1, 8, 11);
        }
        // readout: rate, last note, ratchets
        cv.text(tiny(), kRateNames[juce::jlimit(0, kNumRates - 1, param(t, "rate"))], kInfoX, y0 + 2);
        if (gen && on) {
            const int note = gen->ui[size_t(t)].note.load();
            if (note >= 0) cv.text(tiny(), eu::noteName(note).toUpperCase().toRawUTF8(), kInfoX, y0 + 8);
            const int r = gen->ui[size_t(t)].ratchets.load();
            if (r > 1 && flash) cv.text(tiny(), ("X" + juce::String(r)).toRawUTF8(), kInfoX + 22, y0 + 8);
        }
    }
    cv.draw(g, 0, 0);
}

void StepGrid::mouseDown(const juce::MouseEvent& e)
{
    const int x = toLcd(int(e.position.x)), y = toLcd(int(e.position.y));
    const int t = juce::jlimit(0, kTracks - 1, y / kRowH);
    if (x < kKeyW) {
        if (x >= kKeyW - 10) {   // the ON box
            if (auto* p = m_proc.apvts.getParameter(eu::id(t, "on"))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->getValue() >= 0.5f ? 0.0f : 1.0f); p->endChangeGesture(); }
        }
        if (onSelect) onSelect(t);
        return;
    }
    const int i = (x - kStripX) / kCellW, steps = param(t, "steps");
    if (x >= kStripX && i >= 0 && i < steps) {
        m_proc.setStepMask(t, m_proc.stepMask(t) ^ (1u << i));
        if (onSelect && t != selected) onSelect(t);
        repaint();
    } else if (onSelect) onSelect(t);
}

void StepGrid::mouseMove(const juce::MouseEvent& e)
{
    const int x = toLcd(int(e.position.x)), y = toLcd(int(e.position.y));
    const int t = juce::jlimit(0, kTracks - 1, y / kRowH), i = (x - kStripX) / kCellW;
    const juce::Point<int> h = x >= kStripX && i < param(t, "steps") ? juce::Point<int>(t, i) : juce::Point<int>(-1, -1);
    setMouseCursor(x < kKeyW || h.x >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (h != m_hover) { m_hover = h; repaint(); }
}

juce::String StepGrid::helpAt(juce::Point<int> local) const
{
    const int x = toLcd(local.x), y = toLcd(local.y);
    const int t = juce::jlimit(0, kTracks - 1, y / kRowH);
    if (x < kKeyW)
        return x >= kKeyW - 10 ? "TRACCIA " + juce::String(t + 1) + ": IL QUADRATINO ACCENDE/SPEGNE IL SEQUENCER DI QUESTA TRACCIA"
                               : "TRACCIA " + juce::String(t + 1) + ": CLICK PER SELEZIONARLA (ANCHE NELLA PAGINA SYNTH)";
    const int i = (x - kStripX) / kCellW;
    if (x >= kStripX && i < param(t, "steps"))
        return "STEP " + juce::String(i + 1) + ": CLICK PER SILENZIARE/RIATTIVARE A MANO. PIENO = COLPO, ALTO = ACCENTO, BARRATO = MUTO";
    if (x >= kInfoX) return "RATE DELLA TRACCIA, ULTIMA NOTA SUONATA E RATCHET (X2..X8)";
    return {};
}

// ---------------------------------------------------------------------------
// Necklace

void Necklace::paint(juce::Graphics& g)
{
    const int w = toLcd(getWidth()), h = toLcd(getHeight());
    LcdCanvas cv(w, h);
    const auto P = [&](const char* n) { return int(std::lround(m_proc.apvts.getRawParameterValue(eu::id(track, n))->load())); };
    const int steps = juce::jlimit(1, kMaxSteps, P("steps")), pulses = P("pulses"), rot = P("rotate"), acc = P("accents");
    const bool on = P("on") != 0;
    const uint32_t mask = m_proc.stepMask(track);
    const int cx = w / 2, cy = h / 2, r = juce::jmin(w, h) / 2 - 4;
    auto pt = [&](int i) {
        const double a = -juce::MathConstants<double>::halfPi + juce::MathConstants<double>::twoPi * i / steps;
        return juce::Point<int>(cx + int(std::lround(r * std::cos(a))), cy + int(std::lround(r * std::sin(a))));
    };
    // the Euclidean polygon through the hits
    int first = -1, prev = -1;
    for (int i = 0; i < steps; ++i)
        if (euclidHit(steps, pulses, rot, i) && !((mask >> i) & 1u)) {
            if (prev >= 0) line(cv, pt(prev).x, pt(prev).y, pt(i).x, pt(i).y);
            else first = i;
            prev = i;
        }
    if (first >= 0 && prev != first) line(cv, pt(prev).x, pt(prev).y, pt(first).x, pt(first).y);
    const int playing = m_proc.generator() && on ? m_proc.generator()->ui[size_t(track)].step.load() : -1;
    for (int i = 0; i < steps; ++i) {
        const auto p = pt(i);
        const bool hit = euclidHit(steps, pulses, rot, i), accent = hit && acc > 0 && euclidHit(steps, acc, rot, i), muted = (mask >> i) & 1u;
        if (hit && muted) outline(cv, p.x - 1, p.y - 1, 3, 3);
        else if (accent) cv.fillRect(p.x - 2, p.y - 2, 5, 5);
        else if (hit) cv.fillRect(p.x - 1, p.y - 1, 3, 3);
        else cv.set(p.x, p.y);
        if (i == playing) outline(cv, p.x - 3, p.y - 3, 7, 7);
    }
    const juce::String a = juce::String(juce::jmin(pulses, steps)) + "/" + juce::String(steps);
    cv.textCentred(small(), a.toRawUTF8(), 0, w, cy - 3);
    cv.draw(g, 0, 0);
}

// ---------------------------------------------------------------------------
// LanePanel

LanePanel::LanePanel(MnmOneProcessor& p) : m_proc(p)
{
    for (int l = 0; l < kLanes; ++l) {
        auto& tg = m_target[size_t(l)];
        tg.label = "L" + juce::String(l + 1);
        tg.textFn = [this](int v) { return targetName(v - 1); };
        tg.helpFn = [l] { return "LANE " + juce::String(l + 1) + ": QUALE MANOPOLA DELLA MONOMACHINE BLOCCARE (P-LOCK) A OGNI TRIG. CLICK PER SCEGLIERE"; };
        m_mode[size_t(l)].helpFn = [] { return juce::String("FORMA: RANDOM, CYCLE (SEQUENZA CHE SI RIPETE OGNI LEN), RAMPE, TRIANGLE, SINE (SU LEN STEP), ACCENT, PITCH (SEGUE LA NOTA), TURING"); };
        m_len[size_t(l)].label = "LEN";
        m_len[size_t(l)].helpFn = [] { return juce::String("LEN: PERIODO DELLA FORMA IN TRIG (SINE: IN STEP)"); };
        m_chance[size_t(l)].label = "CH";
        m_chance[size_t(l)].textFn = [](int v) { return juce::String(v); };
        m_chance[size_t(l)].helpFn = [] { return juce::String("CH: PROBABILITA' (%) CHE IL TRIG RICEVA IL P-LOCK; ALTRIMENTI LA MANOPOLA RESTA AL SUO VALORE"); };
        m_range[size_t(l)].getProperties().set("help", "MIN-MAX: LA FINESTRA DI VALORI (0-127) IN CUI SI MUOVE LA MANOPOLA. TRASCINA LE MANIGLIE; DOPPIO CLICK = TUTTO");
        m_range[size_t(l)].liveValue = [this, l] { return m_proc.generator() ? m_proc.generator()->ui[size_t(m_track)].lock[l].load() : -1; };
        for (juce::Component* c : {static_cast<juce::Component*>(&tg), static_cast<juce::Component*>(&m_mode[size_t(l)]), static_cast<juce::Component*>(&m_len[size_t(l)]),
                                   static_cast<juce::Component*>(&m_chance[size_t(l)]), static_cast<juce::Component*>(&m_range[size_t(l)])})
            addAndMakeVisible(c);
    }
    setTrack(0);
}

juce::String LanePanel::targetName(int target) const
{
    if (target < 0) return "OFF";
    if (target < 8) {
        const int slot = juce::jlimit(0, spec::kNumMachines - 1, int(std::lround(m_proc.apvts.getRawParameterValue(machineId(m_track))->load())));
        const auto& p = spec::kMachines[slot].params[target];
        if (p.display != spec::Display::Blank && juce::String(p.label) != "-") return "SYN " + juce::String(p.label);
    }
    return eu::lockTargetName(target);
}

void LanePanel::setTrack(int t)
{
    m_track = t;
    auto& s = m_proc.apvts;
    for (int l = 0; l < kLanes; ++l) {
        m_target[size_t(l)].bind(s, eu::laneId(t, l, "tgt"));
        m_mode[size_t(l)].bind(s, eu::laneId(t, l, "mode"));
        m_len[size_t(l)].bind(s, eu::laneId(t, l, "len"));
        m_chance[size_t(l)].bind(s, eu::laneId(t, l, "chance"));
        m_range[size_t(l)].bind(s, eu::laneId(t, l, "min"), eu::laneId(t, l, "max"));
    }
    repaint();
}

void LanePanel::paint(juce::Graphics& g)
{
    LcdCanvas cv(kLcdW, kLcdH);
    cv.fillRect(0, 0, kLcdW, 10);
    cv.text(bold(), ("LANES T" + juce::String(m_track + 1)).toRawUTF8(), 3, 1, false);
    for (int l = 1; l < kLanes; ++l) cv.dotsH(0, kLcdW - 1, 11 + l * kRowH - 1);
    cv.dotsH(0, kLcdW - 1, kLcdH - 1);
    cv.dotsV(0, 11, kLcdH - 1);
    cv.dotsV(kLcdW - 1, 11, kLcdH - 1);
    cv.draw(g, 0, 0);
}

void LanePanel::resized()
{
    for (int l = 0; l < kLanes; ++l) {
        const int y = 11 + l * kRowH;
        auto R = [](int x, int yy, int w, int h) { return juce::Rectangle<int>(x * kScale, yy * kScale, w * kScale, h * kScale); };
        m_target[size_t(l)].setBounds(R(2, y + 1, 76, 8));
        m_mode[size_t(l)].setBounds(R(80, y + 1, 58, 8));
        m_range[size_t(l)].setBounds(R(3, y + 10, 76, 6));
        m_len[size_t(l)].setBounds(R(82, y + 9, 28, 8));
        m_chance[size_t(l)].setBounds(R(111, y + 9, 29, 8));
    }
}

// ---------------------------------------------------------------------------
// SeqTabPage

SeqTabPage::SeqTabPage(MnmOneProcessor& p) : m_proc(p), m_grid(p), m_lanes(p), m_necklace(p)
{
    addAndMakeVisible(m_grid);
    addAndMakeVisible(m_lanes);
    for (auto* pg : {&m_rhythm, &m_motion, &m_pitch}) addAndMakeVisible(pg);
    addAndMakeVisible(m_necklace);
    addAndMakeVisible(m_shake);
    addAndMakeVisible(m_clear);
    m_grid.onSelect = [this](int t) { setTrack(t); if (onTrackSelected) onTrackSelected(t); };
    m_shake.onClick = [this] { shake(); };
    m_clear.onClick = [this] { clearMutes(); };
    m_shake.getProperties().set("help", "SHAKE: UN NUOVO RITMO EUCLIDEO A CASO (MA MUSICALE) PER LA TRACCIA SELEZIONATA");
    m_clear.getProperties().set("help", "CLEAR: TOGLIE I MUTE MESSI A MANO SUGLI STEP DELLA TRACCIA SELEZIONATA");
    m_necklace.getProperties().set("help", "LA COLLANA: GLI STEP IN CERCHIO, I COLPI UNITI NEL POLIGONO EUCLIDEO; IL QUADRATO E' LA POSIZIONE CHE SUONA");

    struct K { const char* id; const char* label; const char* help; };
    const K rhythm[8] = {
        {"steps", "STEP", "STEPS: LUNGHEZZA DEL PATTERN (1-32). TRACCE CON LUNGHEZZE DIVERSE SI SFASANO (POLIMETRIA)"},
        {"pulses", "PULS", "PULSES: QUANTI COLPI DISTRIBUITI IL PIU' UNIFORMEMENTE POSSIBILE SUGLI STEP (EUCLIDE)"},
        {"rotate", "ROT", "ROTATE: SPOSTA IL PATTERN IN AVANTI DI N STEP"},
        {"rate", "RATE", "RATE: DURATA DI UNO STEP. 1/20, 1/28, 1/14, 1/10 = GIRI DISPARI CHE SCIVOLANO CONTRO LA BATTUTA"},
        {"accents", "ACNT", "ACCENTS: UN SECONDO RITMO EUCLIDEO SUGLI STESSI STEP; LE LANE IN MODO ACCENT LO SEGUONO"},
        {"prob", "CHNC", "CHANCE: PROBABILITA' CHE OGNI COLPO SUONI DAVVERO"},
        {"cond", "COND", "CONDITION (COME ELEKTRON): 1:2 = PRIMO GIRO DI DUE... PRE = SE IL COLPO PRECEDENTE HA SUONATO, NEI = SE LA TRACCIA PRIMA HA SUONATO, 1ST = SOLO AL PRIMO GIRO"},
        {"gate", "GATE", "GATE: DURATA DELLA NOTA IN % DELLO STEP (O DELLA SUA PARTE DI RATCHET)"}};
    const K motion[8] = {
        {"rchance", "RTCH", "RATCHET: PROBABILITA' CHE UN COLPO SI RIPETA DENTRO LO STEP (ROLL)"},
        {"rmax", "RMAX", "RATCHET MAX: FINO A QUANTE RIPETIZIONI (X2..X8); OFF = NIENTE RATCHET"},
        {"rshape", "SHAP", "SHAPE: FLAT = REGOLARE, ACCEL = SEMPRE PIU' FITTO, DECEL = PALLINA CHE RIMBALZA, RANDOM"},
        {"rpitch", "RPCH", "RATCHET PITCH: GRADI DI SCALA AGGIUNTI A OGNI RIPETIZIONE (ARPEGGI GLITCH)"},
        {"swing", "SWNG", "SWING: RITARDA UNO STEP SU DUE (FINO A MEZZO STEP)"},
        {"drift", "DRFT", "DRIFT: RITARDO CASUALE DI OGNI COLPO, TIMING 'UMANO' O SBILENCO ALLA AUTECHRE"},
        {nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr}};
    const K pitch[8] = {
        {"pmode", "MODE", "MODE: FIXED = SEMPRE LA NOTA LOW (BATTERIA), RANDOM, WALK = PASSI VICINI, TURING = REGISTRO CHE MUTA, CYCLE = MELODIA DI LEN NOTE"},
        {"scale", "SCAL", "SCALE: SCALA DI QUESTA TRACCIA, O GLOBAL = QUELLA NELLA BARRA IN ALTO. CHROMATIC PER I KIT DI BATTERIA"},
        {"low", "LOW", "LOW: NOTA PIU' BASSA DEL RANGE (CON FIXED: L'UNICA NOTA)"},
        {"high", "HIGH", "HIGH: NOTA PIU' ALTA DEL RANGE. LE NOTE STANNO SEMPRE NEL RANGE E NELLA SCALA"},
        {"len", "LEN", "LEN: TURING/CYCLE = LUNGHEZZA DELLA FRASE (IN TRIG); WALK = SALTO MASSIMO IN GRADI"},
        {"mutate", "MUT", "MUTATE: TURING = PROBABILITA' DI CAMBIARE UN BIT; CYCLE = DI SOSTITUIRE UNA NOTA"},
        {"hold", "HOLD", "HOLD: PROBABILITA' DI RIPETERE LA NOTA PRECEDENTE (FRASI PIU' STABILI)"},
        {nullptr, nullptr, nullptr}};
    auto setup = [](SeqKnobPage& page, const K* ks) {
        for (int k = 0; k < 8; ++k) {
            auto& kn = page.knobs[size_t(k)];
            if (!ks[k].id) continue;
            kn.label = ks[k].label;
            kn.getProperties().set("pid", ks[k].id);
            const juce::String help = ks[k].help;
            kn.helpFn = [help, &kn] { return help + "  [" + kn.valueText() + "]"; };
            kn.setVisible(true);
        }
    };
    setup(m_rhythm, rhythm);
    setup(m_motion, motion);
    setup(m_pitch, pitch);
    m_motion.knobs[0].textFn = [](int v) { return juce::String(v) + "%"; };
    // PITCH: the knobs a mode does not use are dimmed
    auto mode = [this] { return PitchMode(juce::jlimit(0, int(PitchMode::Count) - 1, int(std::lround(m_proc.apvts.getRawParameterValue(eu::id(m_track, "pmode"))->load())))); };
    m_pitch.knobs[1].dimFn = [mode] { return mode() == PitchMode::Fixed; };
    m_pitch.knobs[3].dimFn = [mode] { return mode() == PitchMode::Fixed; };
    m_pitch.knobs[4].dimFn = [mode] { const auto m = mode(); return m == PitchMode::Fixed || m == PitchMode::Random; };
    m_pitch.knobs[5].dimFn = [mode] { const auto m = mode(); return m != PitchMode::Turing && m != PitchMode::Cycle; };
    m_pitch.knobs[6].dimFn = [mode] { return mode() == PitchMode::Fixed; };
    m_pitch.knobs[2].textFn = [](int v) { return eu::noteName(v); };
    m_pitch.knobs[3].textFn = [](int v) { return eu::noteName(v); };

    addMouseListener(this, true);
    setTrack(0);
}

void SeqTabPage::setTrack(int t)
{
    m_track = juce::jlimit(0, kTracks - 1, t);
    for (auto* pg : {&m_rhythm, &m_motion, &m_pitch})
        for (auto& k : pg->knobs)
            if (k.getProperties().contains("pid")) k.bind(m_proc.apvts, eu::id(m_track, k.getProperties()["pid"].toString()));
    m_grid.selected = m_track;
    m_necklace.track = m_track;
    m_lanes.setTrack(m_track);
    repaint();
}

void SeqTabPage::tick()
{
    if (m_help.isEmpty()) repaint(m_helpArea);
    m_grid.tick();
    m_necklace.repaint();
    m_lanes.tick();
    // PITCH: LEN reads JUMP in WALK mode
    const int pm = int(std::lround(m_proc.apvts.getRawParameterValue(eu::id(m_track, "pmode"))->load()));
    const juce::String len = pm == int(PitchMode::Walk) ? "JUMP" : "LEN";
    if (m_pitch.knobs[4].label != len) { m_pitch.knobs[4].label = len; }
    for (auto& k : m_pitch.knobs) k.repaint();
}

void SeqTabPage::paint(juce::Graphics& g)
{
    g.fillAll(lcd::paper);
    LcdCanvas cv(kLcdW, 12);
    cv.dotsH(0, kLcdW - 1, 0);
    juce::String help = m_help;
    if (help.isEmpty())
        help = m_proc.engineReady() ? juce::String("PASSA IL MOUSE SU UN CONTROLLO PER LA SPIEGAZIONE. TRASCINA = CAMBIA, DOPPIO CLICK = DEFAULT, SHIFT = FINE")
                                    : juce::String("MANCA IL FILE OS DELLA MONOMACHINE: APRI LA PAGINA SYNTH PER SELEZIONARLO (IL SEQUENCER PARTE DOPO)");
    cv.text(small(), fit(small(), help, kLcdW - 4).toRawUTF8(), 2, 4);
    cv.draw(g, m_helpArea.getX(), m_helpArea.getY());
}

void SeqTabPage::resized()
{
    auto R = [](int x, int y, int w, int h) { return juce::Rectangle<int>(x * kScale, y * kScale, w * kScale, h * kScale); };
    m_grid.setBounds(R(4, 4, StepGrid::kLcdW, kTracks * StepGrid::kRowH));
    m_lanes.setBounds(R(4 + StepGrid::kLcdW + 3, 4, LanePanel::kLcdW, LanePanel::kLcdH));
    const int y = 4 + kTracks * StepGrid::kRowH + 6;
    m_rhythm.setBounds(R(4, y, SeqKnobPage::kLcdW, SeqKnobPage::kLcdH));
    m_motion.setBounds(R(4 + 133, y, SeqKnobPage::kLcdW, SeqKnobPage::kLcdH));
    m_pitch.setBounds(R(4 + 266, y, SeqKnobPage::kLcdW, SeqKnobPage::kLcdH));
    const int nx = 4 + 266 + SeqKnobPage::kLcdW + 4;
    const int nw = kLcdW - nx - 2;
    m_necklace.setBounds(R(nx, y, nw, 50));
    m_shake.setBounds(R(nx, y + 52, nw, 11));
    m_clear.setBounds(R(nx, y + 65, nw, 11));
    m_helpArea = R(0, y + SeqKnobPage::kLcdH + 4, kLcdW, 12);
}

void SeqTabPage::mouseMove(const juce::MouseEvent& e)
{
    juce::Component* c = e.eventComponent;
    if (c == &m_grid) { setHelp(m_grid.helpAt(e.getEventRelativeTo(&m_grid).getPosition())); return; }
    setHelp(helpOf(c == this ? nullptr : c));
}

void SeqTabPage::shake()
{
    auto& r = juce::Random::getSystemRandom();
    static const int stepChoices[] = {5, 7, 8, 9, 11, 12, 13, 15, 16, 16, 16, 19, 24, 32};
    static const int rateChoices[] = {4, 4, 4, 3, 6, 2, 8, 5, 1};
    const int steps = stepChoices[r.nextInt(int(std::size(stepChoices)))];
    const int pulses = juce::jmax(1, steps / 5 + r.nextInt(juce::jmax(1, steps * 3 / 5 - steps / 5 + 1)));
    auto set = [this](const char* name, int v) {
        if (auto* p = m_proc.apvts.getParameter(eu::id(m_track, name))) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(float(v))); p->endChangeGesture();
        }
    };
    set("steps", steps);
    set("pulses", pulses);
    set("rotate", r.nextInt(steps));
    set("accents", r.nextInt(juce::jmax(1, pulses / 2 + 1)));
    set("rate", rateChoices[r.nextInt(int(std::size(rateChoices)))]);
    set("prob", 70 + r.nextInt(31));
    m_proc.setStepMask(m_track, 0);
}

void SeqTabPage::clearMutes() { m_proc.setStepMask(m_track, 0); m_grid.repaint(); m_necklace.repaint(); }

// ---------------------------------------------------------------------------
// EuclidView

EuclidView::EuclidView(MnmOneProcessor& p) : m_proc(p), m_synth(p), m_seq(p)
{
    setLookAndFeel(&m_lnf);
    addChildComponent(m_synth);
    addAndMakeVisible(m_seq);
    auto& s = m_proc.apvts;
    struct F { SeqField* f; const char* id; const char* label; const char* help; };
    for (const F& f : {F{&m_seed, "seed", "SEED", "SEED: OGNI NUMERO E' UN'ALTRA TESSITURA. STESSO SEED + STESSA POSIZIONE = STESSO RISULTATO (IL BOUNCE SUONA COME IL PLAY)"},
                       F{&m_loop, "loop", "LOOP", "LOOP: OFF = SEMPRE NUOVO; N BARS = LA TESSITURA SI RIPETE OGNI N BATTUTE"},
                       F{&m_evolve, "evolve", "EVOLVE", "EVOLVE: A OGNI GIRO DEL LOOP OGNI STEP PUO' MUTARE CON QUESTA PROBABILITA'"},
                       F{&m_root, "root", "ROOT", "ROOT: TONICA DELLA SCALA GLOBALE. LE NOTE SUL CANALE MIDI 16 TRASPONGONO (C4 = 0)"},
                       F{&m_scale, "scale", "SCALE", "SCALE: SCALA GLOBALE PER LE TRACCE CON SCALE = GLOBAL"},
                       F{&m_run, "run", "FREE", "FREE: ON = IL SEQUENCER GIRA COL SUO CLOCK ANCHE A DAW FERMA (BPM DELLA PAGINA SYNTH)"}}) {
        f.f->bind(s, eu::gid(f.id));
        f.f->label = f.label;
        const juce::String help = f.help;
        f.f->helpFn = [help] { return help; };
        addAndMakeVisible(*f.f);
    }
    m_dice.onClick = [this] { m_seed.setIntValue(1 + juce::Random::getSystemRandom().nextInt(9999)); };
    addAndMakeVisible(m_dice);
    m_seq.onTrackSelected = [this](int t) { m_synth.selectTrack(t); m_lastSynthTrack = t; };
    showSequencer(true);
    layoutSize();
    startTimerHz(24);
}

EuclidView::~EuclidView() { setLookAndFeel(nullptr); }

void EuclidView::layoutSize()
{
    const int w = juce::jmax(m_synth.getWidth(), SeqTabPage::kLcdW * kScale);
    const int h = kTabLcdH * kScale + juce::jmax(m_synth.getHeight(), SeqTabPage::kLcdH * kScale);
    if (getWidth() != w || getHeight() != h) setSize(w, h);
    else resized();
}

void EuclidView::childBoundsChanged(juce::Component* c)
{
    if (c == &m_synth) layoutSize();
}

juce::Rectangle<int> EuclidView::tabRect(int i) const
{
    const int w0 = LcdCanvas::textWidth(bold(), "SYNTH") + 10, w1 = LcdCanvas::textWidth(bold(), "SEQUENCER") + 10;
    return i == 0 ? juce::Rectangle<int>(4 * kScale, 2 * kScale, w0 * kScale, 11 * kScale)
                  : juce::Rectangle<int>((6 + w0) * kScale, 2 * kScale, w1 * kScale, 11 * kScale);
}

void EuclidView::resized()
{
    const int top = kTabLcdH * kScale;
    m_synth.setTopLeftPosition(0, top);
    m_seq.setBounds((getWidth() - SeqTabPage::kLcdW * kScale) / 2, top, SeqTabPage::kLcdW * kScale, getHeight() - top);
    int x = tabRect(1).getRight() / kScale + 8;
    auto place = [&](juce::Component& c, int w) { c.setBounds(x * kScale, 3 * kScale, w * kScale, 9 * kScale); x += w + 3; };
    place(m_seed, 38);
    m_dice.setBounds(x * kScale, 2 * kScale, 26 * kScale, 11 * kScale);
    x += 29;
    place(m_loop, 46);
    place(m_evolve, 44);
    place(m_root, 28);
    place(m_scale, 62);
    place(m_run, 36);
}

void EuclidView::paint(juce::Graphics& g)
{
    g.fillAll(lcd::paper);
    const int w = getWidth() / kScale;
    LcdCanvas cv(w, kTabLcdH);
    for (int i = 0; i < 2; ++i) {
        const auto r = tabRect(i) / kScale;
        const bool active = (i == 1) == m_seqShown;
        if (active) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight());
        else outline(cv, r.getX(), r.getY(), r.getWidth(), r.getHeight());
        cv.textCentred(bold(), i == 0 ? "SYNTH" : "SEQUENCER", r.getX(), r.getWidth(), r.getY() + 2, !active);
    }
    cv.dotsH(0, w - 1, kTabLcdH - 1);
    // transport, right-aligned: PLAY bar.beat or STOP
    cv.text(small(), m_transport.toRawUTF8(), w - 4 - LcdCanvas::textWidth(small(), m_transport.toRawUTF8()), 5);
    cv.draw(g, 0, 0);
}

void EuclidView::mouseDown(const juce::MouseEvent& e)
{
    const auto pos = e.getEventRelativeTo(this).getPosition();
    if (tabRect(0).contains(pos)) showSequencer(false);
    else if (tabRect(1).contains(pos)) showSequencer(true);
}

void EuclidView::mouseMove(const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    setMouseCursor(tabRect(0).contains(pos) || tabRect(1).contains(pos) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void EuclidView::showSequencer(bool seq)
{
    m_seqShown = seq;
    m_synth.setVisible(!seq);
    m_seq.setVisible(seq);
    repaint();
}

void EuclidView::timerCallback()
{
    if (m_synth.selectedTrack() != m_lastSynthTrack) { m_lastSynthTrack = m_synth.selectedTrack(); m_seq.setTrack(m_lastSynthTrack); }
    if (m_seqShown) m_seq.tick();
    juce::String tr = "STOP";
    if (auto* gen = m_proc.generator(); gen && gen->uiPlaying.load()) {
        const double ppq = juce::jmax(0.0, gen->uiPpq.load());
        tr = "PLAY " + juce::String(int(ppq / 4.0) + 1) + "." + juce::String(int(std::fmod(ppq, 4.0)) + 1);
    }
    if (tr != m_transport) { m_transport = tr; repaint(0, 0, getWidth(), kTabLcdH * kScale); }
}

} // namespace mnm::plugin::one
