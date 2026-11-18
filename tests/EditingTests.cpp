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
        const auto folder = folderHolder.getFile();
        folder.createDirectory();
        const auto audio = makeAudio (folder);

        tracktion::Engine engine ("CarthageTests", nullptr, std::make_unique<HeadlessBehaviour>());
        auto edit = tracktion::createEmptyEdit (engine, folder.getChildFile ("Test.tracktionedit"));
        editing::useConsistentFilePaths (*edit);
        edit->ensureNumberOfAudioTracks (2);
        edit->tempoSequence.getTempo (0)->setBpm (120.0);

        auto lanes = editing::lanes (*edit);
        auto& um = edit->getUndoManager();

        const auto newClip = [&] (double beat)
        {
            auto c = tracktion::insertWaveClip (*lanes[0], "Tone", audio,
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
            expectEquals (editing::laneOf (*editing::lanes (*edit)[1]->getClips().getFirst()), 1, "on the second lane");
            editing::remove ({ editing::lanes (*edit)[1]->getClips().getFirst() });
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

            auto* painted = editing::placeCopy (*c, *lanes[1], 16.0);
            expect (painted != nullptr && editing::laneOf (*painted) == 1 && std::abs (editing::startBeat (*painted) - 16.0) < 1.0e-6,
                    "a copy placed on another lane");

            editing::begin (*edit, "Add marker");
            expect (editing::addMarker (*edit, 4.0) != nullptr);
            editing::setLoopRange (*edit, 8.0, 4.0);
            near (edit->getTransport().getLoopRange().getStart().inSeconds(), 2.0, "loop starts at beat 4");
            near (edit->getTransport().getLoopRange().getEnd().inSeconds(), 4.0, "and ends at beat 8");
        }

        beginTest ("Copy and paste into another project, in another folder and tempo (W30)");
        {
            for (auto* l : lanes)
                for (auto* c : l->getClips())
                    editing::remove ({ c });

            auto* a = newClip (2.0);
            auto* b = tracktion::insertWaveClip (*lanes[1], "Tone 2", audio,
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
            target->ensureNumberOfAudioTracks (8);
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
            expectEquals (editing::lanes (*target)[0]->getClips().size() + editing::lanes (*target)[1]->getClips().size(), 0,
                          "a paste undoes in one step");
        }

        beginTest ("A dropped sound lands at the beat, as long as the file (W30)");
        {
            auto* dropped = editing::insertFile (*lanes[1], audio, 6.0);
            expect (dropped != nullptr);
            near (editing::startBeat (*dropped), 6.0, "at the drop beat");
            near (dropped->getPosition().getLength().inSeconds(), 4.0, "the whole 4-second file");
            expectEquals (editing::laneOf (*dropped), 1);
            expect (editing::insertFile (*lanes[1], folder.getChildFile ("missing.wav"), 0.0) == nullptr, "a missing file adds nothing");
        }

        expectEquals (editing::named ("Move", 1), juce::String ("Move clip"));
        expectEquals (editing::named ("Move", 3), juce::String ("Move 3 clips"));
    }
};

static EditingTests editingTests;
