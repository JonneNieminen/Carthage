#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <memory>

namespace carthage::engine
{
    /** Shifts a sound's formants (its vowel-like colour) without changing its pitch or length
        (owner, phase 4e): the sample editor's Formant control. It runs Signalsmith Stretch, the
        stretcher W36 approved, which Tracktion bundles but whose formant control it does not
        expose. It sits on a clip, marked, and is only there while the shift is not zero. It
        reports its latency, and Tracktion lines clip plugins up by it (checked by render). */
    class FormantPlugin final : public tracktion::Plugin
    {
    public:
        explicit FormantPlugin (tracktion::PluginCreationInfo);
        ~FormantPlugin() override;

        static const char* getPluginName()   { return "Formant"; }
        static const char* xmlTypeName;

        juce::String getName() const override              { return "Formant"; }
        juce::String getPluginType() override              { return xmlTypeName; }
        juce::String getShortName (int) override           { return "Formant"; }
        juce::String getSelectableDescription() override   { return "Formant shift"; }

        void initialise (const tracktion::PluginInitialisationInfo&) override;
        void deinitialise() override;
        void reset() override;
        double getLatencySeconds() override;

        int getNumOutputChannelsGivenInputs (int numInputChannels) override   { return juce::jmin (numInputChannels, 2); }
        BusLayout getBusses() const override   { return BusLayout::singleStereoInOut(); }
        void applyToBuffer (const tracktion::PluginRenderContext&) override;

        /** Semitones, -12 to 12: up brightens the colour (smaller-sounding), down darkens it. */
        juce::CachedValue<float> semitones;

    private:
        struct Stretch;
        std::unique_ptr<Stretch> stretch;   // made in initialise, off the audio thread
        juce::AudioBuffer<float> input;
        float applied = 0.0f;
        double rate = 44100.0;
        int latencySamples = 0;
    };
}
