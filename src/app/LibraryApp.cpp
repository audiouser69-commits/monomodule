// Monomodule Library: standalone app entry point.
#include <juce_gui_extra/juce_gui_extra.h>
#include "LibraryComponent.h"
#include "WindowScale.h"

namespace mnm::app {

namespace scale = mnm::plugin::scale;

// The library's layout scaled as a whole (WindowScale.h): the window resizes from its edges with its aspect ratio
// locked, and opens at the scale shared by every Monomodule window.
class MainWindow : public juce::DocumentWindow {
public:
    MainWindow() : juce::DocumentWindow("Monomodule Library", mnm::plugin::skin::paperColour(), juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar(true);
        auto holder = std::make_unique<scale::ScaledHolder>(std::make_unique<LibraryComponent>(std::make_unique<mnm::library::Store>()));
        const auto base = holder->base();
        const auto size = scale::scaled(base, scale::fitScreen(scale::load(), base));
        holder->setSize(size.x, size.y);
        m_aspect.setBase(base);
        setConstrainer(&m_constrainer);
        setResizable(true, false);
        setContentOwned(holder.release(), true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    // JUCE constrains a top-level window with its native frame included: the ratio is the one inside the title bar
    struct FrameConstrainer final : juce::BorderedComponentBoundsConstrainer {
        explicit FrameConstrainer(MainWindow& w) : window(w) {}
        juce::ComponentBoundsConstrainer* getWrappedConstrainer() const override { return &window.m_aspect; }
        juce::BorderSize<int> getAdditionalBorder() const override
        {
            if (auto* peer = window.getPeer())
                if (const auto frame = peer->getFrameSizeIfPresent()) return *frame;
            return {};
        }
        MainWindow& window;
    };
    scale::AspectConstrainer m_aspect;
    FrameConstrainer m_constrainer{*this};
};

class LibraryApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Monomodule Library"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String&) override { m_window = std::make_unique<MainWindow>(); }
    void shutdown() override { m_window.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    std::unique_ptr<MainWindow> m_window;
};

} // namespace mnm::app

START_JUCE_APPLICATION(mnm::app::LibraryApplication)
