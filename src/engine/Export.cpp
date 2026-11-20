#include "Export.h"

#include "Editing.h"
#include "Mixer.h"

namespace carthage::exporting
{
    using namespace tracktion;

    juce::String describe (Format f)
    {
        switch (f)
        {
            case Format::wav16:      return "16-bit WAV";
            case Format::wav24:      return "24-bit WAV";
            case Format::wav32Float: return "32-bit float WAV";
            case Format::flac16:     return "16-bit FLAC";
            case Format::flac24:     return "24-bit FLAC";
        }

        return {};
    }

    juce::String extensionOf (Format f)
    {
        return f == Format::flac16 || f == Format::flac24 ? "flac" : "wav";
    }

    int bitDepthOf (Format f)
    {
        switch (f)
        {
            case Format::wav16:
            case Format::flac16:     return 16;
            case Format::wav24:
            case Format::flac24:     return 24;
            case Format::wav32Float: return 32;   // JUCE writes 32-bit WAV as IEEE float
        }

        return 24;
    }

    std::optional<Range> rangeOf (Edit& edit)
    {
        if (const auto loop = editing::loopRegion (edit))
            return Range { { edit.tempoSequence.toTime (BeatPosition::fromBeats (loop->from)),
                             edit.tempoSequence.toTime (BeatPosition::fromBeats (loop->to)) }, true };

        std::optional<TimeRange> span;

        for (auto* clip : editing::allClips (edit))
        {
            const auto r = clip->getEditTimeRange();
            span = span ? span->getUnionWith (r) : r;
        }

        if (! span || span->getLength() <= TimeDuration())
            return std::nullopt;

        return Range { *span, false };
    }

    namespace
    {
        int leadInSamples (double rate)   { return static_cast<int> (std::lround (leadInSeconds * rate)); }

        /** Where the render goes before its lead-in is cut: beside the destination, hidden. */
        juce::File intermediateFor (const juce::File& destination)
        {
            return destination.getSiblingFile ("." + destination.getFileNameWithoutExtension() + ".rendering.wav");
        }

        /** Copies the render into the chosen format, without its lead-in. Any thread. */
        juce::Result finish (const juce::File& rendered, const juce::File& destination, const Settings& settings, Engine& engine)
        {
            const auto removeRendered = juce::ScopeGuard { [&rendered] { rendered.deleteFile(); } };
            std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().getWavFormat()->createReaderFor (rendered.createInputStream().release(), true));

            if (reader == nullptr)
                return juce::Result::fail ("The render could not be read back");

            auto* format = settings.format == Format::flac16 || settings.format == Format::flac24
                               ? engine.getAudioFileFormatManager().getFlacFormat()
                               : engine.getAudioFileFormatManager().getWavFormat();
            destination.deleteFile();
            std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (destination);

            if (static_cast<juce::FileOutputStream*> (out.get())->failedToOpen())
                return juce::Result::fail ("Carthage could not write " + destination.getFullPathName());

            auto writer = format->createWriterFor (out, juce::AudioFormatWriterOptions{}
                                                            .withSampleRate (reader->sampleRate)
                                                            .withNumChannels (static_cast<int> (reader->numChannels))
                                                            .withBitsPerSample (bitDepthOf (settings.format)));

            if (writer == nullptr)
                return juce::Result::fail ("The " + describe (settings.format) + " file could not be started");

            const auto skip = static_cast<juce::int64> (leadInSamples (settings.sampleRate));

            if (! writer->writeFromAudioReader (*reader, skip, reader->lengthInSamples - skip))
            {
                writer.reset();
                destination.deleteFile();
                return juce::Result::fail ("Writing the file failed (is the disk full?)");
            }

            return juce::Result::ok();
        }
    }

    Renderer::Parameters parametersFor (Edit& edit, const Range& range, const Settings& settings, const juce::File& destination)
    {
        // Rendered as 32-bit float with a short lead-in, which finish() cuts while writing the
        // chosen format: Tracktion's render fades in its first 32 samples whenever a plugin
        // reports latency, and that fade must fall before the range, not on its first sound.
        Renderer::Parameters p (edit);
        p.destFile = intermediateFor (destination);
        p.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
        p.bitDepth = 32;
        p.sampleRateForAudio = settings.sampleRate;
        p.blockSizeForAudio = 512;
        p.time = { range.time.getStart() - TimeDuration::fromSamples (leadInSamples (settings.sampleRate), settings.sampleRate), range.time.getEnd() };
        p.tracksToDo = toBitSet (getAllTracks (edit));   // every track: lanes play through channels and the Pre-Master (an empty set renders nothing)
        p.usePlugins = true;
        p.useMasterPlugins = true;          // master's Line, fader and pan are part of the mix
        p.canRenderInMono = false;          // a mix is stereo, even if every source is mono
        p.ditheringEnabled = false;
        p.checkNodesForAudio = false;       // an empty stretch still exports, as silence
        p.endAllowance = range.isLoopRegion ? TimeDuration() : TimeDuration::fromSeconds (tailSeconds);
        return p;
    }

    juce::Result render (Edit& edit, const Range& range, const Settings& settings, const juce::File& destination)
    {
        // As Tracktion's own blocking render does: free the playback graph first.
        const Edit::ScopedRenderStatus status (edit, true);
        TransportControl::stopAllTransports (edit.engine, false, true);
        destination.deleteFile();
        const auto rendered = intermediateFor (destination);

        auto task = render_utils::createRenderTask (parametersFor (edit, range, settings, destination), "Export", nullptr, nullptr);

        if (task == nullptr)
            return juce::Result::fail ("The mix could not be prepared for export");

        while (task->runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
            ;

        if (task->errorMessage.isNotEmpty() || ! rendered.existsAsFile())
        {
            rendered.deleteFile();
            return juce::Result::fail (task->errorMessage.isNotEmpty() ? task->errorMessage : juce::String ("Nothing was written"));
        }

        task.reset();
        return finish (rendered, destination, settings, edit.engine);
    }

    Job::~Job()
    {
        *alive = false;
        cancel();
    }

    float Job::getProgress() const
    {
        return handle != nullptr ? handle->getProgress() : 0.0f;
    }

    void Job::cancel()
    {
        if (handle != nullptr)
            handle->cancel();
    }

    std::unique_ptr<Job> start (Edit& edit, const Range& range, const Settings& settings, const juce::File& destination,
                                std::function<void (juce::Result)> finished)
    {
        std::unique_ptr<Job> job (new Job());
        edit.getTransport().stop (false, false);
        destination.deleteFile();

        auto params = parametersFor (edit, range, settings, destination);
        const std::weak_ptr<bool> token = job->alive;

        // The renderer calls back on its own thread; the outcome goes to the message thread.
        auto& engine = edit.engine;
        job->handle = EditRenderer::render (std::move (params), [token, destination, settings, &engine, finished = std::move (finished)] (tl::expected<juce::File, std::string> result)
        {
            // On the render thread: cut the lead-in and write the chosen format here, off the
            // message thread. A cancelled or failed export leaves nothing behind.
            const auto rendered = intermediateFor (destination);
            const auto outcome = result.has_value() ? finish (rendered, destination, settings, engine)
                                                    : juce::Result::fail (juce::String (result.error()));

            if (outcome.failed())
            {
                rendered.deleteFile();
                destination.deleteFile();
            }

            juce::MessageManager::callAsync ([token, outcome, finished]
            {
                if (token.lock() != nullptr && finished != nullptr)
                    finished (outcome);
            });
        });

        return job;
    }
}
