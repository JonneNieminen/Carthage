#include "Updater.h"
#include "Paths.h"

namespace carthage::app
{
    Updater::Updater (Hooks h)
        : hooks (std::move (h))
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Updates";
        options.filenameSuffix = ".settings";
        options.folderName = {};
        options.commonToAllUsers = false;
        settings = std::make_unique<juce::PropertiesFile> (Paths::settings().getChildFile ("Updates.settings"), options);
    }

    bool Updater::getReceivesBetaUpdates() const
    {
        return settings->getBoolValue ("receiveBetaUpdates", false);
    }

    void Updater::setReceivesBetaUpdates (bool beta)
    {
        settings->setValue ("receiveBetaUpdates", beta);
        settings->saveIfNeeded();
    }

    juce::String Updater::getFeedUrl() const
    {
        // Stable: the latest release's appcast. GitHub's "latest" never points at a pre-release,
        // which keeps betas off it. Beta: a fixed release that every release updates
        // (docs/RELEASING.md, "Feeds").
        return getReceivesBetaUpdates() ? CARTHAGE_FEED_BETA : CARTHAGE_FEED_STABLE;
    }
}
