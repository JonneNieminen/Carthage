#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <functional>
#include <memory>
#include <optional>

namespace carthage::app
{
    /** In-app updates behind one interface (brief, "Updates and releases"; DECISIONS F10): Sparkle 2
        on macOS (platform/Updater_mac.mm), WinSparkle on Windows (platform/Updater_win.cpp).

        Rules it keeps (docs/RELEASING.md, "Update behaviour in the app"):
        - a background check on launch, with no dialog; a failed background check is silent;
        - an update found is offered through the app's own banner, never a modal window;
        - "Install on quit" is the default; "Restart now" is refused while Hooks::isBusy() is true
          (playback or recording), and asks Hooks::prepareToRestart() before restarting;
        - "Receive beta updates" switches between the stable and the beta feed.

        All methods and every hook: message thread.
    */
    class Updater
    {
    public:
        /** Where an offered update has got to. */
        enum class Stage
        {
            found,        ///< known, not downloaded; the user has not chosen yet
            downloading,  ///< the user chose; Sparkle is fetching and checking it
            ready,        ///< downloaded and verified: installs when Carthage quits, or now on request
            installing    ///< being installed; Carthage is about to relaunch
        };

        struct Offer
        {
            juce::String version;   ///< as the release names it, "0.1.1"
            juce::String notes;     ///< plain text, one change per line
            Stage stage = Stage::found;
            double progress = 0.0;  ///< 0..1 while downloading
            bool installOnQuitChosen = false;
            bool canInstallOnQuit = true;   ///< false where the platform updater can not (WinSparkle)
        };

        struct Hooks
        {
            /** True while playing or recording: no restart then. */
            std::function<bool()> isBusy;

            /** Before a restart: save, or ask; return false to cancel the restart. */
            std::function<bool()> prepareToRestart;

            /** The banner: show or update an offer, or hide it (nullopt). */
            std::function<void (std::optional<Offer>)> showOffer;

            /** A short message for the status line, only for checks the user asked for, or errors
                while installing. */
            std::function<void (const juce::String&, bool isWarning)> showMessage;
        };

        /** The platform's updater. When this build can not update (a developer build, or no public
            key compiled in), it still exists and says so when asked to check. */
        static std::unique_ptr<Updater> create (Hooks);

        virtual ~Updater() = default;

        /** False for builds that can not update; the reason is in whyUnavailable(). */
        virtual bool isAvailable() const = 0;
        virtual juce::String whyUnavailable() const = 0;

        /** Quietly, on launch. */
        virtual void checkInBackground() = 0;

        /** "Check for updates…": says what it found, including "up to date". */
        virtual void checkNow() = 0;

        /** The banner's two choices, and closing it. */
        virtual void installOnQuit() = 0;
        virtual void restartNow() = 0;
        virtual void dismiss() = 0;

        /** "Receive beta updates" (remembered). */
        bool getReceivesBetaUpdates() const;
        void setReceivesBetaUpdates (bool);

        /** The feed this build reads now. */
        juce::String getFeedUrl() const;

    protected:
        explicit Updater (Hooks);
        Hooks hooks;

    private:
        std::unique_ptr<juce::PropertiesFile> settings;
    };
}
