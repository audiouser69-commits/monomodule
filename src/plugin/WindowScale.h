// Window scaling (issue #7). Every Monomodule window (the plugin editors, the Library app) keeps the layout it was
// designed at, its base size, and shows it scaled as a whole: the window resizes with its aspect ratio locked, so no
// layout has to adapt to another shape. One scale is shared by all of them: the last scale a window was resized to is
// kept in the shared settings (uiScale), and every window opens at it. The plugins' editor is in ScaledEditor.h.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>
#include <memory>
#include "SharedSettings.h"

namespace mnm::plugin::scale {

constexpr double kMin = 0.5, kMax = 2.0;

inline double load()   // 1 until a window has been resized
{
    const double s = loadSharedSetting("uiScale", "1").getDoubleValue();
    return s > 0.0 ? juce::jlimit(kMin, kMax, s) : 1.0;
}
inline void save(double s) { saveSharedSetting("uiScale", juce::String(juce::jlimit(kMin, kMax, s), 3)); }

inline juce::Point<int> scaled(juce::Point<int> base, double s) { return {int(std::lround(base.x * s)), int(std::lround(base.y * s))}; }

// s, or less where a window of `base` size would not fit the main screen's work area (never below kMin)
inline double fitScreen(double s, juce::Point<int> base)
{
    if (const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        s = juce::jmin(s, 0.95 * d->userArea.getWidth() / base.x, 0.9 * d->userArea.getHeight() / base.y);
    return juce::jmax(kMin, s);
}

// A window's size: its base size's aspect ratio, from kMin to kMax times the base size.
class AspectConstrainer : public juce::ComponentBoundsConstrainer {
public:
    void setBase(juce::Point<int> base)
    {
        const auto lo = scaled(base, kMin), hi = scaled(base, kMax);
        setSizeLimits(lo.x, lo.y, hi.x, hi.y);
        setFixedAspectRatio(double(base.x) / double(base.y));
    }
};

// Shows one component, the view, laid out at its own size (the base size) and scaled to fill this one. The view may
// change its own size (the plugin editor's status lines): onBaseChanged then lets the owner resize, at the same scale.
// A new scale that lasts becomes the shared scale (a resize by the user); the scale a window opens at does not.
class ScaledHolder : public juce::Component, private juce::Timer {
public:
    explicit ScaledHolder(std::unique_ptr<juce::Component> view) : m_view(std::move(view)), m_base(m_view->getWidth(), m_view->getHeight())
    {
        addAndMakeVisible(*m_view);
    }
    ~ScaledHolder() override { if (isTimerRunning()) timerCallback(); }

    juce::Component& view() const { return *m_view; }
    juce::Point<int> base() const { return m_base; }
    double scale() const { return getWidth() / double(m_base.x); }
    std::function<void()> onBaseChanged;

    void resized() override
    {
        if (getWidth() <= 0 || getHeight() <= 0) return;
        // each axis fills the window exactly: the two differ by a rounded pixel at most
        m_view->setTransform(juce::AffineTransform::scale(float(getWidth()) / float(m_base.x), float(getHeight()) / float(m_base.y)));
        if (m_saved < 0.0) m_saved = scale();
        else if (std::abs(scale() - m_saved) > 0.001) startTimer(400);   // saved once the resizing has settled
    }
    void childBoundsChanged(juce::Component* child) override
    {
        if (child != m_view.get() || juce::Point<int>(m_view->getWidth(), m_view->getHeight()) == m_base) return;   // (setTransform lands here too)
        m_base = {m_view->getWidth(), m_view->getHeight()};
        if (onBaseChanged) onBaseChanged();
        resized();
    }

private:
    void timerCallback() override { stopTimer(); m_saved = scale(); save(m_saved); }

    std::unique_ptr<juce::Component> m_view;
    juce::Point<int> m_base;
    double m_saved = -1.0;   // the scale last saved, or the one the window opened at
};

} // namespace mnm::plugin::scale
