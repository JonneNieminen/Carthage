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
