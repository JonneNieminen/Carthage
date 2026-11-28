#pragma once

#include "Controls.h"

namespace carthage::ui
{
    /** Says that a plugin's process stopped (5f-4, W69): a small card floating over the bottom
        right, like the update banner, never a dialog. It names the plugins and says what Carthage
        did (started them again with their settings, or left one off after its third crash). It
        stays until closed, since a crash is worth knowing about; a later crash joins the same
        card. Nothing to choose: Carthage has done what there was to do. */
    class CrashBanner final : public juce::Component
    {
    public:
        CrashBanner();

        /** Adds a crash to the card and shows it. */
        void add (const juce::StringArray& crashed, const juce::StringArray& stoppedForGood, int others);

        void hide();

        int getPreferredHeight (int width) const;
        static constexpr int preferredWidth = 360;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        juce::StringArray crashedNames, stoppedNames;
        int otherPlugins = 0;

        juce::Label title, body;
        IconButton close { IconButton::Glyph::close };

        void refresh();
    };
}
