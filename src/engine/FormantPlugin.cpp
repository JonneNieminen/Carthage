#include "FormantPlugin.h"

// Signalsmith's own header, as Tracktion bundles it (W36); header-only.
#include <tracktion_engine/3rd_party/signalsmith-stretch/signalsmith-stretch.h>

#include <utility>

namespace carthage::engine
{
    struct FormantPlugin::Stretch
    {
        signalsmith::stretch::SignalsmithStretch<float> core;
    };

    const char* FormantPlugin::xmlTypeName = "carthageFormant";

    FormantPlugin::FormantPlugin (tracktion::PluginCreationInfo info) : tracktion::Plugin (std::move (info))
    {
        semitones.referTo (state, "semitones", getUndoManager(), 0.0f);
    }

    FormantPlugin::~FormantPlugin()
    {
        notifyListenersOfDeletion();
    }

    void FormantPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
    {
        // Every allocation here, never on the audio thread: the stretcher, and a block of input.
        rate = info.sampleRate;
        stretch = std::make_unique<Stretch>();
        stretch->core.presetDefault (2, static_cast<float> (rate));
        stretch->core.setTransposeSemitones (0.0f);
        applied = semitones.get();
        stretch->core.setFormantSemitones (applied, false);
        latencySamples = stretch->core.inputLatency() + stretch->core.outputLatency();
        input.setSize (2, std::max (info.blockSizeSamples, 4096));
    }

    void FormantPlugin::deinitialise()   {}

    void FormantPlugin::reset()
    {
        if (stretch != nullptr)
            stretch->core.reset();
    }

    double FormantPlugin::getLatencySeconds()
    {
        return rate > 0.0 ? latencySamples / rate : 0.0;
    }

    void FormantPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || stretch == nullptr || fc.bufferNumSamples > input.getNumSamples())
            return;

        if (const auto wanted = semitones.get(); wanted != applied)
        {
            applied = wanted;
            stretch->core.setFormantSemitones (applied, false);
        }

        // The stretcher reads and writes separate buffers, two channels; a mono clip is doubled.
        auto& dest = *fc.destBuffer;
        const auto n = fc.bufferNumSamples, start = fc.bufferStartSample;
        const auto channels = std::min (2, dest.getNumChannels());

        for (int ch = 0; ch < 2; ++ch)
            input.copyFrom (ch, 0, dest, std::min (ch, channels - 1), start, n);

        float* out[2] = { dest.getWritePointer (0, start), channels > 1 ? dest.getWritePointer (1, start) : input.getWritePointer (1) };
        const float* in[2] = { input.getReadPointer (0), input.getReadPointer (1) };

        if (channels == 1)
        {
            // Mono: the second output must not land on the second input it is reading.
            in[1] = in[0];
        }

        stretch->core.process (in, n, out, n);
    }
}
