#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace carthage::engine::plughost
{
    /** What the app and `carthage-plughost` say to each other over JUCE's child-process pipe (5f,
        W65). Every message is a ValueTree of one type; a request carries an `id` and its answer
        is a `reply` with the same id, `ok` and, when not, an `error`.

        load (description, sampleRate, maxBlock) → reply (slot, name, numIns, numOuts, latency, tail,
                                                    acceptsMidi, producesMidi; a `bus` child per bus
                                                    and a `parameter` child per parameter)
        connect (slot, memory, wake, bytes, sampleRate) → reply: the host has attached and is
                                                    rendering (again, for a new block size or rate)
        getState (slot)                          → reply (state, base64; values)
        setState (slot, state)                   → reply (values: every parameter after it)
        unload (slot)                            → reply */
    inline constexpr const char* commandLineID = "carthage-plughost";

    namespace msg
    {
        inline const juce::Identifier load ("load"), connect ("connect"), getState ("getState"),
                                      setState ("setState"), unload ("unload"), reply ("reply");

        inline const juce::Identifier id ("id"), ok ("ok"), error ("error"), slot ("slot"), description ("description"),
                                      sampleRate ("sampleRate"), maxBlock ("maxBlock"), name ("name"), numIns ("numIns"),
                                      numOuts ("numOuts"), latency ("latency"), memory ("memory"), wake ("wake"),
                                      bytes ("bytes"), state ("state"), tail ("tail"), acceptsMidi ("acceptsMidi"),
                                      producesMidi ("producesMidi"), bus ("bus"), input ("input"), channels ("channels"),
                                      enabled ("enabled"), parameter ("parameter"), paramID ("paramID"),
                                      defaultValue ("defaultValue"), value ("value"), steps ("steps"), discrete ("discrete"),
                                      boolean ("boolean"), label ("label"), automatable ("automatable"), values ("values");
    }

    juce::MemoryBlock toBlock (const juce::ValueTree&);
    juce::ValueTree fromBlock (const juce::MemoryBlock&);

    /** What the app needs to stand in for a plugin, as load's reply carries it: name, channels,
        latency, tail, MIDI, a `bus` child per bus, a `parameter` child per parameter. */
    void describe (juce::AudioPluginInstance&, juce::ValueTree& into);

    /** Every parameter's value (0 to 1), as one property. */
    juce::String valuesToText (const juce::Array<float>&);
    juce::Array<float> valuesFromText (const juce::String&);

    /** The host's own test plugins, so the tests need no third-party plugin: format "CarthageTest".
        "Half": stereo in and out at the level of its one parameter, "Gain" (0.5 by default), and
        each note-on adds 1 to the first output at its sample (MIDI timing). Its state is the gain
        followed by whatever was last set. */
    inline constexpr const char* testFormat = "CarthageTest";
    juce::PluginDescription testPlugin (const juce::String& name);
}
