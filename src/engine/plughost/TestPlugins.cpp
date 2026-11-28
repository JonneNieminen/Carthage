#include "TestPlugins.h"

#include "Protocol.h"

#include <atomic>
#include <cstring>

namespace carthage::engine::plughost
{
    namespace
    {
        /** Half's one parameter: its gain, 0 to 1. */
        class Gain final : public juce::AudioPluginInstance::HostedParameter
        {
        public:
            float getValue() const override             { return value.load(); }
            void setValue (float v) override            { value.store (juce::jlimit (0.0f, 1.0f, v)); }
            float getDefaultValue() const override      { return 0.5f; }
            juce::String getName (int maxLength) const override   { return juce::String ("Gain").substring (0, maxLength); }
            juce::String getLabel() const override      { return {}; }
            juce::String getText (float v, int) const override   { return juce::String (v, 2); }
            float getValueForText (const juce::String& text) const override   { return text.getFloatValue(); }
            juce::String getParameterID() const override { return "gain"; }

        private:
            std::atomic<float> value { 0.5f };
        };

        /** "Half" (Protocol.h). */
        class Half final : public juce::AudioPluginInstance
        {
        public:
            Half()
                : juce::AudioPluginInstance (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                                              .withOutput ("Out", juce::AudioChannelSet::stereo()))
            {
                auto param = std::make_unique<Gain>();
                gain = param.get();
                addHostedParameter (std::move (param));
            }

            void fillInPluginDescription (juce::PluginDescription& d) const override   { d = testPlugin ("Half"); }

            const juce::String getName() const override   { return "Half"; }
            void prepareToPlay (double, int) override {}
            void releaseResources() override {}

            void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
            {
                buffer.applyGain (gain->getValue());

                for (const auto m : midi)
                {
                    const auto message = m.getMessage();

                    if (message.isNoteOn() && buffer.getNumChannels() > 0)
                        buffer.addSample (0, m.samplePosition, 1.0f);

                    // Controller 7 moves the gain as the plugin's own window would: the host hears it.
                    if (message.isControllerOfType (7))
                        gain->setValueNotifyingHost (static_cast<float> (message.getControllerValue()) / 127.0f);
                }
            }

            double getTailLengthSeconds() const override   { return 0.0; }
            bool acceptsMidi() const override              { return true; }
            bool producesMidi() const override             { return false; }
            juce::AudioProcessorEditor* createEditor() override   { return nullptr; }
            bool hasEditor() const override                { return false; }
            int getNumPrograms() override                  { return 1; }
            int getCurrentProgram() override               { return 0; }
            void setCurrentProgram (int) override {}
            const juce::String getProgramName (int) override   { return {}; }
            void changeProgramName (int, const juce::String&) override {}

            void getStateInformation (juce::MemoryBlock& data) override
            {
                const auto g = gain->getValue();
                data.replaceAll (&g, sizeof (g));
                data.append (extra.getData(), extra.getSize());
            }

            void setStateInformation (const void* data, int size) override
            {
                if (size < static_cast<int> (sizeof (float)))
                    return;

                float g = 0.0f;
                std::memcpy (&g, data, sizeof (g));
                gain->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, g));
                extra.replaceAll (static_cast<const char*> (data) + sizeof (float), static_cast<size_t> (size) - sizeof (float));
            }

        private:
            Gain* gain = nullptr;
            juce::MemoryBlock extra;
        };
    }

    std::unique_ptr<juce::AudioPluginInstance> createTestPlugin (const juce::PluginDescription& d, juce::String& error)
    {
        if (d.pluginFormatName == testFormat && d.name == "Half")
            return std::make_unique<Half>();

        error = "no test plugin called " + d.name;
        return {};
    }

    void TestPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& found, const juce::String& id)
    {
        if (id == "Half")
            found.add (new juce::PluginDescription (testPlugin ("Half")));
    }

    void TestPluginFormat::createPluginInstance (const juce::PluginDescription& d, double, int, PluginCreationCallback callback)
    {
        juce::String error;
        auto instance = createTestPlugin (d, error);
        callback (std::move (instance), error);
    }
}
