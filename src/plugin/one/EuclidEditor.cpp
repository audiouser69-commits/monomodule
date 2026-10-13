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
        if (param(t, "mode") == int(TrackMode::Grid)) {   // GRID: the written trigs, each with its length
            const int selStep = edit && sel ? edit->step[size_t(t)] : -1;
            for (int i = 0; i < steps; ++i) {
                const int cx = kStripX + i * kCellW;
                const uint32_t c = m_proc.gridCell(t, i);
                if (gridTrig(c)) {
                    cv.fillRect(cx, y0 + 3, 6, 7, on);
                    if (!on) for (int yy = y0 + 3; yy < y0 + 10; ++yy) for (int xx = cx; xx < cx + 6; ++xx) if (((xx + yy) & 1) == 0) cv.set(xx, yy, false);
                    const int len = gridLength(c);
                    const int end = juce::jmin(kStripX + steps * kCellW - 1, cx + (len * kCellW) / 4 - 1);
                    if (end > cx + 6) { for (int xx = cx + 6; xx <= end; ++xx) cv.set(xx, y0 + 6); cv.set(end, y0 + 5); cv.set(end, y0 + 7); }
                } else {
                    cv.fillRect(cx + 2, y0 + 6, 2, 2, on);
                }
                if (i % 4 == 0) cv.set(cx, y0 + 12);
                if (i == selStep) outline(cv, cx - 1, y0 + 1, 8, 11);
                if (i == playing) {
                    cv.fillRect(cx, y0 + 11, 6, 2);
                    if (flash && gridTrig(c)) cv.invertRect(cx - 1, y0 + 1, 8, 10);
                }
                if (m_hover.x == t && m_hover.y == i && i != selStep) outline(cv, cx - 1, y0 + 1, 8, 11);
            }
            cv.text(tiny(), "GRID", kInfoX + 22, y0 + 2);
            cv.text(tiny(), kRateNames[juce::jlimit(0, kNumRates - 1, param(t, "rate"))], kInfoX, y0 + 2);
            if (gen && on) {
                const int note = gen->ui[size_t(t)].note.load();
                if (note >= 0) cv.text(tiny(), eu::noteName(note).toUpperCase().toRawUTF8(), kInfoX, y0 + 8);
            }
            continue;
        }
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
    if (x >= kStripX && i >= 0 && i < steps && param(t, "mode") == int(TrackMode::Grid)) {   // GRID: add / select / remove
        const uint32_t c = m_proc.gridCell(t, i);
        const bool selectedNow = edit && edit->step[size_t(t)] == i && t == selected;
        if (!gridTrig(c)) m_proc.setGridCell(t, i, gridCell(true, edit ? edit->lastNote : 60, edit ? edit->lastLength : 4));
        else if (selectedNow || e.mods.isPopupMenu()) m_proc.setGridCell(t, i, c & ~kGridTrig);
        if (edit) edit->step[size_t(t)] = i;
        if (onSelect && t != selected) onSelect(t);
        repaint();
    } else if (x >= kStripX && i >= 0 && i < steps) {
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
    if (x >= kStripX && i < param(t, "steps") && param(t, "mode") == int(TrackMode::Grid)) {
        const uint32_t c = m_proc.gridCell(t, i);
        return "GRID STEP " + juce::String(i + 1) + (gridTrig(c) ? ": " + eu::noteName(gridNote(c)).toUpperCase() + " LEN " + juce::String(gridLength(c) / 4.0, 2)
                                                             : juce::String(": VUOTO")) + ". CLICK: AGGIUNGE / SELEZIONA, DI NUOVO: TOGLIE";
    }
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

SeqTabPage::SeqTabPage(MnmOneProcessor& p) : m_proc(p), m_grid(p), m_lanes(p), m_step(p), m_necklace(p)
{
    addAndMakeVisible(m_grid);
    addAndMakeVisible(m_lanes);
    for (auto* pg : {&m_rhythm, &m_motion, &m_pitch}) addAndMakeVisible(pg);
    addChildComponent(m_step);
    m_step.onChanged = [this] { m_grid.repaint(); };
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
    // GRID: the hits come from the written steps, so the Euclidean knobs and GATE do nothing
    for (int k : {1, 2, 4, 7}) m_rhythm.knobs[size_t(k)].dimFn = [this] { return gridMode(); };
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
    m_step.track = m_track;
    m_necklace.track = m_track;
    m_lanes.setTrack(m_track);
    repaint();
}

bool SeqTabPage::gridMode() const
{
    return int(std::lround(m_proc.apvts.getRawParameterValue(eu::id(m_track, "mode"))->load())) == int(TrackMode::Grid);
}

void SeqTabPage::tick()
{
    const bool grid = gridMode();
    if (m_step.isVisible() != grid) { m_step.setVisible(grid); m_pitch.setVisible(!grid); }
    if (grid) m_step.repaint();
    for (auto& k : m_rhythm.knobs) k.repaint();
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
    m_step.setBounds(m_pitch.getBounds());
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
    if (c == &m_step) { setHelp(m_step.helpAt(e.getEventRelativeTo(&m_step).getPosition())); return; }
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
// GRID helpers and the STEP page

void captureEuclidToGrid(MnmOneProcessor& p, int t)
{
    auto I = [&](const juce::String& id) { return int(std::lround(p.apvts.getRawParameterValue(id)->load())); };
    const int steps = juce::jlimit(1, kMaxSteps, I(eu::id(t, "steps"))), pulses = I(eu::id(t, "pulses")), rot = I(eu::id(t, "rotate"));
    const int low = I(eu::id(t, "low")), high = I(eu::id(t, "high")), trackScale = I(eu::id(t, "scale")) - 1;
    const int root = I(eu::gid("root")), scale = trackScale >= 0 ? trackScale : I(eu::gid("scale")), seed = I(eu::gid("seed"));
    const bool fixed = I(eu::id(t, "pmode")) == int(PitchMode::Fixed);
    const int gate = I(eu::id(t, "gate"));
    const int len = juce::jlimit(1, 16, int(std::lround(gate / 25.0)));
    const uint32_t mask = p.stepMask(t);
    uint8_t notes[128];
    const int n = scaleNotes(root, scale, low, high, notes);
    for (int i = 0; i < kMaxSteps; ++i) {
        const bool hit = i < steps && euclidHit(steps, pulses, rot, i) && !((mask >> i) & 1u);
        int note = juce::jlimit(0, 127, low);
        if (!fixed && n > 0) note = notes[juce::jlimit(0, n - 1, int(unit(hash64(uint64_t(seed), uint64_t(t), 911u, uint64_t(i))) * n))];
        p.setGridCell(t, i, gridCell(hit, note, len));
    }
}

void clearGrid(MnmOneProcessor& p, int t)
{
    for (int i = 0; i < kMaxSteps; ++i) p.setGridCell(t, i, p.gridCell(t, i) & ~kGridTrig);
}

void shiftGrid(MnmOneProcessor& p, int t, int by)
{
    const int steps = juce::jlimit(1, kMaxSteps, int(std::lround(p.apvts.getRawParameterValue(eu::id(t, "steps"))->load())));
    std::array<uint32_t, kMaxSteps> c{};
    for (int i = 0; i < steps; ++i) c[size_t(i)] = p.gridCell(t, i);
    for (int i = 0; i < steps; ++i) p.setGridCell(t, ((i + by) % steps + steps) % steps, c[size_t(i)]);
}

void transposeGrid(MnmOneProcessor& p, int t, int semitones)
{
    for (int i = 0; i < kMaxSteps; ++i) {
        const uint32_t c = p.gridCell(t, i);
        p.setGridCell(t, i, gridCell(gridTrig(c), juce::jlimit(0, 127, gridNote(c) + semitones), gridLength(c)));
    }
}

namespace {
constexpr const char* kStepCellNames[8] = {"STEP", "NOTE", "LEN", "TRIG", "OCT", "SHFT", "EUC>", "CLR"};
juce::String lenText(int q) { return q % 4 == 0 ? juce::String(q / 4) : juce::String(q / 4.0, 2).trimCharactersAtEnd("0"); }
}

int GridStepPage::cellAt(juce::Point<int> local) const
{
    const int x = toLcd(local.x), y = toLcd(local.y) - 11;
    if (y < 0 || x < 0 || x >= 4 * kCell) return -1;
    return juce::jlimit(0, 7, (y / kCell) * 4 + x / kCell);
}

void GridStepPage::paint(juce::Graphics& g)
{
    LcdCanvas cv(kLcdW, kLcdH);
    const int sel = edit ? edit->step[size_t(track)] : 0;
    const uint32_t c = m_proc.gridCell(track, sel);
    cv.fillRect(0, 0, kLcdW, 10);
    cv.text(bold(), "GRID STEP", 3, 1, false);
    cv.dotsH(0, kLcdW - 1, kLcdH - 1);
    cv.dotsV(kLcdW - 1, 11, kLcdH - 1);
    for (int k = 0; k < 8; ++k) {
        const int x0 = (k % 4) * kCell, y0 = 11 + (k / 4) * kCell;
        cv.dotsH(x0, x0 + kCell, y0);
        cv.dotsV(x0, y0, y0 + kCell - 1);
        cv.textCentred(tiny(), kStepCellNames[k], x0 + 1, kCell - 1, y0 + kLabelY);
        juce::String v;
        switch (k) {
        case 0: v = juce::String(sel + 1); break;
        case 1: v = gridTrig(c) ? eu::noteName(gridNote(c)).toUpperCase() : juce::String("--"); break;
        case 2: v = gridTrig(c) ? lenText(gridLength(c)) : juce::String("--"); break;
        case 3: v = gridTrig(c) ? "ON" : "OFF"; break;
        case 4: v = "+/-"; break;
        case 5: v = "< >"; break;
        case 6: v = "GRID"; break;
        default: v = "ALL"; break;
        }
        cv.textCentred(k < 4 ? bold() : small(), fit(bold(), v, kCell - 4).toRawUTF8(), x0 + 1, kCell - 1, y0 + 12);
        if (k == m_hover || k == m_down) cv.invertRect(x0 + 1, y0 + kValueBoxY, kCell - 1, kValueBoxH);
        cv.textCentred(tiny(), k == 0 ? "DRAG" : k < 3 ? "DRAG" : "CLICK", x0 + 1, kCell - 1, y0 + kValueBoxY + 2, !(k == m_hover || k == m_down));
    }
    cv.draw(g, 0, 0);
}

void GridStepPage::nudge(int cell, int d)
{
    if (!edit || d == 0) return;
    const int steps = juce::jlimit(1, kMaxSteps, int(std::lround(m_proc.apvts.getRawParameterValue(eu::id(track, "steps"))->load())));
    int& sel = edit->step[size_t(track)];
    const uint32_t c = m_proc.gridCell(track, sel);
    switch (cell) {
    case 0: sel = juce::jlimit(0, steps - 1, sel + d); break;
    case 1: { const int n = juce::jlimit(0, 127, gridNote(c) + d); m_proc.setGridCell(track, sel, gridCell(true, n, gridLength(c))); edit->lastNote = n; break; }
    case 2: { const int q = juce::jlimit(1, 128, gridLength(c) + d); m_proc.setGridCell(track, sel, gridCell(true, gridNote(c), q)); edit->lastLength = q; break; }
    case 4: transposeGrid(m_proc, track, 12 * d); break;
    case 5: shiftGrid(m_proc, track, d); break;
    default: break;
    }
    if (onChanged) onChanged();
    repaint();
}

void GridStepPage::mouseDown(const juce::MouseEvent& e)
{
    m_down = cellAt(e.getPosition());
    m_applied = 0;
    if (m_down == 3 && edit) {
        const int sel = edit->step[size_t(track)];
        const uint32_t c = m_proc.gridCell(track, sel);
        m_proc.setGridCell(track, sel, gridTrig(c) ? (c & ~kGridTrig) : gridCell(true, edit->lastNote, edit->lastLength));
    } else if (m_down == 6) {
        captureEuclidToGrid(m_proc, track);
    } else if (m_down == 7) {
        clearGrid(m_proc, track);
    }
    if (onChanged) onChanged();
    repaint();
}

void GridStepPage::mouseDrag(const juce::MouseEvent& e)
{
    if (m_down < 0 || m_down == 3 || m_down >= 6) return;
    const int pxPerStep = m_down == 4 ? 24 : m_down == 5 ? 18 : 8;
    const int total = int(std::lround(float(e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / float(pxPerStep)));
    nudge(m_down, total - m_applied);
    m_applied = total;
}

void GridStepPage::mouseUp(const juce::MouseEvent&) { m_down = -1; repaint(); }

void GridStepPage::mouseMove(const juce::MouseEvent& e)
{
    const int c = cellAt(e.getPosition());
    if (c != m_hover) { m_hover = c; repaint(); }
    setMouseCursor(c >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void GridStepPage::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int c = cellAt(e.getPosition());
    if (c < 0 || c == 3 || c >= 6) return;
    m_wheel += w.deltaY;
    const int d = int(m_wheel / 0.1f);
    if (d == 0) return;
    m_wheel -= float(d) * 0.1f;
    nudge(c, w.isReversed ? -d : d);
}

juce::String GridStepPage::helpAt(juce::Point<int> local) const
{
    switch (cellAt(local)) {
    case 0: return "STEP: QUALE STEP STAI MODIFICANDO (TRASCINA O ROTELLA). SI SCEGLIE ANCHE CLICCANDO SUI TASTI TRIG SOTTO IL DISPLAY";
    case 1: return "NOTE: LA NOTA DELLO STEP (TRASCINA, ROTELLA, OPPURE SUONALA SULLA TASTIERA DEL PANNELLO)";
    case 2: return "LEN: QUANTO DURA LA NOTA (NOTE OFF), IN STEP: 0.25 = UN QUARTO DI STEP, 4 = QUATTRO STEP (SI LEGA ALLE SUCCESSIVE)";
    case 3: return "TRIG: ACCENDE O SPEGNE IL TRIG DELLO STEP (LA NOTA RESTA MEMORIZZATA)";
    case 4: return "OCT: TRASPONE TUTTE LE NOTE DELLA TRACCIA DI UN'OTTAVA (TRASCINA SU/GIU')";
    case 5: return "SHFT: SPOSTA TUTTO IL PATTERN DI UNO STEP A DESTRA O A SINISTRA (TRASCINA)";
    case 6: return "EUC>GRID: COPIA IL RITMO EUCLIDEO ATTUALE NEGLI STEP, CON NOTE DAL RANGE E DALLA SCALA, PER RIFINIRLO A MANO";
    case 7: return "CLR: TOGLIE TUTTI I TRIG DELLA TRACCIA";
    default: return {};
    }
}

// ---------------------------------------------------------------------------
// EuclidView: the front panel

namespace {
constexpr const char* kEncPageNames[EuclidView::NumEncPages] = {"Syn", "Amp", "Filt", "Efx", "Lfo 1", "Lfo 2", "Lfo 3", "Rhythm", "Motion"};
constexpr const char* kRhythmIds[8] = {"steps", "pulses", "rotate", "rate", "accents", "prob", "cond", "gate"};
constexpr const char* kRhythmNames[8] = {"STEPS", "PULSES", "ROTATE", "RATE", "ACCENT", "CHANCE", "COND", "GATE"};
constexpr const char* kMotionIds[8] = {"rchance", "rmax", "rshape", "rpitch", "swing", "drift", nullptr, nullptr};
constexpr const char* kMotionNames[8] = {"RATCHET", "R MAX", "R SHAPE", "R PITCH", "SWING", "DRIFT", "", ""};
}

EuclidView::EuclidView(MnmOneProcessor& p) : m_proc(p), m_synth(p), m_seq(p), m_maku(p), m_mix(p), m_trigs(p)
{
    setLookAndFeel(&m_lnf);
    addChildComponent(m_synth);
    addAndMakeVisible(m_seq);
    addChildComponent(m_maku);
    addChildComponent(m_mix);
    m_maku.onFocus = [this](int) { bindEncoders(); };
    m_seq.setGridEdit(&m_edit);
    m_seq.onTrackSelected = [this](int t) { selectTrack(t); };

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

    // ---- track keys
    for (int t = 0; t < 6; ++t) {
        auto& k = m_trackKeys[size_t(t)];
        k = std::make_unique<hw::HwKey>("Track " + juce::String(t + 1));
        k->setCaption(juce::String(t + 1));
        k->onClick = [this, t] {
            if (!m_makuShown) { selectTrack(t); return; }
            if (t < maku::kParts) { m_proc.makuCommand({maku::MakuCommand::Focus, t}); m_maku.setHelp(m_maku.ui().part[size_t(t)].data() + juce::String(" A FUOCO: ENCODER E-G = I SUOI MACRO, LA TASTIERA LA SUONA")); }
        };
        addAndMakeVisible(*k);
    }
    // ---- function keys
    m_synthKey.onClick = [this] { showSequencer(false); };
    m_seqKey.onClick = [this] { showSequencer(true); };
    m_gridKey.onClick = [this] { setMode(!gridMode()); };
    m_recKey.setClickingTogglesState(true);
    m_recKey.onClick = [this] { m_proc.setRecord(m_recKey.getToggleState(), m_track); };
    m_clearKey.onClick = [this] { if (gridMode()) clearGrid(m_proc, m_track); else m_proc.setStepMask(m_track, 0); };
    m_captureKey.onClick = [this] { captureEuclidToGrid(m_proc, m_track); setMode(true); };
    m_makuKey.onClick = [this] { if (m_makuShown) showSequencer(true); else showMaku(); };
    m_mixKey.onClick = [this] { if (m_mixShown) showSequencer(true); else showMix(); };
    m_mixKey.setTooltip("MIX: le sei tracce e MAKU come canali (livelli, mute, meter), le uscite e il routing tra i due motori");
    addAndMakeVisible(m_mixKey);
    m_makuOnKey.onClick = [this] {
        if (auto* on = m_proc.apvts.getParameter(maku::kOnId)) {
            on->beginChangeGesture();
            on->setValueNotifyingHost(on->getValue() >= 0.5f ? 0.0f : 1.0f);
            on->endChangeGesture();
        }
    };
    m_makuKey.setTooltip("MAKU: il generatore ambient sul display; il pannello suona i suoi gesti (di nuovo = torna al SEQ)");
    m_makuOnKey.setTooltip("MAKU ON: accende / spegne MAKU (spento non consuma CPU)");
    for (auto* k : {&m_makuKey, &m_makuOnKey}) { k->setEnabled(m_proc.makuAvailable()); addAndMakeVisible(*k); }
    m_synthKey.setTooltip("SYNTH: le pagine della Monomachine (macchina, SYN, AMP, FILT, EFX, LFO) sul display");
    m_seqKey.setTooltip("SEQ: il sequencer sul display");
    m_gridKey.setTooltip("GRID: la traccia selezionata suona le note scritte sui tasti trig (LED acceso) invece del ritmo euclideo");
    m_recKey.setTooltip("REC: le note che suoni (tastiera MIDI o la tastiera del pannello) si scrivono negli step della traccia");
    m_clearKey.setTooltip("CLEAR: GRID = toglie tutti i trig; EUCLID = toglie i mute messi a mano");
    m_captureKey.setTooltip("EUC > GRID: copia il ritmo euclideo attuale negli step, da rifinire a mano");
    for (auto* k : {&m_synthKey, &m_seqKey, &m_gridKey, &m_recKey, &m_clearKey, &m_captureKey}) addAndMakeVisible(*k);

    // ---- data entry
    for (int i = 0; i < NumEncPages; ++i) {
        auto& k = m_pageKeys[size_t(i)];
        k = std::make_unique<hw::HwKey>(kEncPageNames[i], i >= RhythmPage ? hw::colour::ledAmber : hw::colour::led);
        k->onClick = [this, i] {
            if (m_mixShown) {   // MUTE 1-6, MAKU ON
                const juce::String id = i < 6 ? muteId(i) : i == 6 ? juce::String(maku::kOnId) : juce::String();
                if (auto* p = id.isEmpty() ? nullptr : m_proc.apvts.getParameter(id)) {
                    p->beginChangeGesture(); p->setValueNotifyingHost(p->getValue() >= 0.5f ? 0.0f : 1.0f); p->endChangeGesture();
                }
                return;
            }
            if (!m_makuShown) { setEncoderPage(i); return; }
            if (maku::kVerbs[i].button == maku::BHome) m_proc.makuCommand({maku::MakuCommand::NewWorld});
            else if (maku::kVerbs[i].button == maku::BRec) m_proc.makuCommand({maku::MakuCommand::Like});
        };
        k->onStateChange = [this] { makuButtons(); };
        addAndMakeVisible(*k);
    }
    for (auto& e : m_enc) addAndMakeVisible(e);

    // ---- trig keys and keyboard
    m_trigs.track = [this] { return m_track; };
    m_trigs.selectedStep = [this](int t) -> int& { return m_edit.step[size_t(t)]; };
    m_trigs.lastNote = [this]() -> int& { return m_edit.lastNote; };
    m_trigs.lastLength = [this]() -> int& { return m_edit.lastLength; };
    m_trigs.onHint = [this](const juce::String& h) { setHint(h); };
    addAndMakeVisible(m_trigs);
    m_stepPage1.onClick = [this] { m_trigs.page = 0; };
    m_stepPage2.onClick = [this] { m_trigs.page = 1; };
    m_octDown.onClick = [this] { if (m_makuShown) { MakuPage::fifth(m_proc, -1); return; } m_keys.baseOctave = juce::jmax(0, m_keys.baseOctave - 1); m_keys.repaint(); };
    m_octUp.onClick = [this] { if (m_makuShown) { MakuPage::fifth(m_proc, 1); return; } m_keys.baseOctave = juce::jmin(7, m_keys.baseOctave + 1); m_keys.repaint(); };
    for (auto* k : {&m_stepPage1, &m_stepPage2, &m_octDown, &m_octUp}) addAndMakeVisible(*k);
    m_keys.onNote = [this](int note, bool on) {
        if (m_makuShown) { makuNote(note, on); return; }
        m_proc.auditionNote(m_track, note, on);
        if (!on) return;
        m_edit.lastNote = note;
        const bool playing = m_proc.generator() && m_proc.generator()->uiPlaying.load();
        if (gridMode()) writeNote(note, m_recKey.getToggleState() && !playing);
    };
    addAndMakeVisible(m_keys);

    selectTrack(0);
    showSequencer(true);
    layoutSize();
    startTimerHz(30);
}

EuclidView::~EuclidView() { setLookAndFeel(nullptr); }

bool EuclidView::gridMode() const
{
    return int(std::lround(m_proc.apvts.getRawParameterValue(eu::id(m_track, "mode"))->load())) == int(TrackMode::Grid);
}

void EuclidView::setMode(bool grid)
{
    if (grid) {   // a first GRID starts from what the generator was playing, so there is something to edit
        bool empty = true;
        for (int i = 0; i < kMaxSteps; ++i) empty = empty && !gridTrig(m_proc.gridCell(m_track, i));
        if (empty) captureEuclidToGrid(m_proc, m_track);
    }
    if (auto* p = m_proc.apvts.getParameter(eu::id(m_track, "mode"))) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(grid ? 1.0f : 0.0f));
        p->endChangeGesture();
    }
    setHint(grid ? "GRID: CLICCA I TASTI TRIG PER SCRIVERE LE NOTE, SUONA LA TASTIERA PER CAMBIARLE, TRASCINA A DESTRA PER LA LUNGHEZZA"
                 : "EUCLID: IL RITMO E LE NOTE LI GENERA IL SEQUENCER. I TASTI TRIG MOSTRANO I COLPI E LI SILENZIANO A MANO");
}

void EuclidView::writeNote(int note, bool advance)
{
    const int t = m_track;
    int& s = m_edit.step[size_t(t)];
    const uint32_t c = m_proc.gridCell(t, s);
    m_proc.setGridCell(t, s, gridCell(true, note, gridTrig(c) ? gridLength(c) : m_edit.lastLength));
    m_edit.lastNote = note;
    if (advance) {
        const int steps = juce::jlimit(1, kMaxSteps, int(std::lround(m_proc.apvts.getRawParameterValue(eu::id(t, "steps"))->load())));
        s = (s + 1) % steps;
        m_trigs.page = s / 16;
    }
    setHint("STEP " + juce::String(s + 1) + "   " + eu::noteName(note) + (advance ? "   >> STEP SUCCESSIVO" : ""));
}

void EuclidView::setHint(const juce::String& s)
{
    if (s == m_hint) return;
    m_hint = s;
    repaint(m_hintArea);
    if (m_seqShown) m_seq.showHint(s.toUpperCase());
    if (m_makuShown) m_maku.setHelp(s.toUpperCase());
}

// MAKU's keyboard is F3 .. G5: a note outside it plays an octave in. With the kick in focus the white keys are its
// 16 steps (MAKU's own rule).
void EuclidView::makuNote(int note, bool on)
{
    int k = note - maku::kFirstKeyNote;
    while (k < 0) k += 12;
    while (k >= maku::kKeys) k -= 12;
    const uint32_t bit = 1u << k;
    const uint32_t keys = on ? (m_mkKeys | bit) : (m_mkKeys & ~bit);
    if (keys == m_mkKeys) return;
    m_mkKeys = keys;
    m_proc.makuCommand({maku::MakuCommand::Keys, int(keys)});
}

void EuclidView::makuButtons()
{
    if (!m_makuShown) return;
    uint32_t mask = 0;
    for (int i = 0; i < maku::kNumVerbs; ++i) {
        const int b = maku::kVerbs[i].button;
        if (b != maku::BHome && b != maku::BRec && m_pageKeys[size_t(i)]->isDown()) mask |= 1u << b;   // (NEW WORLD, LIKE: a call each)
    }
    if (mask == m_mkButtons) return;
    const uint32_t fresh = mask & ~m_mkButtons;
    m_mkButtons = mask;
    m_proc.makuCommand({maku::MakuCommand::Buttons, int(mask)});
    for (int i = 0; i < maku::kNumVerbs; ++i)
        if ((fresh >> maku::kVerbs[i].button) & 1u) m_maku.setHelp(maku::kVerbs[i].help);
}

void EuclidView::hidePages()
{
    m_synth.setVisible(false);
    m_seq.setVisible(false);
    m_maku.setVisible(false);
    m_mix.setVisible(false);
    for (juce::Component* c : {static_cast<juce::Component*>(&m_seed), static_cast<juce::Component*>(&m_loop), static_cast<juce::Component*>(&m_evolve),
                               static_cast<juce::Component*>(&m_root), static_cast<juce::Component*>(&m_scale), static_cast<juce::Component*>(&m_run),
                               static_cast<juce::Component*>(&m_dice)})
        c->setVisible(false);
}

void EuclidView::showMix()
{
    leaveMaku();
    hidePages();
    m_mixShown = true;
    m_seqShown = false;
    m_mix.setVisible(true);
    static const char* const keys[NumEncPages] = {"Mute 1", "Mute 2", "Mute 3", "Mute 4", "Mute 5", "Mute 6", "Maku On", "-", "-"};
    for (int i = 0; i < NumEncPages; ++i) { m_pageKeys[size_t(i)]->setButtonText(keys[i]); m_pageKeys[size_t(i)]->setEnabled(i < 7); }
    m_synthKey.setToggleState(false, juce::dontSendNotification);
    m_seqKey.setToggleState(false, juce::dontSendNotification);
    m_mixKey.setToggleState(true, juce::dontSendNotification);
    m_mix.tick();
    m_boundPage = -1;
    bindEncoders();
    repaint();
}

void EuclidView::leaveMix()
{
    if (!m_mixShown) return;
    m_mixShown = false;
    m_mix.setVisible(false);
    for (int i = 0; i < NumEncPages; ++i) { m_pageKeys[size_t(i)]->setButtonText(kEncPageNames[i]); m_pageKeys[size_t(i)]->setEnabled(true); m_pageKeys[size_t(i)]->setLed(0.0f); }
    m_mixKey.setToggleState(false, juce::dontSendNotification);
    m_boundPage = -1;
}

void EuclidView::showMaku()
{
    if (!m_proc.makuAvailable()) return;
    leaveMix();
    m_makuShown = true;
    m_seqShown = false;
    m_synth.setVisible(false);
    m_seq.setVisible(false);
    m_maku.setVisible(true);
    for (juce::Component* c : {static_cast<juce::Component*>(&m_seed), static_cast<juce::Component*>(&m_loop), static_cast<juce::Component*>(&m_evolve),
                               static_cast<juce::Component*>(&m_root), static_cast<juce::Component*>(&m_scale), static_cast<juce::Component*>(&m_run),
                               static_cast<juce::Component*>(&m_dice)})
        c->setVisible(false);
    for (int i = 0; i < NumEncPages; ++i) m_pageKeys[size_t(i)]->setButtonText(maku::kVerbs[i].key);
    static const char* const parts[6] = {"Pulse", "Floor", "Haze", "Voice", "-", "-"};
    for (int t = 0; t < 6; ++t) { m_trackKeys[size_t(t)]->setButtonText(parts[t]); m_trackKeys[size_t(t)]->setEnabled(t < maku::kParts); }
    m_octDown.setButtonText("Flat");
    m_octUp.setButtonText("Sharp");
    m_synthKey.setToggleState(false, juce::dontSendNotification);
    m_seqKey.setToggleState(false, juce::dontSendNotification);
    m_makuKey.setToggleState(true, juce::dontSendNotification);
    m_maku.tick();
    m_boundPage = -1;
    bindEncoders();
    repaint();
}

void EuclidView::leaveMaku()
{
    if (!m_makuShown) return;
    m_makuShown = false;
    m_maku.setVisible(false);
    if (m_mkKeys) { m_mkKeys = 0; m_proc.makuCommand({maku::MakuCommand::Keys, 0}); }
    if (m_mkButtons) { m_mkButtons = 0; m_proc.makuCommand({maku::MakuCommand::Buttons, 0}); }
    for (int i = 0; i < NumEncPages; ++i) m_pageKeys[size_t(i)]->setButtonText(kEncPageNames[i]);
    for (int t = 0; t < 6; ++t) { m_trackKeys[size_t(t)]->setButtonText("Track " + juce::String(t + 1)); m_trackKeys[size_t(t)]->setEnabled(true); }
    m_octDown.setButtonText("Oct -");
    m_octUp.setButtonText("Oct +");
    m_makuKey.setToggleState(false, juce::dontSendNotification);
    for (auto& e : m_enc) e.onValueChange = nullptr;
    for (auto& k : m_pageKeys) k->setLed(0.0f);
    m_boundPage = -1;
    selectTrack(m_track);
}

void EuclidView::selectTrack(int t)
{
    m_track = juce::jlimit(0, 5, t);
    if (m_synth.selectedTrack() != m_track) m_synth.selectTrack(m_track);
    if (m_seq.track() != m_track) m_seq.setTrack(m_track);
    m_lastSynthTrack = m_track;
    m_proc.setRecord(m_recKey.getToggleState(), m_track);
    for (int i = 0; i < 6; ++i) m_trackKeys[size_t(i)]->setToggleState(i == m_track, juce::dontSendNotification);
    bindEncoders();
    repaint();
}

void EuclidView::setEncoderPage(int page)
{
    m_encPage = juce::jlimit(0, NumEncPages - 1, page);
    bindEncoders();
    repaint(m_encArea);
}

void EuclidView::bindEncoders()
{
    if (m_mixShown) {   // A-F the six track levels, G MAKU LEVEL, H MAKU SEND LEVEL
        if (m_boundPage == -3) return;
        m_boundPage = -3; m_boundTrack = -1;
        for (int k = 0; k < 8; ++k) {
            auto& e = m_enc[size_t(k)];
            m_encAttach[size_t(k)].reset();
            e.onValueChange = nullptr;
            const juce::String id = k < 6 ? levelId(k) : k == 6 ? juce::String(maku::kLevelId) : juce::String(maku::kSendLevelId);
            const bool ok = k < 6 || m_proc.makuAvailable();
            e.setEnabled(ok);
            if (!ok) continue;
            m_encAttach[size_t(k)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, id, e);
            e.getProperties().set("pid", id);
        }
        repaint(m_encArea);
        return;
    }
    if (m_makuShown) {   // A-D the four parts' levels, E-G the focused part's macros, H MAKU LEVEL
        const int focus = juce::jlimit(0, maku::kParts - 1, m_maku.ui().focus);
        if (m_boundPage == -2 && m_boundSlot == focus) return;
        m_boundPage = -2; m_boundSlot = focus; m_boundTrack = -1;
        for (int k = 0; k < 8; ++k) {
            auto& e = m_enc[size_t(k)];
            m_encAttach[size_t(k)].reset();
            e.setEnabled(true);
            if (k == 7) {
                e.onValueChange = nullptr;
                m_encAttach[size_t(k)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, maku::kLevelId, e);
                e.getProperties().set("pid", maku::kLevelId);
                continue;
            }
            e.getProperties().set("pid", "");
            e.setRange(0.0, 127.0, 1.0);
            e.setDoubleClickReturnValue(false, 0.0);
            e.setValue(k < 4 ? m_maku.ui().level[size_t(k)] : m_maku.ui().macro[size_t(focus)][size_t(k - 4)], juce::dontSendNotification);
            e.onValueChange = [this, k] {
                const int v = int(std::lround(m_enc[size_t(k)].getValue()));
                const int f = juce::jlimit(0, maku::kParts - 1, m_maku.ui().focus);
                if (k < 4) m_proc.makuCommand({maku::MakuCommand::Level, k, v});
                else m_proc.makuCommand({maku::MakuCommand::Macro, f, k - 4, v});
            };
        }
        repaint(m_encArea);
        return;
    }
    const int t = m_track;
    const int slot = juce::jlimit(0, spec::kNumMachines - 1, int(std::lround(m_proc.apvts.getRawParameterValue(machineId(t))->load())));
    if (t == m_boundTrack && m_encPage == m_boundPage && slot == m_boundSlot) return;
    m_boundTrack = t; m_boundPage = m_encPage; m_boundSlot = slot;
    for (int k = 0; k < 8; ++k) {
        m_encAttach[size_t(k)].reset();
        juce::String id;
        switch (m_encPage) {
        case SynPage: id = synId(t, k); break;
        case AmpPage: id = pageId(t, host::Page::AMP, k); break;
        case FiltPage: id = pageId(t, host::Page::FILT, k); break;
        case EfxPage: id = pageId(t, host::Page::EFX, k); break;
        case Lfo1Page: case Lfo2Page: case Lfo3Page: id = lfoId(t, m_encPage - Lfo1Page, k); break;
        case RhythmPage: id = eu::id(t, kRhythmIds[k]); break;
        default: id = kMotionIds[k] ? eu::id(t, kMotionIds[k]) : juce::String(); break;
        }
        const bool blank = id.isEmpty() || (m_encPage == SynPage && spec::kMachines[slot].params[k].display == spec::Display::Blank);
        auto& e = m_enc[size_t(k)];
        e.setEnabled(!blank);
        if (blank) { e.setValue(0.0, juce::dontSendNotification); continue; }
        m_encAttach[size_t(k)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, id, e);
        e.getProperties().set("pid", id);
    }
    repaint(m_encArea);
}

juce::String EuclidView::encoderLabel(int k) const
{
    if (m_mixShown) return k < 6 ? "T" + juce::String(k + 1) + " LEVEL" : k == 6 ? juce::String("MAKU") : juce::String("SEND");
    if (m_makuShown) {
        const auto& u = m_maku.ui();
        const int f = juce::jlimit(0, maku::kParts - 1, u.focus);
        if (k < 4) return u.part[size_t(k)].data();
        if (k < 7) return u.macroName[size_t(f)][size_t(k - 4)].data();
        return "LEVEL";
    }
    const int slot = juce::jlimit(0, spec::kNumMachines - 1, int(std::lround(m_proc.apvts.getRawParameterValue(machineId(m_track))->load())));
    switch (m_encPage) {
    case SynPage: { const auto& p = spec::kMachines[slot].params[k]; return p.display == spec::Display::Blank ? juce::String("-") : juce::String(p.label); }
    case AmpPage: case FiltPage: case EfxPage: return spec::kSharedPages[m_encPage - AmpPage].labels[k];
    case Lfo1Page: case Lfo2Page: case Lfo3Page: return spec::kLfoParams[k].label;
    case RhythmPage: return kRhythmNames[k];
    default: return kMotionNames[k];
    }
}

void EuclidView::layoutSize()
{
    const int lcdW = juce::jmax(m_synth.getWidth(), SeqTabPage::kLcdW * kScale);
    const int lcdH = juce::jmax(m_synth.getHeight(), kStatusH + SeqTabPage::kLcdH * kScale);
    const int w = kMargin * 2 + kLeftW + kGap + lcdW + 2 * kBezel + kGap + kRightW;
    const int h = kMargin * 2 + kHeaderH + lcdH + 2 * kBezel + 20 + kTrigH + 14 + kKeysH;
    if (getWidth() != w || getHeight() != h) setSize(w, h);
    else resized();
}

void EuclidView::childBoundsChanged(juce::Component* c)
{
    if (c == &m_synth) layoutSize();
}

void EuclidView::resized()
{
    const int lcdW = juce::jmax(m_synth.getWidth(), SeqTabPage::kLcdW * kScale);
    const int lcdH = juce::jmax(m_synth.getHeight(), kStatusH + SeqTabPage::kLcdH * kScale);
    const int x0 = kMargin, y0 = kMargin + kHeaderH;
    m_bezel = {x0 + kLeftW + kGap, y0, lcdW + 2 * kBezel, lcdH + 2 * kBezel};
    m_lcd = m_bezel.reduced(kBezel);
    m_synth.setTopLeftPosition(m_lcd.getX() + (lcdW - m_synth.getWidth()) / 2, m_lcd.getY() + (lcdH - m_synth.getHeight()) / 2);
    m_statusArea = m_lcd.withHeight(kStatusH);
    m_seq.setBounds(m_lcd.getX() + (lcdW - SeqTabPage::kLcdW * kScale) / 2, m_lcd.getY() + kStatusH, SeqTabPage::kLcdW * kScale, SeqTabPage::kLcdH * kScale);
    m_maku.setBounds(m_seq.getX(), m_lcd.getY(), MakuPage::kLcdW * kScale, MakuPage::kLcdH * kScale);
    m_mix.setBounds(m_maku.getBounds());

    // status line of the SEQ screen (LCD px from the left of the LCD)
    int x = 66;
    auto place = [&](juce::Component& c, int w) { c.setBounds(m_lcd.getX() + x * kScale, m_lcd.getY() + 3 * kScale, w * kScale, 9 * kScale); x += w + 3; };
    place(m_seed, 38);
    m_dice.setBounds(m_lcd.getX() + x * kScale, m_lcd.getY() + 2 * kScale, 26 * kScale, 11 * kScale);
    x += 29;
    place(m_loop, 46);
    place(m_evolve, 44);
    place(m_root, 28);
    place(m_scale, 62);
    place(m_run, 36);

    // left: track keys, function keys
    const int kw = (kLeftW - 10) / 2, kh = 66;
    for (int t = 0; t < 6; ++t)
        m_trackKeys[size_t(t)]->setBounds(x0 + (t % 2) * (kw + 10), y0 + 24 + (t / 2) * (kh + 6), kw, kh);
    const int fy = y0 + 24 + 3 * (kh + 6) + 34;
    hw::HwKey* fn[6] = {&m_synthKey, &m_seqKey, &m_gridKey, &m_recKey, &m_clearKey, &m_captureKey};
    for (int i = 0; i < 6; ++i) fn[i]->setBounds(x0 + (i % 2) * (kw + 10), fy + (i / 2) * (kh + 6), kw, kh);
    const int my = fy + 3 * (kh + 6) + 34;   // MAKU
    m_makuKey.setBounds(x0, my, kw, kh);
    m_makuOnKey.setBounds(x0 + kw + 10, my, kw, kh);
    m_mixKey.setBounds(x0, my + kh + 6, kw, kh);

    // right: encoders and page keys
    const int rx = m_bezel.getRight() + kGap;
    m_encArea = {rx, y0, kRightW, 24 + 2 * 104};
    const int ew = kRightW / 4;
    for (int k = 0; k < 8; ++k) m_enc[size_t(k)].setBounds(rx + (k % 4) * ew + 6, y0 + 24 + (k / 4) * 104 + 18, ew - 12, ew - 12);
    const int py = y0 + 24 + 2 * 104 + 34;
    const int pw = (kRightW - 16) / 3;
    for (int i = 0; i < NumEncPages; ++i) m_pageKeys[size_t(i)]->setBounds(rx + (i % 3) * (pw + 8), py + (i / 3) * 64, pw, 58);
    m_transportArea = {rx, py + 3 * 64 + 10, kRightW, m_bezel.getBottom() - (py + 3 * 64 + 10)};

    // bottom: trig keys, step pages, keyboard
    m_trigArea = {m_bezel.getX(), m_bezel.getBottom() + 20, m_bezel.getWidth(), kTrigH};
    m_trigs.setBounds(m_trigArea);
    m_stepPage1.setBounds(x0, m_trigArea.getY() + 14, kw, 58);
    m_stepPage2.setBounds(x0 + kw + 10, m_trigArea.getY() + 14, kw, 58);
    m_keysArea = {m_bezel.getX(), m_trigArea.getBottom() + 14, 15 * 56, kKeysH};
    m_keys.setBounds(m_keysArea);
    m_octDown.setBounds(x0, m_keysArea.getY() + 22, kw, 58);
    m_octUp.setBounds(x0 + kw + 10, m_keysArea.getY() + 22, kw, 58);
    m_hintArea = {m_keysArea.getRight() + 24, m_keysArea.getY(), getWidth() - kMargin - m_keysArea.getRight() - 24, kKeysH};
}

void EuclidView::paint(juce::Graphics& g)
{
    using namespace hw;
    const auto all = getLocalBounds().toFloat();
    juce::ColourGradient grad(colour::chassisTop, 0.0f, 0.0f, colour::chassisBottom, 0.0f, all.getHeight(), false);
    g.setGradientFill(grad);
    g.fillRect(all);
    // brushed texture: faint horizontal lines
    g.setColour(juce::Colours::white.withAlpha(0.012f));
    for (int y = 0; y < getHeight(); y += 3) g.fillRect(0, y, getWidth(), 1);
    g.setColour(colour::panelLine);
    g.drawRect(all.reduced(6.0f), 1.0f);
    for (auto c : {juce::Point<float>(16, 16), juce::Point<float>(all.getRight() - 16, 16), juce::Point<float>(16, all.getBottom() - 16),
                   juce::Point<float>(all.getRight() - 16, all.getBottom() - 16)})
        drawScrew(g, c, 6.0f);

    // header: name, and what this is
    g.setColour(colour::silk);
    g.setFont(font(30.0f, true).withExtraKerningFactor(0.18f));
    g.drawText("MONOMODULE", kMargin, kMargin - 2, 330, 40, juce::Justification::centredLeft, false);
    g.setColour(colour::led);
    g.drawText("EUCLID", kMargin + 300, kMargin - 2, 200, 40, juce::Justification::centredLeft, false);
    g.setColour(colour::silkDim);
    g.setFont(font(12.0f, true).withExtraKerningFactor(0.2f));
    g.drawText("SIX TRACK DSP SYNTHESIS  /  GENERATIVE EUCLIDEAN SEQUENCER", kMargin, kMargin + 34, 700, 18, juce::Justification::centredLeft, false);
    g.drawText("EMULATED DSP56300 ENGINE", getWidth() - kMargin - 400, kMargin + 6, 400, 18, juce::Justification::centredRight, false);

    // section labels
    auto label = [&](const juce::String& s, int x, int y, int w) {
        g.setColour(colour::silkDim);
        g.setFont(font(11.0f, true).withExtraKerningFactor(0.25f));
        g.drawText(s, x, y, w, 14, juce::Justification::centredLeft, false);
        g.setColour(colour::panelLine);
        const int tw = juce::GlyphArrangement::getStringWidthInt(font(11.0f, true).withExtraKerningFactor(0.25f), s);
        g.fillRect(x + tw + 8, y + 7, juce::jmax(0, w - tw - 8), 1);
    };
    const int y0 = kMargin + kHeaderH;
    label(m_makuShown ? "PART" : "TRACK", kMargin, y0, kLeftW);
    label("FUNCTION", kMargin, y0 + 24 + 3 * 72 + 10, kLeftW);
    label("AMBIENT", kMargin, m_makuKey.getY() - 22, kLeftW);
    if (m_mixShown) {
        label("DATA ENTRY  MIX", m_encArea.getX(), y0, kRightW);
        label("MUTES", m_encArea.getX(), y0 + 24 + 2 * 104 + 12, kRightW);
    } else if (m_makuShown) {
        const int f = juce::jlimit(0, maku::kParts - 1, m_maku.ui().focus);
        label("DATA ENTRY  MAKU  " + juce::String(m_maku.ui().part[size_t(f)].data()), m_encArea.getX(), y0, kRightW);
        label("VERBS  (HOLD)", m_encArea.getX(), y0 + 24 + 2 * 104 + 12, kRightW);
    } else {
        label("DATA ENTRY  " + juce::String(kEncPageNames[m_encPage]).toUpperCase() + "  T" + juce::String(m_track + 1), m_encArea.getX(), y0, kRightW);
        label("PAGE", m_encArea.getX(), y0 + 24 + 2 * 104 + 12, kRightW);
    }
    label("STEP PAGE", kMargin, m_trigArea.getY() - 4, kLeftW);
    label(gridMode() ? "TRIG  -  GRID: WRITE NOTES" : "TRIG  -  EUCLID: HITS AND MUTES", m_trigArea.getX(), m_trigArea.getY() - 18, m_trigArea.getWidth());
    label(m_makuShown ? "KEY  FLAT / SHARP" : "OCTAVE", kMargin, m_keysArea.getY() + 2, kLeftW);

    // encoder names and values
    for (int k = 0; k < 8; ++k) {
        const auto r = m_enc[size_t(k)].getBounds();
        const bool en = m_enc[size_t(k)].isEnabled();
        g.setColour(en ? colour::silk : colour::silkDim);
        g.setFont(font(11.5f, true));
        g.drawText(juce::String::charToString(juce::juce_wchar('A' + k)) + "  " + encoderLabel(k), r.getX() - 8, r.getY() - 18, r.getWidth() + 16, 14, juce::Justification::centred, false);
        if (en) {
            juce::String v;
            if (auto* p = m_proc.apvts.getParameter(m_enc[size_t(k)].getProperties()["pid"].toString())) v = p->getCurrentValueAsText().toUpperCase();
            else if (m_makuShown) v = juce::String(int(std::lround(m_enc[size_t(k)].getValue())));
            g.setColour(colour::led.withAlpha(0.95f));
            g.setFont(font(12.0f, true));
            g.drawText(v, r.getX() - 8, r.getBottom() + 2, r.getWidth() + 16, 14, juce::Justification::centred, false);
        }
    }

    // LCD bezel and screen
    g.setColour(colour::bezel);
    g.fillRoundedRectangle(m_bezel.toFloat(), 8.0f);
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawRoundedRectangle(m_bezel.toFloat().reduced(0.5f), 8.0f, 1.0f);
    g.setColour(lcd::paper);
    g.fillRect(m_lcd);
    if (m_seqShown) {   // the SEQ screen's status line
        LcdCanvas cv(m_lcd.getWidth() / kScale, 14);
        cv.fillRect(0, 0, 62, 14);
        cv.text(bold(), ("SEQ T" + juce::String(m_track + 1)).toRawUTF8(), 4, 3, false);
        cv.text(small(), m_transport.toRawUTF8(), cv.width() - 4 - LcdCanvas::textWidth(small(), m_transport.toRawUTF8()), 5);
        cv.dotsH(0, cv.width() - 1, 13);
        cv.draw(g, m_lcd.getX(), m_lcd.getY());
    }

    // transport window
    {
        auto r = m_transportArea.toFloat().reduced(0.0f, 4.0f);
        g.setColour(colour::bezel);
        g.fillRoundedRectangle(r, 6.0f);
        const bool playing = m_proc.generator() && m_proc.generator()->uiPlaying.load();
        auto inner = r.reduced(14.0f, 10.0f);
        g.setColour(playing ? colour::ledGreen : colour::silkDim);
        g.setFont(font(26.0f, true));
        g.drawText(m_transport, inner.removeFromTop(34.0f).toNearestInt(), juce::Justification::centredLeft, false);
        g.setColour(colour::silkDim);
        g.setFont(font(11.0f, true).withExtraKerningFactor(0.2f));
        g.drawText(juce::String(m_proc.hostBpm(), 1) + " BPM", inner.removeFromTop(16.0f).toNearestInt(), juce::Justification::centredLeft, false);
        if (m_recKey.getToggleState()) {
            g.setColour(colour::led);
            g.drawText("REC  T" + juce::String(m_track + 1) + (gridMode() ? "" : "  (SOLO IN GRID)"), inner.removeFromTop(16.0f).toNearestInt(), juce::Justification::centredLeft, false);
        }
        if (m_proc.makuAvailable()) {
            const auto& u = m_maku.ui();
            const bool on = m_proc.apvts.getRawParameterValue(maku::kOnId)->load() >= 0.5f;
            inner.removeFromTop(8.0f);
            g.setColour(on && u.running ? colour::ledGreen : colour::silkDim);
            g.setFont(font(13.0f, true).withExtraKerningFactor(0.15f));
            g.drawText(!on ? juce::String("MAKU OFF") : "MAKU  " + juce::String(u.running ? "PLAY" : u.extClock ? "WAITS FOR PLAY" : "STOP"),
                       inner.removeFromTop(18.0f).toNearestInt(), juce::Justification::centredLeft, false);
            g.setColour(colour::silkDim);
            g.setFont(font(11.0f, true).withExtraKerningFactor(0.2f));
            if (u.world > 0)
                g.drawText("WORLD " + juce::String(u.world) + "   " + juce::String(mnm::gen::kNoteNames[juce::jlimit(0, 11, u.root)]) + " " + maku::kMakuScaleNames[u.scale & 15]
                       + "   " + juce::String(u.bpm) + " BPM", inner.removeFromTop(16.0f).toNearestInt(), juce::Justification::centredLeft, false);
        }
    }

    // hint
    g.setColour(colour::silkDim);
    g.setFont(font(12.0f, true));
    const juce::String hint = m_hint.isNotEmpty() ? m_hint
        : m_makuShown ? juce::String("MAKU: i tasti della pagina sono gesti, tienili premuti (SINK, CASCADE, SWELL, WOBBLE, LIFT, SCRAMBLE), DRIFT lo fa muovere da solo, "
                                     "NEW WORLD cambia mondo, LIKE lo ricorda. PULSE..VOICE = la parte a fuoco; la tastiera la suona. FLAT/SHARP = tonalita' per quinte.")
        : gridMode() ? juce::String("GRID: click su un tasto trig = nuova nota (o la seleziona); di nuovo = la toglie. Trascina su/giu' = nota, "
                                    "a destra = durata. La tastiera scrive la nota nello step selezionato; con REC avanza da sola.")
                     : juce::String("EUCLID: il sequencer genera ritmo e note. I tasti trig mostrano i colpi (click = mute). "
                                    "Premi GRID per scrivere le note a mano.");
    g.drawFittedText(hint.toUpperCase(), m_hintArea.reduced(0, 8), juce::Justification::topLeft, 5, 1.0f);
}

void EuclidView::paintOverChildren(juce::Graphics& g)
{
    // glass: a soft reflection over the top of the screen
    juce::ColourGradient glass(juce::Colours::white.withAlpha(0.07f), 0.0f, float(m_lcd.getY()),
                               juce::Colours::white.withAlpha(0.0f), 0.0f, float(m_lcd.getY() + m_lcd.getHeight() / 3), false);
    g.setGradientFill(glass);
    g.fillRect(m_lcd);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawRect(m_lcd.expanded(1), 2);
}

void EuclidView::showSequencer(bool seq)
{
    leaveMaku();
    leaveMix();
    m_seqShown = seq;
    m_synth.setVisible(!seq);
    m_seq.setVisible(seq);
    for (juce::Component* c : {static_cast<juce::Component*>(&m_seed), static_cast<juce::Component*>(&m_loop), static_cast<juce::Component*>(&m_evolve),
                               static_cast<juce::Component*>(&m_root), static_cast<juce::Component*>(&m_scale), static_cast<juce::Component*>(&m_run),
                               static_cast<juce::Component*>(&m_dice)})
        c->setVisible(seq);
    m_synthKey.setToggleState(!seq, juce::dontSendNotification);
    m_seqKey.setToggleState(seq, juce::dontSendNotification);
    bindEncoders();
    repaint();
}

void EuclidView::timerCallback()
{
    if (m_synth.selectedTrack() != m_lastSynthTrack) selectTrack(m_synth.selectedTrack());
    if (m_seqShown) m_seq.tick();
    if (m_makuShown || m_proc.makuAvailable()) m_maku.tick();
    if (m_mixShown) m_mix.tick();
    bindEncoders();   // follows a machine change (MAKU: the focused part)
    if (m_makuShown) {   // the encoders follow MAKU (DRIFT moves the macros too), except the one in the hand
        const auto& u = m_maku.ui();
        const int f = juce::jlimit(0, maku::kParts - 1, u.focus);
        for (int k = 0; k < 7; ++k) {
            auto& e = m_enc[size_t(k)];
            if (e.isMouseButtonDown()) continue;
            const int v = k < 4 ? u.level[size_t(k)] : u.macro[size_t(f)][size_t(k - 4)];
            if (int(std::lround(e.getValue())) != v) e.setValue(v, juce::dontSendNotification);
        }
    }

    auto* gen = m_proc.generator();
    const bool playing = gen && gen->uiPlaying.load();
    juce::String tr = "STOP";
    if (playing) {
        const double ppq = juce::jmax(0.0, gen->uiPpq.load());
        tr = "PLAY " + juce::String(int(ppq / 4.0) + 1) + "." + juce::String(int(std::fmod(ppq, 4.0)) + 1);
    }
    if (tr != m_transport) { m_transport = tr; repaint(m_transportArea); repaint(m_statusArea); }

    m_makuOnKey.setLed(m_proc.makuAvailable() && m_proc.apvts.getRawParameterValue(maku::kOnId)->load() >= 0.5f ? 1.0f : 0.0f);
    m_makuKey.setLed(m_makuShown ? 1.0f : 0.0f);
    m_mixKey.setLed(m_mixShown ? 1.0f : 0.0f);
    if (m_mixShown) {   // the mute keys: lit = muted (MAKU ON: lit = on)
        for (int i = 0; i < 7; ++i) {
            const juce::String id = i < 6 ? muteId(i) : juce::String(maku::kOnId);
            const bool v = m_proc.apvts.getRawParameterValue(id)->load() >= 0.5f;
            m_pageKeys[size_t(i)]->setToggleState(false, juce::dontSendNotification);
            m_pageKeys[size_t(i)]->setLed(v ? 1.0f : 0.0f);
        }
        for (int t = 0; t < 6; ++t) m_trackKeys[size_t(t)]->setLed(juce::jmin(1.0f, m_proc.trackPeak(t) * 2.0f));
        m_trigs.tick();
        repaint(m_encArea.withHeight(m_encArea.getHeight() + 10));
        repaint(m_transportArea);
        return;
    }
    if (m_makuShown) {   // MAKU's LEDs: the part in focus, the verbs lit while they act and breathing while they fade
        const auto& u = m_maku.ui();
        const float blink = (juce::Time::getMillisecondCounter() / 400) % 2 == 0 ? 1.0f : 0.4f;
        for (int t = 0; t < 6; ++t)
            m_trackKeys[size_t(t)]->setLed(t < maku::kParts ? (t == u.focus ? 1.0f : 0.15f * float(u.level[size_t(t)]) / 127.0f) : 0.0f);
        for (int t = 0; t < 6; ++t) m_trackKeys[size_t(t)]->setToggleState(false, juce::dontSendNotification);
        for (int i = 0; i < NumEncPages; ++i) {
            const int b = maku::kVerbs[i].button;
            float led = ((u.litButtons >> b) & 1u) ? 1.0f : ((u.breathButtons >> b) & 1u) ? 0.45f * blink : 0.0f;
            if (b == maku::BPlay) led = u.drift ? 1.0f : 0.0f;
            if (b == maku::BRec) led = u.liked ? 1.0f : 0.0f;
            if (m_pageKeys[size_t(i)]->isDown()) led = 1.0f;
            m_pageKeys[size_t(i)]->setToggleState(false, juce::dontSendNotification);
            m_pageKeys[size_t(i)]->setLed(led);
        }
        m_trigs.tick();
        repaint(m_encArea.withHeight(m_encArea.getHeight() + 10));
        repaint(m_transportArea);
        return;
    }
    // LEDs
    for (int t = 0; t < 6; ++t) {
        const int n = gen ? gen->ui[size_t(t)].trigs.load() : 0;
        if (n != m_lastTrigs[size_t(t)]) { m_lastTrigs[size_t(t)] = n; m_trackFlash[size_t(t)] = 1.0f; }
        else m_trackFlash[size_t(t)] *= 0.7f;
        m_trackKeys[size_t(t)]->setLed(juce::jmax(m_trackFlash[size_t(t)], juce::jmin(1.0f, m_proc.trackPeak(t) * 2.0f)));
    }
    for (int i = 0; i < NumEncPages; ++i) m_pageKeys[size_t(i)]->setToggleState(i == m_encPage, juce::dontSendNotification);
    m_gridKey.setToggleState(gridMode(), juce::dontSendNotification);
    const int playStep = playing ? gen->ui[size_t(m_track)].step.load() : -1;
    const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
    m_stepPage1.setLed(m_trigs.page == 0 ? 1.0f : (playStep >= 0 && playStep < 16 && blink ? 0.5f : 0.0f));
    m_stepPage2.setLed(m_trigs.page == 1 ? 1.0f : (playStep >= 16 && blink ? 0.5f : 0.0f));
    m_recKey.setLed(m_recKey.getToggleState() ? (playing && blink ? 0.6f : 1.0f) : 0.0f);

    // REC: notes played in from the host
    MnmOneProcessor::RecordedNote rec;
    while (m_proc.popRecorded(rec)) {
        if (!gridMode()) continue;
        if (rec.playing && rec.step >= 0) {
            const uint32_t c = m_proc.gridCell(m_track, rec.step);
            m_proc.setGridCell(m_track, rec.step, gridCell(true, rec.note, gridTrig(c) ? gridLength(c) : m_edit.lastLength));
            m_edit.step[size_t(m_track)] = rec.step;
        } else {
            writeNote(rec.note, true);
        }
    }

    // the keyboard shows the selected step's note
    const uint32_t c = m_proc.gridCell(m_track, m_edit.step[size_t(m_track)]);
    const int hi = gridMode() && gridTrig(c) ? gridNote(c) : -1;
    if (hi != m_keys.highlighted) { m_keys.highlighted = hi; m_keys.repaint(); }
    m_trigs.tick();
    repaint(m_encArea.withHeight(m_encArea.getHeight() + 10));
}

} // namespace mnm::plugin::one
