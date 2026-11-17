#pragma once

#include "Controls.h"

#include <optional>

namespace carthage::ui
{
    /** The update banner (brief, "Updates and releases"): a small card that floats over the bottom
        right of the window, never a dialog. It shows what changed and two choices, "Install on
        quit" (the default, filled) and "Restart now", which is unavailable while playing or
        recording. The app's updater drives it through show(); the card only shows and asks. */
    class UpdateBanner final : public juce::Component
    {
    public:
        enum class Stage { found, downloading, ready, installing };

        struct Model
        {
            juce::String version;
            juce::String notes;              ///< plain text, one change per line
            Stage stage = Stage::found;
            double progress = 0.0;           ///< 0..1 while downloading
            bool installOnQuitChosen = false;
            bool canInstallOnQuit = true;    ///< false on Windows: WinSparkle installs from its window
        };

        UpdateBanner();

        /** nullopt hides the card. */
        void show (std::optional<Model>);

        /** Polled by the main component: "Restart now" waits while this is true. */
        void setBusy (bool playingOrRecording);

        std::function<void()> onInstallOnQuit, onRestartNow, onDismiss;

        /** The card's height for the current text, at the given width. */
        int getPreferredHeight (int width) const;

        static constexpr int preferredWidth = 360;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        std::optional<Model> model;
        bool busy = false;

        juce::Label title, notes, detail;
        juce::TextButton primary, secondary;
        IconButton close { IconButton::Glyph::close };

        void refresh();
    };
}
