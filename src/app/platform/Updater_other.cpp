// Platforms without an updater (none ship yet; Linux is not a target). The app still has an Updater,
// which says why it can not check.

#include "../Updater.h"

using namespace carthage::app;

namespace
{
    class NoUpdater final : public Updater
    {
    public:
        explicit NoUpdater (Hooks h) : Updater (std::move (h)) {}

        bool isAvailable() const override               { return false; }
        juce::String whyUnavailable() const override     { return "Carthage can not update itself on this system."; }
        void checkInBackground() override {}
        void checkNow() override                         { if (hooks.showMessage) hooks.showMessage (whyUnavailable(), false); }
        void installOnQuit() override {}
        void restartNow() override {}
        void dismiss() override {}
    };
}

std::unique_ptr<Updater> Updater::create (Hooks hooks)
{
    return std::make_unique<NoUpdater> (std::move (hooks));
}
