#include <engine/Editing.h>
#include <engine/SampleEdits.h>

using namespace carthage;

namespace
{
    struct Headless final : tracktion::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override   { return false; }
    };

    juce::File makeAudio (const juce::File& folder, const juce::String& name)
    {
        const auto file = folder.getChildFile (name + ".wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (16));
        juce::AudioBuffer<float> b (1, 48000);
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.2f * std::sin (static_cast<float> (i) * 0.05f));
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    }
}

/** Phase 5a's model (W47, W49): lanes are where clips are drawn, channels carry the sound. */
class MixerTests final : public juce::UnitTest
{
public:
    MixerTests() : juce::UnitTest ("Mixer channels and routing", "engine") {}

    void runTest() override
    {
        juce::TemporaryFile holder;
        const auto& folder = holder.getFile();
        folder.createDirectory();
        const auto kick = makeAudio (folder, "Kick");
        const auto snare = makeAudio (folder, "Snare");

        tracktion::Engine engine ("CarthageTests", nullptr, std::make_unique<Headless>());
        auto edit = tracktion::createEmptyEdit (engine, folder.getChildFile ("Mix.tracktionedit"));
        editing::useConsistentFilePaths (*edit);
        editing::prepareMix (*edit, true);
        auto& um = edit->getUndoManager();
        um.clearUndoHistory();

        const auto lanes = editing::lanes (*edit);
        const auto channels = editing::channels (*edit);

        beginTest ("A new project: 8 lanes, 50 channels, nothing in the audio graph");
        {
            expectEquals (lanes.size(), 8);
            expectEquals (channels.size(), editing::defaultChannels);
            expectEquals (channels[0]->getName(), juce::String ("Channel 1"));
            expectEquals (channels[49]->getName(), juce::String ("Channel 50"));
            expectEquals (editing::lanes (*edit)[2].getName(), juce::String ("Lane 3"));

            editing::syncMix (*edit, nullptr);
            for (auto* ch : channels)
                expect (! ch->isProcessing (false), "an empty channel costs nothing");
            expect (! um.canUndo(), "setting up and syncing are not undo steps: " + um.getUndoDescription());
        }

        beginTest ("A new sound plays to master on its lane; routing a sample moves all its clips");
        {
            editing::begin (*edit, "Add sample");
            auto* a = editing::insertFile (*edit, lanes[0], kick, 0.0);
            auto* b = editing::insertFile (*edit, lanes[3], kick, 8.0);
            auto* other = editing::insertFile (*edit, lanes[3], snare, 4.0);
            expect (a != nullptr && b != nullptr && other != nullptr);
            expect (editing::routeOf (*a) == nullptr && editing::routeOf (*b) == nullptr, "to master until routed (W47)");
            expectEquals (editing::laneOf (*a), 0);
            expectEquals (editing::laneOf (*b), 3);

            editing::begin (*edit, "Route Kick");
            editing::routeSample (*a, channels[2]);
            expect (editing::routeOf (*a) == channels[2] && editing::routeOf (*b) == channels[2], "every clip of the sample goes");
            expect (editing::routeOf (*other) == nullptr, "another sample stays where it was");
            expectEquals (editing::laneOf (*a), 0, "each keeps its lane");
            expectEquals (editing::laneOf (*b), 3);
            expect (a->getTrack() != b->getTrack(), "on carriers of their own lanes, so lanes keep their crossfades apart");

            editing::syncMix (*edit, nullptr);
            expect (channels[2]->isProcessing (false), "a channel with sound playing into it is in the graph");
            expect (! channels[3]->isProcessing (false), "its neighbour is not");

            um.undo();
            expect (editing::routeOf (*editing::clipsOn (*edit, lanes[0]).getFirst()) == nullptr, "one undo step routes it back");
            um.redo();
        }

        beginTest ("Moving a clip to another lane keeps its route; copies follow their sample");
        {
            auto* a = editing::clipsOn (*edit, lanes[0]).getFirst();
            editing::begin (*edit, "Move clip");
            editing::move ({ a }, 0.0, 1);
            expectEquals (editing::laneOf (*a), 1);
            expect (editing::routeOf (*a) == channels[2], "the route stays");

            auto* copy = editing::placeCopy (*a, lanes[5], 32.0);
            expect (copy != nullptr && editing::laneOf (*copy) == 5 && editing::routeOf (*copy) == channels[2], "a copy plays where its sample does");
        }

        beginTest ("Copy and paste: the route stays in the same project, master in another");
        {
            auto* a = editing::clipsOn (*edit, lanes[1]).getFirst();
            const auto copied = editing::copy ({ a });

            editing::begin (*edit, "Paste");
            const auto here = editing::paste (*edit, *copied, 64.0);
            expect (here.size() == 1 && editing::routeOf (*here[0]) == channels[2], "pasted here, it keeps its channel");
            expectEquals (editing::laneOf (*here[0]), 1, "and its lane");

            auto otherEdit = tracktion::createEmptyEdit (engine, folder.getChildFile ("Other.tracktionedit"));
            editing::useConsistentFilePaths (*otherEdit);
            editing::prepareMix (*otherEdit, true);
            editing::begin (*otherEdit, "Paste");
            const auto there = editing::paste (*otherEdit, *copied, 0.0);
            expect (there.size() == 1 && editing::routeOf (*there[0]) == nullptr, "in another project its channel means nothing: master");
        }

        beginTest ("A channel's output: master or another channel; feedback is refused");
        {
            expect (editing::setOutput (*channels[0], channels[1]).wasOk());
            expect (editing::outputOf (*channels[0]) == channels[1]);
            expect (editing::setOutput (*channels[1], channels[0]).failed(), "a loop back is refused");
            expect (editing::setOutput (*channels[1], channels[1]).failed(), "so is itself");
            expect (editing::setOutput (*channels[2], channels[0]).wasOk(), "a chain is fine: 3 into 1 into 2");
            expect (editing::setOutput (*channels[1], channels[2]).failed(), "but not closing it into a ring");

            editing::syncMix (*edit, nullptr);
            expect (channels[0]->isProcessing (false) && channels[1]->isProcessing (false), "downstream of the sound is in the graph too");

            expect (editing::setOutput (*channels[2], nullptr).wasOk());
            expect (editing::setOutput (*channels[0], nullptr).wasOk());
            expect (editing::outputOf (*channels[0]) == nullptr, "back to master");
        }

        beginTest ("Deleting a channel sends what played into it to master; undo brings it back");
        {
            auto* routed = editing::clipsOn (*edit, lanes[1]).getFirst();
            const auto clipID = routed->itemID;
            expect (editing::setOutput (*channels[4], channels[2]).wasOk());

            editing::begin (*edit, "Delete channel");
            editing::deleteChannel (*channels[2]);
            expectEquals (editing::channels (*edit).size(), editing::defaultChannels - 1);
            auto* after = tracktion::findClipForID (*edit, clipID);
            expect (after != nullptr && editing::routeOf (*after) == nullptr, "its clips play to master");
            expectEquals (editing::laneOf (*after), 1, "on their lane");
            expect (editing::outputOf (*channels[4]) == nullptr, "a channel that played into it now goes to master");

            um.undo();
            expectEquals (editing::channels (*edit).size(), editing::defaultChannels, "undo brings the channel back");
            auto* back = tracktion::findClipForID (*edit, clipID);
            expect (back != nullptr && editing::routeOf (*back) == editing::channels (*edit)[2], "and its clips into it");
            editing::setOutput (*editing::channels (*edit)[4], nullptr);
        }

        beginTest ("Lane mute and solo silence that lane's clips, whatever their channel");
        {
            const auto carrierOf = [] (tracktion::Clip* c) { return dynamic_cast<tracktion::AudioTrack*> (c->getTrack()); };
            auto* onOne = editing::clipsOn (*edit, lanes[1]).getFirst();
            auto* onThree = editing::clipsOn (*edit, lanes[3]).getFirst();

            editing::setLaneMuted (*edit, lanes[1], true);
            expect (carrierOf (onOne)->isMuted (false), "muted lane: silent");
            expect (! carrierOf (onThree)->isMuted (false), "others play");
            editing::setLaneMuted (*edit, lanes[1], false);

            editing::setLaneSoloed (*edit, lanes[3], true);
            expect (carrierOf (onOne)->isMuted (false) && ! carrierOf (onThree)->isMuted (false), "solo: only that lane plays");
            editing::setLaneSoloed (*edit, lanes[3], false);
            expect (! carrierOf (onOne)->isMuted (false), "no solo: all play");
        }

        beginTest ("A project from before 5a keeps its sound: lane n plays through channel n");
        {
            auto old = tracktion::createEmptyEdit (engine, folder.getChildFile ("Old.tracktionedit"));
            editing::useConsistentFilePaths (*old);
            old->ensureNumberOfAudioTracks (3);
            const auto tracks = tracktion::getAudioTracks (*old);
            tracks[0]->setName ("Guitar");
            tracks[1]->setName ("Drums");
            tracks[2]->setName ("Bass");
            tracks[1]->state.setProperty ("carthageColour", "ff8fd6a4", nullptr);
            tracks[2]->setMute (true);
            auto clip = tracktion::insertWaveClip (*tracks[1], "Kick", kick, { { tracktion::TimePosition::fromSeconds (1.0), tracktion::TimeDuration::fromSeconds (1.0) } },
                                                   tracktion::DeleteExistingClips::no);
            const auto clipID = clip->itemID;

            editing::prepareMix (*old, false);
            const auto oldLanes = editing::lanes (*old);
            expectEquals (oldLanes.size(), 3, "a lane for each old lane");
            expectEquals (oldLanes[1].getName(), juce::String ("Drums"), "names kept");
            expectEquals (oldLanes[1].state["carthageColour"].toString(), juce::String ("ff8fd6a4"), "colours kept");
            expect (oldLanes[2].isMuted(), "mute moves to the lane");
            expectEquals (editing::channels (*old).size(), editing::defaultChannels);

            auto* moved = tracktion::findClipForID (*old, clipID);
            expect (moved != nullptr && editing::laneOf (*moved) == 1, "the clip is on its lane");
            expect (moved != nullptr && editing::routeOf (*moved) == editing::channels (*old)[1], "playing through channel 2");

            editing::prepareMix (*old, false);
            expectEquals (editing::lanes (*old).size(), 3, "preparing again changes nothing");
        }
    }
};

static MixerTests mixerTests;
