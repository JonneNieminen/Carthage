#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <memory>
#include <optional>

namespace carthage::exporting
{
    /** Export (brief, feature 8; W43): Cmd+R renders the mix, after every channel, the
        Pre-Master and master with their plugins, to WAV (16-bit, 24-bit, 32-bit float) or FLAC
        (16-bit, 24-bit). It renders the loop region when there is one, otherwise the project from
        its first sound to its last, letting reverbs and delays ring out until they fall silent.
        The metronome is never in an export. */
    enum class Format { wav16, wav24, wav32Float, flac16, flac24 };

    inline constexpr Format formats[] = { Format::wav16, Format::wav24, Format::wav32Float, Format::flac16, Format::flac24 };

    /** "24-bit WAV", and the file's extension ("wav"). */
    juce::String describe (Format);
    juce::String extensionOf (Format);
    int bitDepthOf (Format);

    /** The sample rates an export may use (the brief's range), and how each reads. */
    inline constexpr double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

    /** What gets exported and why. */
    struct Range
    {
        tracktion::TimeRange time;
        bool isLoopRegion = false;
    };

    /** The loop region if the edit has one; else from the first clip's start to the last clip's
        end; nothing if the edit has no clips. */
    std::optional<Range> rangeOf (tracktion::Edit&);

    /** How long before the range the render starts; that much is cut off again (see parametersFor). */
    inline constexpr double leadInSeconds = 0.01;

    /** How long a whole-project export may ring on after the last clip ends, at most. */
    inline constexpr double tailSeconds = 10.0;

    struct Settings
    {
        Format format = Format::wav24;
        double sampleRate = 48000.0;
    };

    /** The render's parameters: every track (lanes play through channels and the Pre-Master), every
        plugin including master's, no dither, no normalising. A whole-project export gets a tail
        that stops when the sound does; a loop region is exported exactly. */
    tracktion::Renderer::Parameters parametersFor (tracktion::Edit&, const Range&, const Settings&, const juce::File& destination);

    /** Renders on the calling thread and returns when the file is written: for tests and the
        bench. The app uses start(). */
    juce::Result render (tracktion::Edit&, const Range&, const Settings&, const juce::File& destination);

    /** A running export: its progress (0 to 1) can be read from any thread, cancel() stops it
        (the file is then removed), and destroying it cancels it too. */
    class Job
    {
    public:
        ~Job();
        float getProgress() const;
        void cancel();

    private:
        friend std::unique_ptr<Job> start (tracktion::Edit&, const Range&, const Settings&, const juce::File&,
                                           std::function<void (juce::Result)>);
        Job() = default;
        std::shared_ptr<tracktion::EditRenderer::Handle> handle;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };

    /** Starts rendering on a thread of its own (heavy work never runs on the message thread);
        `finished` is called on the message thread with the outcome. Playback stops first: the
        render takes the edit's graph while it runs. */
    std::unique_ptr<Job> start (tracktion::Edit&, const Range&, const Settings&, const juce::File& destination,
                                std::function<void (juce::Result)> finished);
}
