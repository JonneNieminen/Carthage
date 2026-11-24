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

        beginTest ("Channel mute and solo: a soloed channel plays with what feeds it and what it feeds (W50)");
        {
            const auto chs = editing::channels (*edit);
            const auto carrierOf = [] (tracktion::Clip* c) { return dynamic_cast<tracktion::AudioTrack*> (c->getTrack()); };
            auto* toMaster = editing::insertFile (*edit, lanes[6], snare, 96.0);
            auto* routed = editing::clipsOn (*edit, lanes[1]).getFirst();   // into channel 3

            editing::setChannelMuted (*chs[2], true);
            expect (chs[2]->isMuted (false), "a muted channel is silent");
            expect (! carrierOf (routed)->isMuted (false), "its carriers are not touched: the channel silences them");
            editing::setChannelMuted (*chs[2], false);
            expect (! chs[2]->isMuted (false));

            expect (editing::setOutput (*chs[2], chs[7]).wasOk());   // channel 3 into channel 8
            expect (editing::setOutput (*chs[9], chs[2]).wasOk());   // channel 10 into channel 3
            editing::setChannelSoloed (*chs[2], true);
            expect (! chs[2]->isMuted (false), "the soloed channel plays");
            expect (! chs[7]->isMuted (false), "and what it plays into");
            expect (! chs[9]->isMuted (false), "and what plays into it");
            expect (chs[4]->isMuted (false), "others are silent");
            expect (! carrierOf (routed)->isMuted (false), "its lanes' clips play");
            expect (carrierOf (toMaster)->isMuted (false), "clips straight to master are silent");
            expect (! chs[2]->isSolo (false), "Tracktion's own solo is not used");

            editing::setChannelSoloed (*chs[2], false);
            for (auto* ch : chs)
                expect (! ch->isMuted (false), "no solo: every channel plays");
            expect (! carrierOf (toMaster)->isMuted (false));
            editing::setOutput (*chs[2], nullptr);
            editing::setOutput (*chs[9], nullptr);
        }

        beginTest ("Cmd-click on a channel's M mutes the others, on S solos it alone; again undoes it");
        {
            const auto chs = editing::channels (*edit);
            editing::setChannelSoloed (*chs[5], true);

            editing::muteOtherChannels (*chs[2]);
            expect (! editing::isChannelMuted (*chs[2]), "the clicked channel plays");
            for (auto* ch : chs)
                if (ch != chs[2])
                    expect (editing::isChannelMuted (*ch), "every other channel is muted");

            editing::muteOtherChannels (*chs[2]);
            for (auto* ch : chs)
                expect (! editing::isChannelMuted (*ch), "again: every channel plays");

            editing::soloChannelAlone (*chs[2]);
            for (auto* ch : chs)
                expect (editing::isChannelSoloed (*ch) == (ch == chs[2]), "only the clicked channel is soloed");

            editing::soloChannelAlone (*chs[2]);
            for (auto* ch : chs)
                expect (! editing::isChannelSoloed (*ch), "again: no channel is soloed");
            for (auto* ch : chs)
                expect (! ch->isMuted (false), "and every channel plays");
        }

        beginTest ("Arming (5d): nothing is armed until the person arms it; several can be; undo takes it back");
        {
            const auto chs = editing::channels (*edit);
            expect (editing::armedChannels (*edit).isEmpty(), "nothing armed by default");

            editing::begin (*edit, "Arm");
            editing::setArmed (*chs[0], true);
            editing::begin (*edit, "Arm");
            editing::setArmed (*chs[3], true);
            expect (editing::armedChannels (*edit) == juce::Array<tracktion::AudioTrack*> { chs[0], chs[3] });

            edit->getUndoManager().undo();
            expect (editing::armedChannels (*edit) == juce::Array<tracktion::AudioTrack*> { chs[0] }, "undo disarms the last");
            editing::setArmed (*chs[0], false);
            expect (editing::armedChannels (*edit).isEmpty());
        }

        beginTest ("A take goes to the first lane with room for it, or a new lane (FL-style)");
        {
            const auto all = editing::lanes (*edit);
            const auto span = [] (double a, double b) { return tracktion::TimeRange (tracktion::TimePosition::fromSeconds (a), tracktion::TimePosition::fromSeconds (b)); };
            const auto busy = editing::clipsOn (*edit, all[1]);
            expect (! busy.isEmpty(), "lane 2 has a clip (set up above)");
            const auto taken = busy.getFirst()->getEditTimeRange();

            expect (editing::freeLaneFor (*edit, taken, all[1]) == all[2] || ! editing::clipsOn (*edit, all[2]).isEmpty(),
                    "a busy lane is passed over");
            expect (editing::freeLaneFor (*edit, span (500.0, 501.0), all[1]) == all[1], "a lane free at that time is used");

            const auto lanesBefore = all.size();
            const auto last = all.getLast();
            editing::begin (*edit, "Fill");
            auto* filler = editing::insertFile (*edit, last, kick, 2000.0);
            expect (filler != nullptr);
            const auto at = filler->getEditTimeRange();
            const auto made = editing::freeLaneFor (*edit, at, last);
            expectEquals (editing::lanes (*edit).size(), lanesBefore + 1, "every lane busy there: a new one at the end");
            expect (made == editing::lanes (*edit).getLast());
            edit->getUndoManager().undo();
            expectEquals (editing::lanes (*edit).size(), lanesBefore, "the new lane is part of the same undo step");
        }

        beginTest ("Project loudness: master starts there and remembers it; channels start at 0 dB");
        {
            expectWithinAbsoluteError (editing::projectLoudness (*edit), 0.0f, 1.0e-6f, "0 dB unless chosen");
            editing::setProjectLoudness (*edit, -6.0f);
            expectWithinAbsoluteError (editing::projectLoudness (*edit), -6.0f, 1.0e-6f);
            expectWithinAbsoluteError (edit->getMasterVolumePlugin()->getVolumeDb(), -6.0f, 0.05f, "master at the project's loudness");
            for (auto* ch : editing::channels (*edit))
                expectWithinAbsoluteError (editing::faderOf (*ch)->getVolumeDb(), 0.0f, 0.05f, "channels at 0 dB");
            editing::setProjectLoudness (*edit, 0.0f);
        }

        beginTest ("Every channel and master have a peak and an RMS meter; none counts as an insert");
        {
            auto* ch = editing::channels (*edit)[5];
            expect (editing::peakMeterOf (*ch) != nullptr, "the fader's peak meter");
            expect (editing::rmsMeterOf (*ch) != nullptr, "an RMS meter: " + juce::String (ch->pluginList.size()) + " plugins");
            expect (editing::rmsMeterOf (*ch)->measurer.getMode() == tracktion::LevelMeasurer::RMSMode);
            expect (editing::peakMeterOf (*ch) != editing::rmsMeterOf (*ch));
            expect (editing::masterPeakMeter (*edit) != nullptr && editing::masterRMSMeter (*edit) != nullptr);
            expect (editing::insertsOf (*ch).isEmpty(), "a channel's own fader and meters are not inserts");
        }

        beginTest ("Each channel keeps its own input, monitoring and the record channel (W51)");
        {
            const auto chs = editing::channels (*edit);
            expect (! editing::inputOf (*chs[3]).isSet(), "no input until chosen");
            um.clearUndoHistory();   // earlier routing calls here ran outside a named step

            editing::begin (*edit, "Input");
            editing::setInput (*chs[3], { 2, false });
            editing::setMonitoring (*chs[3], true);
            expect (editing::inputOf (*chs[3]) == editing::ChannelInput { 2, false });
            expect (editing::isMonitoring (*chs[3]));

            editing::begin (*edit, "Input");
            editing::setInput (*chs[4], { 3, true });
            expect (editing::inputOf (*chs[4]) == editing::ChannelInput { 2, true }, "a stereo pair starts on its even input");

            editing::syncMix (*edit, nullptr);
            expect (chs[3]->isProcessing (false), "a channel hearing its input is in the graph");
            expect (! chs[4]->isProcessing (false), "one not hearing it is not (nothing else plays through it)");

            expect (editing::recordChannel (*edit) == chs[0], "takes record through the first channel by default");
            editing::begin (*edit, "Record through");
            editing::setRecordChannel (*edit, *chs[3]);
            expect (editing::recordChannel (*edit) == chs[3]);

            // Every edit can be undone (computer-use test: inputs and the record channel could not).
            um.undo();
            expect (editing::recordChannel (*edit) == chs[0], "undo brings the record channel back");
            um.undo();
            expect (! editing::inputOf (*chs[4]).isSet(), "and the next undo takes back the last input chosen");
            expect (editing::inputOf (*chs[3]).isSet(), "one step at a time");

            editing::begin (*edit, "No input");
            editing::setInput (*chs[3], {});
            expect (! editing::isMonitoring (*chs[3]), "no input: nothing left to hear");
            editing::setInput (*chs[4], {});
        }

        beginTest ("Undo puts a fader's level back where the sound and the strip read it (W63)");
        {
            auto& ch = *editing::channels (*edit)[24];
            editing::begin (*edit, "Volume");
            editing::faderOf (ch)->setVolumeDb (-12.0f);
            editing::begin (*edit, "Next");
            expect (editing::undo (*edit));
            expectWithinAbsoluteError (editing::faderOf (ch)->getVolumeDb(), 0.0f, 0.05f, "the level the fader plays at is 0 dB again");
            expect (editing::redo (*edit));
            expectWithinAbsoluteError (editing::faderOf (ch)->getVolumeDb(), -12.0f, 0.05f, "and redo brings -12 dB back");
            editing::begin (*edit, "Back");
            editing::faderOf (ch)->setVolumeDb (0.0f);
        }

        beginTest ("Reset routing: back to master, no sends, the main output on (owner)");
        {
            const auto chs = editing::channels (*edit);
            auto& a = *chs[14];
            expect (editing::setOutput (a, chs[15]).wasOk());
            expect (editing::addSend (a, *chs[16], -6.0f).wasOk());
            expect (editing::addSend (a, *chs[17]).wasOk());
            editing::setPlaysToOutput (a, false);

            editing::begin (*edit, "Reset routing");
            editing::resetRouting (a);
            expect (editing::outputOf (a) == nullptr, "into master again");
            expect (editing::sendsOf (a).empty(), "no sends left");
            expect (editing::playsToOutput (a), "its main output on");

            edit->getUndoManager().undo();
            expect (editing::outputOf (a) == chs[15] && editing::sendsOf (a).size() == 2, "one undo brings the routing back");
            edit->getUndoManager().redo();
        }

        beginTest ("Mixer chains: copy a channel whole, save its plugins as a chain and load them elsewhere (owner)");
        {
            const auto chs = editing::channels (*edit);
            auto& a = *chs[20];
            auto& b = *chs[21];
            auto& c = *chs[22];

            // A plugin with a setting of its own, as a stand-in for a real one (Tracktion's reverb;
            // never offered to users, AGENTS.md).
            auto reverb = edit->getPluginCache().createNewPlugin (tracktion::ReverbPlugin::xmlTypeName, {});
            expect (reverb != nullptr, "a built-in plugin to carry a setting");

            if (auto* r = dynamic_cast<tracktion::ReverbPlugin*> (reverb.get()))
            {
                a.pluginList.insertPlugin (reverb, editing::listIndexForSlot (a.pluginList, 0), nullptr);
                r->setRoomSize (0.8f);
            }

            editing::faderOf (a)->setVolumeDb (-6.0f);
            editing::faderOf (a)->setPan (0.3f);
            editing::setChannelMuted (a, true);
            expect (editing::addSend (a, *chs[23], -9.0f).wasOk());

            expect (editing::nextChannel (a) == &b, "the next channel");
            expect (editing::nextEmptyChannel (a) == &b, "and the next empty one, while it is");

            editing::begin (*edit, "Copy channel");
            expect (editing::copyChannel (a, b).wasOk());
            expectWithinAbsoluteError (editing::faderOf (b)->getVolumeDb(), -6.0f, 0.05f, "its level");
            expectWithinAbsoluteError (editing::faderOf (b)->getPan(), 0.3f, 0.01f, "its pan");
            expect (editing::isChannelMuted (b), "its mute");
            expectEquals (editing::insertsOf (b).size(), 1, "its plugins");
            auto* copied = dynamic_cast<tracktion::ReverbPlugin*> (editing::insertsOf (b).getFirst());
            expect (copied != nullptr && std::abs (copied->getRoomSize() - 0.8f) < 0.01f, "with their settings");
            expect (copied != reverb.get(), "a plugin of its own, not the same one");
            expect (editing::sendsOf (b).size() == 1 && editing::sendsOf (b)[0].to == chs[23], "its sends");
            expect (editing::nextEmptyChannel (a) == &c, "b is not empty now: the next empty one is c");

            // A chain round trip through a file.
            const auto file = juce::File::createTempFile (editing::chainExtension);
            expect (editing::saveChain (a, file).wasOk());
            const auto chain = editing::readChain (file);
            expect (chain.has_value(), "the file reads back as a chain");
            expectEquals (editing::loadChain (c, chain.value_or (juce::ValueTree())), 1, "loaded onto another channel");
            expect (editing::faderOf (c)->getVolumeDb() > -0.1f, "a chain is plugins only: the level stays");
            expect (! editing::readChain (juce::File::createTempFile (".txt")).has_value(), "anything else is not a chain");
            file.deleteFile();

            expect (editing::copyChannel (a, a).failed(), "a channel can not be copied onto itself");

            editing::undo (*edit);
            expect (editing::insertsOf (b).isEmpty(), "one undo takes the copied plugins away");
            expect (editing::insertsOf (c).isEmpty(), "and the loaded chain");
            expect (editing::faderOf (b)->getVolumeDb() > -0.1f, "and the level");
            expect (editing::sendsOf (b).empty(), "and the sends");

            editing::begin (*edit, "Clean up");
            for (auto* p : editing::insertsOf (a)) editing::removeInsert (*p);
            editing::removeSend (a, *chs[23]);
            editing::setChannelMuted (a, false);
            editing::faderOf (a)->setVolumeDb (0.0f);
            editing::faderOf (a)->setPan (0.0f);
        }

        beginTest ("Sends: several destinations, levels, no loops, main output off (W51)");
        {
            const auto chs = editing::channels (*edit);
            auto& a = *chs[10];
            auto& b = *chs[11];
            auto& c = *chs[12];

            expect (editing::addSend (a, b, -3.0f).wasOk());
            expect (editing::addSend (a, c).wasOk());
            const auto sends = editing::sendsOf (a);
            expectEquals (static_cast<int> (sends.size()), 2, "one channel into two");
            expect (sends[0].to == &b && std::abs (sends[0].gainDb + 3.0f) < 0.1f, "at its own level");
            expect (editing::reaches (a, c) && ! editing::reaches (c, a));

            expect (editing::addSend (c, a).failed(), "a send back is refused");
            expect (editing::setOutput (b, &a).failed(), "so is an output back, through a send");
            expect (editing::addSend (a, a).failed());

            editing::setSendLevel (a, b, -12.0f);
            expect (std::abs (editing::sendsOf (a)[0].gainDb + 12.0f) < 0.1f);

            // The order on a channel: returns, the person's plugins, fader, meters, sends, gate.
            expect (editing::insertsOf (b).isEmpty() && editing::insertsOf (a).isEmpty(), "sends and returns are not inserts");
            expect (dynamic_cast<tracktion::AuxReturnPlugin*> (b.pluginList[0]) != nullptr, "the return comes first");
            const auto slot = editing::listIndexForSlot (b.pluginList, 0);
            expect (b.pluginList[slot] == editing::faderOf (b), "a new plugin goes after the return, before the fader");

            editing::setPlaysToOutput (a, false);
            expect (! editing::playsToOutput (a));
            expect (editing::faderOf (a) != nullptr && std::abs (editing::faderOf (a)->getVolumeDb()) < 0.1f, "the fader is still the fader, not the gate");
            expect (editing::addSend (a, *chs[13]).wasOk());
            expect (dynamic_cast<tracktion::VolumeAndPanPlugin*> (a.pluginList[a.pluginList.size() - 1]) != nullptr, "the gate stays last, after a new send");
            editing::setPlaysToOutput (a, true);
            expect (editing::playsToOutput (a));

            editing::syncMix (*edit, nullptr);
            editing::setChannelSoloed (b, true);
            expect (! a.isMuted (false), "soloing a channel keeps what sends into it");
            expect (c.isMuted (false), "a sibling destination is silent");
            editing::setChannelSoloed (b, false);

            editing::removeSend (a, b);
            editing::removeSend (a, c);
            editing::removeSend (a, *chs[13]);
            expect (editing::sendsOf (a).empty());
        }

        beginTest ("The Pre-Master: everything for master passes through it while it is on (W51)");
        {
            auto* pm = editing::preMaster (*edit);
            const auto chs = editing::channels (*edit);
            expect (pm != nullptr && pm->getName() == "Pre-Master", "every project has one");
            expect (! chs.contains (pm), "it is not one of the channels");
            expect (editing::usesPreMaster (*edit), "on by default");
            expect (editing::outputOf (*chs[20]) == nullptr, "a channel still reads as playing to master");
            expect (chs[20]->getOutput().getDestinationTrack() == pm, "but its sound goes through the Pre-Master");

            auto* clip = editing::insertFile (*edit, lanes[7], kick, 200.0);
            expect (editing::routeOf (*clip) == nullptr, "a new sound reads as master");
            expect (dynamic_cast<tracktion::AudioTrack*> (clip->getTrack())->getOutput().getDestinationTrack() == pm, "and plays through it");

            editing::setUsesPreMaster (*edit, false);
            expect (! editing::usesPreMaster (*edit));
            expect (chs[20]->getOutput().getDestinationTrack() == nullptr, "off: straight to master");
            expect (dynamic_cast<tracktion::AudioTrack*> (clip->getTrack())->getOutput().getDestinationTrack() == nullptr);
            expect (editing::setOutput (*chs[21], nullptr).wasOk() && chs[21]->getOutput().getDestinationTrack() == nullptr);

            editing::setUsesPreMaster (*edit, true);
            expect (chs[20]->getOutput().getDestinationTrack() == pm && chs[21]->getOutput().getDestinationTrack() == pm, "on again: through it");
            expect (editing::outputOf (*chs[4]) == nullptr || editing::outputOf (*chs[4]) != pm, "a channel never names the Pre-Master as its output");
            editing::remove ({ clip });
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
