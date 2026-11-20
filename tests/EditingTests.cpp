#include <engine/Editing.h>

using namespace carthage;

namespace
{
    /** An engine that never opens an audio device: the tests run headless and in CI. */
    struct HeadlessBehaviour final : tracktion::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override   { return false; }
    };

    /** A 4-second sine at 48 kHz: 8 beats at the edit's 120 bpm. */
    juce::File makeAudio (const juce::File& folder)
    {
        auto file = folder.getChildFile ("tone.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0)
                                                                                 .withNumChannels (1).withBitsPerSample (24));
        juce::AudioBuffer<float> b (1, 48000 * 4);

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.25f * std::sin (static_cast<float> (i) * 0.05f));

        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    }
}

class EditingTests final : public juce::UnitTest
{
public:
    EditingTests() : juce::UnitTest ("Timeline edits", "engine") {}

    void runTest() override
    {
        juce::TemporaryFile folderHolder;
        const auto& folder = folderHolder.getFile();
        folder.createDirectory();
        const auto audio = makeAudio (folder);

        tracktion::Engine engine ("CarthageTests", nullptr, std::make_unique<HeadlessBehaviour>());
        auto edit = tracktion::createEmptyEdit (engine, folder.getChildFile ("Test.tracktionedit"));
        editing::useConsistentFilePaths (*edit);
        editing::prepareMix (*edit, true, 2);   // two lanes, the default channels (W47)
        edit->tempoSequence.getTempo (0)->setBpm (120.0);

        auto lanes = editing::lanes (*edit);
        auto& um = edit->getUndoManager();

        // The carrier that holds a lane's clips playing to master: where a new sound goes (W47).
        const auto carrier = [&] (int lane) { return editing::carrierFor (*edit, editing::lanes (*edit)[lane], nullptr); };

        const auto newClip = [&] (double beat)
        {
            auto c = tracktion::insertWaveClip (*carrier (0), "Tone", audio,
                                                { { tracktion::TimePosition::fromSeconds (beat * 0.5), tracktion::TimeDuration::fromSeconds (4.0) } },
                                                tracktion::DeleteExistingClips::no);
            return static_cast<tracktion::Clip*> (c.get());
        };

        const auto near = [this] (double a, double b, const juce::String& what) { expectWithinAbsoluteError (a, b, 1.0e-6, what); };

        beginTest ("A clip beside the edit finds its audio (W32)");
        {
            auto* c = newClip (0.0);
            expectEquals (c->state.getProperty (tracktion::IDs::source).toString().replaceCharacter ('\\', '/'), juce::String ("../tone.wav"),
                          "Tracktion writes it relative to the edit file");
            expect (dynamic_cast<tracktion::AudioClipBase*> (c)->getAudioFile().getFile() == audio, "and Carthage resolves it to the real file");
            editing::remove ({ c });
        }

        beginTest ("Move: by beats, never before zero, across lanes; one transaction undoes it all");
        {
            auto* c = newClip (0.0);
            near (editing::endBeat (*c), 8.0, "8 beats long");

            editing::begin (*edit, "Move clip");
            editing::move ({ c }, 4.0, 0);
            editing::move ({ c }, 2.0, 0);   // a drag: many moves, one transaction
            near (editing::startBeat (*c), 6.0, "moved");
            near (editing::endBeat (*c) - editing::startBeat (*c), 8.0, "length kept");

            editing::begin (*edit, "Move clip");
            editing::move ({ c }, -100.0, 0);
            near (editing::startBeat (*c), 0.0, "stops at zero");

            editing::begin (*edit, "Next");
            um.undo();
            near (editing::startBeat (*c), 6.0, "undo the second move");
            um.undo();
            near (editing::startBeat (*c), 0.0, "one undo reverses the whole first drag");
            um.redo();
            near (editing::startBeat (*c), 6.0, "redo");

            editing::begin (*edit, "Move clip to lane 2");
            editing::move ({ c }, 0.0, 1);
            expectEquals (editing::laneOf (*editing::clipsOn (*edit, editing::lanes (*edit)[1]).getFirst()), 1, "on the second lane");
            editing::remove ({ editing::clipsOn (*edit, editing::lanes (*edit)[1]).getFirst() });
        }

        beginTest ("Trim keeps the content in place; slip moves it; stretch changes the speed");
        {
            auto* c = newClip (0.0);

            editing::begin (*edit, "Trim clip");
            editing::trimStart (*c, 2.0);
            near (editing::startBeat (*c), 2.0, "start trimmed");
            near (editing::endBeat (*c), 8.0, "end unchanged");
            near (c->getPosition().getOffset().inSeconds(), 1.0, "content stays: offset 1 s");

            editing::trimStart (*c, -4.0);
            near (editing::startBeat (*c), 0.0, "can not start before the audio does");

            editing::trimEnd (*c, 4.0);
            near (editing::endBeat (*c), 4.0, "end trimmed");
            editing::trimEnd (*c, 20.0);
            near (editing::endBeat (*c), 8.0, "can not run past the audio");

            editing::trimStart (*c, 2.0);
            editing::begin (*edit, "Slip clip");
            editing::slip (*c, 1.0);
            near (c->getPosition().getOffset().inSeconds(), 0.5, "slipping right shows earlier audio");
            near (editing::startBeat (*c), 2.0, "the clip stays put");

            editing::begin (*edit, "Stretch clip");
            editing::stretchEnd (*c, editing::startBeat (*c) + 2.0 * (editing::endBeat (*c) - editing::startBeat (*c)));
            near (c->getSpeedRatio(), 0.5, "twice as long plays at half speed");
            editing::remove ({ c });
        }

        beginTest ("Split, duplicate, mute, markers and the loop range");
        {
            auto* c = newClip (0.0);

            editing::begin (*edit, "Split clip");
            const auto right = editing::split (*edit, { c }, 3.0);
            expectEquals (right.size(), 1);
            near (editing::endBeat (*c), 3.0, "left part ends at the cut");
            near (editing::startBeat (*right[0]), 3.0, "right part starts there");
            near (editing::endBeat (*right[0]), 8.0, "and ends where the clip did");
            expect (editing::split (*edit, { c }, 10.0).isEmpty(), "a cut outside the clip does nothing");

            editing::begin (*edit, "Duplicate clips");
            const auto copies = editing::duplicate ({ c, right[0] });
            expectEquals (copies.size(), 2);
            near (editing::startBeat (*copies[0]), 8.0, "copies follow the originals");
            near (editing::startBeat (*copies[1]), 11.0, "and keep their spacing");

            editing::begin (*edit, "Mute clips");
            editing::toggleMute ({ c, right[0] });
            expect (c->disabled.get() && right[0]->disabled.get());
            editing::toggleMute ({ c, right[0] });
            expect (! c->disabled.get());

            auto* painted = editing::placeCopy (*c, lanes[1], 16.0);
            expect (painted != nullptr && editing::laneOf (*painted) == 1 && std::abs (editing::startBeat (*painted) - 16.0) < 1.0e-6,
                    "a copy placed on another lane");

            editing::begin (*edit, "Add marker");
            expect (editing::addMarker (*edit, 4.0) != nullptr);
        }

        beginTest ("The loop region: set, change and clear, one undo step each (4g)");
        {
            auto& transport = edit->getTransport();
            const auto loopSeconds = [&] { return std::pair (transport.getLoopRange().getStart().inSeconds(), transport.getLoopRange().getEnd().inSeconds()); };
            editing::clearLoopRegion (*edit);
            um.clearUndoHistory();

            editing::begin (*edit, "Loop region");
            editing::setLoopRegion (*edit, 8.0, 4.0);   // either order
            expect (editing::loopRegion (*edit).has_value() && editing::isLooping (*edit), "set: it loops");
            near (loopSeconds().first, 2.0, "the transport loops from beat 4 (120 bpm)");
            near (loopSeconds().second, 4.0, "to beat 8");

            editing::begin (*edit, "Loop end");
            editing::setLoopRegion (*edit, 4.0, 6.0);
            editing::setLoopRegion (*edit, 4.0, 12.0);   // a drag: many moves, one step
            near (loopSeconds().second, 6.0, "changed: the end follows");

            editing::begin (*edit, "Clear loop");
            editing::clearLoopRegion (*edit);
            expect (! editing::loopRegion (*edit) && ! editing::isLooping (*edit), "cleared: no region, not looping");
            expect (transport.getLoopRange().getLength() == tracktion::TimeDuration(), "and the transport has no loop");

            um.undo();
            editing::syncLoop (*edit);   // what the session's hook does after an undo
            expect (editing::isLooping (*edit), "undoing the clear brings the loop back, looping");
            near (loopSeconds().second, 6.0, "as it was");

            um.undo();
            editing::syncLoop (*edit);
            near (loopSeconds().second, 4.0, "undoing the drag is one step back to beat 8");

            um.undo();
            editing::syncLoop (*edit);
            expect (! editing::loopRegion (*edit) && ! editing::isLooping (*edit), "undoing the set leaves none");

            editing::setLooping (*edit, true);
            expect (! editing::isLooping (*edit), "without a region it cannot loop");

            editing::begin (*edit, "Loop region");
            editing::setLoopRegion (*edit, 0.0, 4.0);
            editing::setLooping (*edit, false);
            expect (editing::loopRegion (*edit).has_value() && ! editing::isLooping (*edit), "looping turns off and the region stays");
            expect (um.getUndoDescription() == "Loop region", "turning looping off is not an undo step");

            editing::setLoopRegion (*edit, 3.0, 3.0);
            expect (editing::loopRegion (*edit).value_or (editing::LoopRegion {}).to == 4.0, "an empty region is not set");
        }

        beginTest ("A project from before 4g keeps its transport's loop (4g)");
        {
            editing::clearLoopRegion (*edit);
            edit->getTransport().setLoopRange ({ tracktion::TimePosition::fromSeconds (1.0), tracktion::TimePosition::fromSeconds (3.0) });
            editing::adoptTransportLoop (*edit);
            const auto r = editing::loopRegion (*edit).value_or (editing::LoopRegion {});
            near (r.from, 2.0, "beat 2");
            near (r.to, 6.0, "to beat 6");
            editing::clearLoopRegion (*edit);
        }

        beginTest ("Copy and paste into another project, in another folder and tempo (W30)");
        {
            for (const auto& l : lanes)
                editing::remove (editing::clipsOn (*edit, l));

            auto* a = newClip (2.0);
            auto* b = tracktion::insertWaveClip (*carrier (1), "Tone 2", audio,
                                                 { { tracktion::TimePosition::fromSeconds (3.0), tracktion::TimeDuration::fromSeconds (1.5) },
                                                   tracktion::TimeDuration::fromSeconds (0.5) },
                                                 tracktion::DeleteExistingClips::no).get();
            const auto copied = editing::copy ({ a, b });
            expectEquals (copied->count(), 2);

            // Another project: another folder, so a relative path from here would not resolve there.
            const auto other = folder.getChildFile ("Elsewhere");
            other.createDirectory();
            auto target = tracktion::createEmptyEdit (engine, other.getChildFile ("Other.tracktionedit"));
            editing::useConsistentFilePaths (*target);
            editing::prepareMix (*target, true);
            target->tempoSequence.getTempo (0)->setBpm (90.0);

            editing::begin (*target, "Paste 2 clips");
            const auto pasted = editing::paste (*target, *copied, 8.0);
            expectEquals (pasted.size(), 2);

            auto lane = [] (auto* c) { return editing::laneOf (*c); };
            auto seconds = [] (auto* c) { return c->getPosition().getStart().inSeconds(); };
            auto* first = lane (pasted[0]) == 0 ? pasted[0] : pasted[1];
            auto* second = first == pasted[0] ? pasted[1] : pasted[0];

            expectEquals (lane (first), 0, "the first clip lands on its lane");
            expectEquals (lane (second), 1, "the second keeps its lane below it");
            near (seconds (first), 8.0 / 1.5, "at beat 8 of a 90 bpm project");
            near (seconds (second) - seconds (first), 2.0, "and the two keep their distance in time");
            near (second->getPosition().getLength().inSeconds(), 1.5, "lengths are kept");
            near (second->getPosition().getOffset().inSeconds(), 0.5, "and so is where the audio starts");
            expect (dynamic_cast<tracktion::AudioClipBase*> (first)->getAudioFile().getFile() == audio,
                    "the pasted clip finds its audio from the other folder");

            target->getUndoManager().undo();
            expectEquals (editing::allClips (*target).size(), 0,
                          "a paste undoes in one step");
        }

        beginTest ("Stretching keeps pitch; a loop fitted to the tempo keeps its beats (W36)");
        {
            auto* stretched = newClip (0.0);
            editing::stretchEnd (*stretched, 12.0);
            expect (dynamic_cast<tracktion::AudioClipBase*> (stretched)->getTimeStretchMode() != tracktion::TimeStretcher::disabled,
                    "a stretched clip is time-stretched, not sped up");

            // The 4-second file as a loop at 60 bpm holds 4 beats; in this 120 bpm project that is 2 s.
            auto* loop = editing::insertFile (*edit, lanes[1], audio, 16.0);
            editing::fitToTempo (*loop, 60.0);
            auto* fitted = dynamic_cast<tracktion::AudioClipBase*> (loop);
            expect (fitted->getAutoTempo(), "it follows the tempo");
            near (editing::endBeat (*loop) - editing::startBeat (*loop), 4.0, "four beats long");
            near (loop->getPosition().getLength().inSeconds(), 2.0, "two seconds at 120 bpm");
            editing::remove ({ stretched, loop });
        }

        beginTest ("A dropped sound lands at the beat, as long as the file (W30)");
        {
            auto* dropped = editing::insertFile (*edit, lanes[1], audio, 6.0);
            expect (dropped != nullptr);
            near (editing::startBeat (*dropped), 6.0, "at the drop beat");
            near (dropped->getPosition().getLength().inSeconds(), 4.0, "the whole 4-second file");
            expectEquals (editing::laneOf (*dropped), 1);
            expect (editing::insertFile (*edit, lanes[1], folder.getChildFile ("missing.wav"), 0.0) == nullptr, "a missing file adds nothing");
        }

        beginTest ("A tempo change: clips stay in time, move with the beat, or stretch with it (W38)");
        {
            const auto run = [&] (editing::TempoFollow how, auto check)
            {
                editing::setTempo (*edit, 120.0);
                auto* c = editing::insertFile (*edit, lanes[1], audio, 4.0);   // 4 s at beat 4 (2 s): beats 4 to 12
                const auto before = editing::placesInBeats (*edit);
                editing::setTempo (*edit, 60.0);
                editing::followTempo (*edit, before, how);
                check (*c);
                editing::remove ({ c });
            };

            run (editing::TempoFollow::leaveInTime, [&] (tracktion::Clip& c)
            {
                near (c.getPosition().getStart().inSeconds(), 2.0, "leave: same second");
                near (c.getPosition().getLength().inSeconds(), 4.0, "leave: same length");
            });
            run (editing::TempoFollow::moveWithBeat, [&] (tracktion::Clip& c)
            {
                near (editing::startBeat (c), 4.0, "move: same beat");
                near (c.getPosition().getLength().inSeconds(), 4.0, "move: same length in seconds");
            });
            run (editing::TempoFollow::stretch, [&] (tracktion::Clip& c)
            {
                near (editing::startBeat (c), 4.0, "stretch: same beat");
                near (editing::endBeat (c), 12.0, "stretch: same length in beats");
                near (c.getSpeedRatio(), 0.5, "plays at half speed, time-stretched");
            });
            editing::setTempo (*edit, 120.0);

            // A fitted loop follows the beat whatever was chosen.
            auto* loop = editing::insertFile (*edit, lanes[1], audio, 4.0);
            editing::fitToTempo (*loop, 120.0);   // 4 s at 120 bpm: 8 beats
            const auto before = editing::placesInBeats (*edit);
            editing::setTempo (*edit, 60.0);
            editing::followTempo (*edit, before, editing::TempoFollow::leaveInTime);
            near (editing::startBeat (*loop), 4.0, "the loop stays on its beat");
            near (editing::endBeat (*loop), 12.0, "and keeps its 8 beats");
            editing::remove ({ loop });
            editing::setTempo (*edit, 120.0);
        }

        beginTest ("Lanes: named Lane N; adding, renaming and deleting are undo steps (W38)");
        {
            lanes = editing::lanes (*edit);
            editing::renameLane (*edit, lanes[0], "Lane 1");
            editing::renameLane (*edit, lanes[1], "Lane 3");
            expectEquals (editing::nextLaneName (*edit), juce::String ("Lane 2"), "the lowest free number");

            const auto added = editing::addLane (*edit, lanes[0]);
            expect (added.isValid());
            expectEquals (added.getName(), juce::String ("Lane 2"));
            expectEquals (editing::lanes (*edit).indexOf (added), 1, "below the lane asked for");
            expectEquals (editing::addLane (*edit).getName(), juce::String ("Lane 4"), "at the end");
            expectEquals (editing::lanes (*edit).size(), 4);
            um.undo();
            expectEquals (editing::lanes (*edit).size(), 3, "undo removes the added lane");

            editing::renameLane (*edit, added, "  Bass  ");
            expectEquals (added.getName(), juce::String ("Bass"), "trimmed");
            editing::renameLane (*edit, added, "   ");
            expectEquals (added.getName(), juce::String ("Bass"), "a blank name keeps the old one");
            um.undo();
            expectEquals (added.getName(), juce::String ("Lane 2"), "undo restores the name");

            editing::setLaneMuted (*edit, added, true);
            editing::setLaneSoloed (*edit, added, true);
            expect (added.isMuted() && added.isSoloed());
            editing::setLaneMuted (*edit, added, false);
            editing::setLaneSoloed (*edit, added, false);
            expect (! added.isMuted() && ! added.isSoloed());

            const auto recordLane = editing::recordingLane (*edit);
            expect (recordLane == editing::lanes (*edit)[0], "the first lane records");
            expect (! editing::canDeleteLane (*edit, recordLane), "the recording lane stays");
            expect (editing::canDeleteLane (*edit, added));
            auto* clip = editing::insertFile (*edit, added, audio, 0.0);
            expect (clip != nullptr);
            const auto clipsBefore = editing::allClips (*edit).size();
            editing::deleteLane (*edit, added);
            expectEquals (editing::lanes (*edit).size(), 2, "deleted with its clip");
            expectEquals (editing::allClips (*edit).size(), clipsBefore - 1, "its clip went with it");
            um.undo();
            const auto back = editing::lanes (*edit);
            expectEquals (back.size(), 3, "undo brings it back");
            expectEquals (back[1].getName(), juce::String ("Lane 2"));
            expectEquals (editing::clipsOn (*edit, back[1]).size(), 1, "with its clip");
            lanes = editing::lanes (*edit);
        }

        beginTest ("Lanes: several added at once, moved, muted around and soloed alone (owner's list)");
        {
            const auto before = editing::lanes (*edit).size();
            const auto first = editing::addLanes (*edit, 4, editing::lanes (*edit)[0]);
            auto all = editing::lanes (*edit);
            expectEquals (all.size(), before + 4);
            expectEquals (all.indexOf (first), 1, "below the lane asked for");
            um.undo();
            expectEquals (editing::lanes (*edit).size(), before, "one undo step");

            all = editing::lanes (*edit);
            const auto moving = all[0];
            editing::moveLane (*edit, moving, all.size() - 1);
            expect (editing::lanes (*edit).getLast() == moving, "to the bottom");
            editing::moveLane (*edit, moving, 0);
            expect (editing::lanes (*edit).getFirst() == moving, "back to the top");
            um.undo();
            expect (editing::lanes (*edit).getLast() == moving, "undo moves it back");
            um.undo();
            expect (editing::lanes (*edit).getFirst() == moving);

            all = editing::lanes (*edit);
            editing::muteOthers (*edit, all[1]);
            for (const auto& t : all) expect (t.isMuted() == ! (t == all[1]), "others muted");
            editing::muteOthers (*edit, all[1]);
            for (const auto& t : all) expect (! t.isMuted(), "again: all play");

            editing::soloAlone (*edit, all[0]);
            for (const auto& t : all) expect (t.isSoloed() == (t == all[0]), "soloed alone");
            editing::soloAlone (*edit, all[0]);
            for (const auto& t : all) expect (! t.isSoloed(), "again: no solo");
            lanes = editing::lanes (*edit);
        }

        expectEquals (editing::named ("Move", 1), juce::String ("Move clip"));
        expectEquals (editing::named ("Move", 3), juce::String ("Move 3 clips"));
    }
};

static EditingTests editingTests;
