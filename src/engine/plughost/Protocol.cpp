#include "Protocol.h"

namespace carthage::engine::plughost
{
    juce::MemoryBlock toBlock (const juce::ValueTree& tree)
    {
        juce::MemoryBlock block;
        juce::MemoryOutputStream out (block, false);
        tree.writeToStream (out);
        out.flush();
        return block;
    }

    juce::ValueTree fromBlock (const juce::MemoryBlock& block)
    {
        return juce::ValueTree::readFromData (block.getData(), block.getSize());
    }

    juce::String valuesToText (const juce::Array<float>& values)
    {
        juce::MemoryBlock block (values.begin(), static_cast<size_t> (values.size()) * sizeof (float));
        return block.toBase64Encoding();
    }

    juce::Array<float> valuesFromText (const juce::String& text)
    {
        juce::MemoryBlock block;
        juce::Array<float> values;

        if (block.fromBase64Encoding (text))
            values.addArray (static_cast<const float*> (block.getData()), static_cast<int> (block.getSize() / sizeof (float)));

        return values;
    }

    /** What the app needs to stand in for the plugin: its buses and parameters. */
    void describe (juce::AudioPluginInstance& p, juce::ValueTree& reply)
    {
        reply.setProperty (msg::name, p.getName(), nullptr);
        reply.setProperty (msg::numIns, p.getTotalNumInputChannels(), nullptr);
        reply.setProperty (msg::numOuts, p.getTotalNumOutputChannels(), nullptr);
        reply.setProperty (msg::latency, p.getLatencySamples(), nullptr);
        reply.setProperty (msg::tail, p.getTailLengthSeconds(), nullptr);
        reply.setProperty (msg::acceptsMidi, p.acceptsMidi(), nullptr);
        reply.setProperty (msg::producesMidi, p.producesMidi(), nullptr);
        reply.setProperty (msg::hasEditor, p.hasEditor(), nullptr);

        for (const bool isInput : { true, false })
            for (int i = 0; i < p.getBusCount (isInput); ++i)
                if (auto* bus = p.getBus (isInput, i))
                {
                    juce::ValueTree b (msg::bus);
                    b.setProperty (msg::input, isInput, nullptr);
                    b.setProperty (msg::name, bus->getName(), nullptr);
                    b.setProperty (msg::channels, bus->getNumberOfChannels(), nullptr);
                    b.setProperty (msg::enabled, bus->isEnabled(), nullptr);
                    reply.appendChild (b, nullptr);
                }

        const auto& parameters = p.getParameters();

        for (int i = 0; i < parameters.size(); ++i)
        {
            auto* param = parameters.getUnchecked (i);
            juce::ValueTree t (msg::parameter);

            if (param == nullptr)   // keeps the indices in step
            {
                reply.appendChild (t, nullptr);
                continue;
            }

            auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (param);
            t.setProperty (msg::paramID, hosted != nullptr ? hosted->getParameterID() : juce::String (i), nullptr);
            t.setProperty (msg::name, param->getName (256), nullptr);
            t.setProperty (msg::label, param->getLabel(), nullptr);
            t.setProperty (msg::defaultValue, param->getDefaultValue(), nullptr);
            t.setProperty (msg::value, param->getValue(), nullptr);
            t.setProperty (msg::steps, param->getNumSteps(), nullptr);
            t.setProperty (msg::discrete, param->isDiscrete(), nullptr);
            t.setProperty (msg::boolean, param->isBoolean(), nullptr);
            t.setProperty (msg::automatable, param->isAutomatable(), nullptr);
            reply.appendChild (t, nullptr);
        }
    }

    juce::PluginDescription testPlugin (const juce::String& name)
    {
        juce::PluginDescription d;
        d.name = name;
        d.descriptiveName = name;
        d.pluginFormatName = testFormat;
        d.fileOrIdentifier = name;
        d.manufacturerName = "Carthage";
        d.category = "Test";
        d.numInputChannels = 2;
        d.numOutputChannels = 2;
        d.isInstrument = false;
        d.uniqueId = name.hashCode();
        return d;
    }
}
