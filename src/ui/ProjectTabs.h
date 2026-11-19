#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <engine/Session.h>

namespace carthage::ui
{
    /** The open projects as tabs (phase 4b, W30), across the top of the window. The front tab
        takes the toolbar's surface, so it reads as joined to the controls below it; the others sit
        back on the well. An unsaved project shows a dot, which turns into the close cross under the
        pointer. A click brings a tab forward, a middle click or the cross closes it, a drag moves
        it, and + starts a new project. A double-click, or Rename in the right-click menu, renames
        the project (owner's list F22).

        It only draws what the session reports (describeProject); the window decides what a click
        does, so closing goes through the same ask-to-save path as Cmd/Ctrl+W. */
    class ProjectTabs final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit ProjectTabs (engine::Session&);

        static constexpr int height = 32;

        std::function<void (int)> onSelect, onClose, onRename, onNotes;
        std::function<void()> onNew;

        /** The session's projects changed (added, closed, saved, edited, moved). */
        void refresh();

        void paint (juce::Graphics&) override;
        void resized() override   { layout(); }
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        engine::Session& session;

        struct Tab
        {
            engine::Session::ProjectSummary summary;
            juce::Rectangle<int> bounds;
        };

        std::vector<Tab> tabs;
        juce::Rectangle<int> plus;
        int hovered = -1, pressed = -1, dragTo = -1;
        bool overClose = false, overPlus = false;

        void layout();
        int tabAt (juce::Point<int>) const;
        juce::Rectangle<int> closeArea (int index) const;
        void updateHover (juce::Point<int>);
    };
}
