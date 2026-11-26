#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace carthage::engine::plughost
{
    /** What the app and `carthage-plughost` say to each other over JUCE's child-process pipe (5f,
        W65). Every message is a ValueTree of one type; a request carries an `id` and its answer
        is a `reply` with the same id, `ok` and, when not, an `error`.

        load (description, sampleRate, maxBlock) → reply (slot, name, numIns, numOuts, latency)
        connect (slot, memory, wake, bytes)      → reply: the host has attached and is rendering
        getState (slot)                          → reply (state, base64)
        setState (slot, state)                   → reply
        unload (slot)                            → reply */
    inline constexpr const char* commandLineID = "carthage-plughost";

    namespace msg
    {
        inline const juce::Identifier load ("load"), connect ("connect"), getState ("getState"),
                                      setState ("setState"), unload ("unload"), reply ("reply");

        inline const juce::Identifier id ("id"), ok ("ok"), error ("error"), slot ("slot"), description ("description"),
                                      sampleRate ("sampleRate"), maxBlock ("maxBlock"), name ("name"), numIns ("numIns"),
                                      numOuts ("numOuts"), latency ("latency"), memory ("memory"), wake ("wake"),
                                      bytes ("bytes"), state ("state");
    }

    juce::MemoryBlock toBlock (const juce::ValueTree&);
    juce::ValueTree fromBlock (const juce::MemoryBlock&);

    /** The host's own test plugins, so the tests need no third-party plugin: format "CarthageTest".
        "Half": stereo in and out at half the level, and each note-on adds 1 to the first output at
        its sample (MIDI timing). */
    inline constexpr const char* testFormat = "CarthageTest";
    juce::PluginDescription testPlugin (const juce::String& name);
}
