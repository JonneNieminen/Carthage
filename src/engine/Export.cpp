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

    Renderer::Parameters parametersFor (Edit& edit, const Range& range, const Settings& settings, const juce::File& destination)
    {
        Renderer::Parameters p (edit);
        p.destFile = destination;
        p.audioFormat = settings.format == Format::flac16 || settings.format == Format::flac24
                            ? edit.engine.getAudioFileFormatManager().getFlacFormat()
                            : edit.engine.getAudioFileFormatManager().getWavFormat();
        p.bitDepth = bitDepthOf (settings.format);
        p.sampleRateForAudio = settings.sampleRate;
        p.blockSizeForAudio = 512;
        p.time = range.time;
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

        auto task = render_utils::createRenderTask (parametersFor (edit, range, settings, destination), "Export", nullptr, nullptr);

        if (task == nullptr)
            return juce::Result::fail ("The mix could not be prepared for export");

        while (task->runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
            ;

        if (task->errorMessage.isNotEmpty() || ! destination.existsAsFile())
        {
            destination.deleteFile();
            return juce::Result::fail (task->errorMessage.isNotEmpty() ? task->errorMessage : juce::String ("Nothing was written"));
        }

        return juce::Result::ok();
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
        job->handle = EditRenderer::render (std::move (params), [token, destination, finished = std::move (finished)] (tl::expected<juce::File, std::string> result)
        {
            const auto outcome = result.has_value() ? juce::Result::ok() : juce::Result::fail (juce::String (result.error()));

            if (outcome.failed())
                destination.deleteFile();   // a cancelled or failed export leaves nothing behind

            juce::MessageManager::callAsync ([token, outcome, finished]
            {
                if (token.lock() != nullptr && finished != nullptr)
                    finished (outcome);
            });
        });

        return job;
    }
}
