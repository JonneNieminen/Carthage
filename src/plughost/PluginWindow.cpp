#include "PluginWindow.h"

namespace carthage::plughost
{
    PluginWindow::PluginWindow (juce::AudioPluginInstance& instance, const juce::String& title, juce::Colour background,
                                std::function<void()> closed, std::function<void (const juce::KeyPress&)> key)
        : juce::DocumentWindow (title, background, juce::DocumentWindow::closeButton),
          onClosed (std::move (closed)), onKey (std::move (key))
    {
        setUsingNativeTitleBar (true);
        juce::AudioProcessorEditor* editor = instance.hasEditor() ? instance.createEditorAndMakeActive() : nullptr;

        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (instance);   // owned by the window below

        // The editor's size, followed when it resizes itself; never one of ours (as in the app).
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        setTopLeftPosition (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt().getCentre()
                            - juce::Point<int> (getWidth() / 2, getHeight() / 2));
        setWantsKeyboardFocus (true);
    }

    PluginWindow::~PluginWindow()
    {
        clearContentComponent();   // the editor goes before its window
    }

    void PluginWindow::closeButtonPressed()
    {
        if (onClosed)
            onClosed();
    }

    bool PluginWindow::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress ('w', juce::ModifierKeys::commandModifier, 0))
        {
            closeButtonPressed();
            return true;
        }

        if (onKey)
            onKey (key);

        return true;
    }
}
