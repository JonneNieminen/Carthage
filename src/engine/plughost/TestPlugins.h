#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace carthage::engine::plughost
{
    /** The test plugins (Protocol.h, testPlugin), so the tests can run the same plugin in this
        process and in carthage-plughost without any third-party plugin. Never offered to users:
        only the host and the tests register the format. "Crasher" and "CrashOnLoad" take their
        process down (5f-4's tests): never load them anywhere but in a host. */
    std::unique_ptr<juce::AudioPluginInstance> createTestPlugin (const juce::PluginDescription&, juce::String& error);

    /** The format "CarthageTest", for an AudioPluginFormatManager (tests). */
    class TestPluginFormat final : public juce::AudioPluginFormat
    {
    public:
        juce::String getName() const override   { return "CarthageTest"; }
        void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String&) override;
        bool fileMightContainThisPluginType (const juce::String& id) override   { return id == "Half" || id == "Crasher" || id == "CrashOnLoad"; }
        juce::String getNameOfPluginFromIdentifier (const juce::String& id) override   { return id; }
        bool pluginNeedsRescanning (const juce::PluginDescription&) override   { return false; }
        bool doesPluginStillExist (const juce::PluginDescription&) override   { return true; }
        bool canScanForPlugins() const override   { return false; }
        bool isTrivialToScan() const override     { return true; }
        juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override   { return {}; }
        juce::FileSearchPath getDefaultLocationsToSearch() override   { return {}; }

    private:
        void createPluginInstance (const juce::PluginDescription&, double, int, PluginCreationCallback) override;
        bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override   { return false; }
    };
}
