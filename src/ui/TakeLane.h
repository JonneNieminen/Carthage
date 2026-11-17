#pragma once

#include "theme/Theme.h"

#include <tracktion_engine/tracktion_engine.h>

#include <map>

namespace carthage::ui
{
    /** The one lane of takes, in a recessed well with a seconds ruler.

        It reads the track's clips from the edit and listens to the track's state; it keeps no copy
        of them. Waveforms come from juce::AudioThumbnail, which reads files on the thumbnail cache's
        own thread, so painting never decodes audio. The clips are drawn into a cached layer; the
        playhead and the take being recorded are separate children on top, so moving them does not
        repaint the clips. */
    class TakeLane final : public juce::Component, private juce::ValueTree::Listener
    {
    public:
        explicit TakeLane (tracktion::AudioTrack&);
        ~TakeLane() override;

        /** Once per frame: where the playhead is, and the take in progress (if any). */
        void setPlayhead (double seconds, std::optional<double> recordingFrom);

        void resized() override;
        void paint (juce::Graphics&) override;

        /** Seconds shown across the lane. */
        juce::Range<double> getVisibleRange() const noexcept   { return visible; }

        static constexpr int rulerHeight = 20;

    private:
        class ClipLayer;
        class Playhead;
        class RecordingRegion;

        tracktion::AudioTrack& track;
        juce::ValueTree trackState;
        juce::AudioFormatManager formats;
        juce::AudioThumbnailCache thumbnailCache { 16 };
        std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;   // by file path

        std::unique_ptr<ClipLayer> clips;
        std::unique_ptr<Playhead> playhead;
        std::unique_ptr<RecordingRegion> recording;
        juce::Range<double> visible { 0.0, 30.0 };
        double lastPlayhead = 0.0;
        std::optional<double> lastRecordingFrom;

        juce::AudioThumbnail& thumbnailFor (const juce::File&);
        void updateVisibleRange();
        float xFor (double seconds) const noexcept;
        void modelChanged();

        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { modelChanged(); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { modelChanged(); }
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { modelChanged(); }
    };
}
