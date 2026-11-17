// WinSparkle behind app::Updater (docs/RELEASING.md, "Update behaviour in the app").
//
// WinSparkle checks quietly on launch and reports through callbacks on its own thread; Carthage
// shows its banner on the message thread. WinSparkle can not install on quit: its installer runs
// from its own window, so the banner offers "Install now", which opens that window, and WinSparkle
// asks Carthage to quit (refused while playing or recording). Offer::canInstallOnQuit is false.

#include "../Updater.h"

#include <juce_events/juce_events.h>
#include <winsparkle.h>

using namespace carthage::app;

namespace
{
    class WinSparkleUpdater;
    WinSparkleUpdater* instance = nullptr;   // WinSparkle's callbacks are plain functions

    class WinSparkleUpdater final : public Updater
    {
    public:
        explicit WinSparkleUpdater (Hooks h)
            : Updater (std::move (h))
        {
            if (CARTHAGE_BUILD_NUMBER == 0)
                reason = "This is a development build. Released builds update themselves.";
            else if (juce::String (CARTHAGE_UPDATE_PUBLIC_KEY).isEmpty())
                reason = "This build has no update key, so it can not check for updates.";

            if (reason.isNotEmpty())
                return;

            instance = this;
            win_sparkle_set_appcast_url (getFeedUrl().toRawUTF8());

            if (win_sparkle_set_eddsa_public_key (CARTHAGE_UPDATE_PUBLIC_KEY) == 0)
            {
                reason = "The update key in this build is not valid.";
                instance = nullptr;
                return;
            }

            win_sparkle_set_app_details (L"Jonne Nieminen", L"Carthage", juce::String (CARTHAGE_VERSION_STRING).toWideCharPointer());
            win_sparkle_set_app_build_version (juce::String (CARTHAGE_BUILD_NUMBER).toWideCharPointer());
            win_sparkle_set_registry_path ("Software\\Carthage\\Updates");
            win_sparkle_set_automatic_check_for_updates (0);   // Carthage checks on launch itself

            win_sparkle_set_can_shutdown_callback ([]() -> int
            {
                // WinSparkle's thread: ask the message thread, and wait for the answer.
                auto busy = juce::MessageManager::callSync ([] { return instance != nullptr && instance->busy(); });
                return busy.value_or (true) ? 0 : 1;
            });

            win_sparkle_set_shutdown_request_callback ([]
            {
                juce::MessageManager::callAsync ([] { juce::JUCEApplicationBase::quit(); });
            });

            win_sparkle_set_did_find_update_callback ([]
            {
                juce::MessageManager::callAsync ([] { if (instance != nullptr) instance->found(); });
            });

            win_sparkle_set_did_not_find_update_callback ([]
            {
                juce::MessageManager::callAsync ([] { if (instance != nullptr) instance->notFound(); });
            });

            win_sparkle_set_error_callback ([]
            {
                juce::MessageManager::callAsync ([] { if (instance != nullptr) instance->failed(); });
            });

            win_sparkle_init();
            initialised = true;
        }

        ~WinSparkleUpdater() override
        {
            if (initialised)
                win_sparkle_cleanup();

            instance = nullptr;
        }

        bool isAvailable() const override               { return initialised; }
        juce::String whyUnavailable() const override     { return reason; }

        void checkInBackground() override
        {
            if (initialised)
            {
                userAsked = false;
                win_sparkle_check_update_without_ui();
            }
        }

        void checkNow() override
        {
            if (! initialised)
            {
                say (reason, false);
                return;
            }

            userAsked = true;
            win_sparkle_check_update_without_ui();
        }

        void installOnQuit() override    { restartNow(); }   // not possible here; the banner does not offer it

        void restartNow() override
        {
            if (! initialised)
                return;

            if (busy())
            {
                say ("Stop playback and recording first.", true);
                return;
            }

            if (hooks.prepareToRestart && ! hooks.prepareToRestart())
                return;

            // WinSparkle's window shows the notes, downloads, checks the signature and runs the
            // installer; it asks Carthage to quit through the callbacks above.
            win_sparkle_check_update_with_ui_and_install();
        }

        void dismiss() override
        {
            if (hooks.showOffer)
                hooks.showOffer (std::nullopt);
        }

    private:
        juce::String reason;
        bool initialised = false, userAsked = false;

        bool busy() const    { return hooks.isBusy && hooks.isBusy(); }

        void found()
        {
            // WinSparkle's callback carries no details; its window shows the notes.
            Offer o;
            o.version = "A new version";
            o.notes = "Install it to see what changed.";
            o.canInstallOnQuit = false;

            if (hooks.showOffer)
                hooks.showOffer (o);

            userAsked = false;
        }

        void notFound()
        {
            if (userAsked)
                say ("Carthage " + juce::String (CARTHAGE_VERSION_STRING) + " is up to date.", false);

            userAsked = false;
        }

        void failed()
        {
            if (userAsked)
                say ("Could not check for updates.", true);

            userAsked = false;
        }

        void say (const juce::String& text, bool warning)
        {
            if (hooks.showMessage)
                hooks.showMessage (text, warning);
        }
    };
}

std::unique_ptr<Updater> Updater::create (Hooks hooks)
{
    return std::make_unique<WinSparkleUpdater> (std::move (hooks));
}
