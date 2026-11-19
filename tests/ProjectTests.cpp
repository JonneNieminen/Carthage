#include <engine/Autosave.h>
#include <engine/Editing.h>
#include <engine/Project.h>

using namespace carthage;

namespace
{
    struct HeadlessBehaviour final : tracktion::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override   { return false; }
    };

    juce::File writeTone (const juce::File& file, double seconds)
    {
        file.getParentDirectory().createDirectory();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0)
                                                                                 .withNumChannels (1).withBitsPerSample (16));
        juce::AudioBuffer<float> b (1, static_cast<int> (48000.0 * seconds));
        b.clear();
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    }

    tracktion::Clip* addClip (tracktion::Edit& edit, int lane, const juce::File& audio, double startSeconds)
    {
        auto c = tracktion::insertWaveClip (*editing::lanes (edit)[lane], audio.getFileNameWithoutExtension(), audio,
                                            { { tracktion::TimePosition::fromSeconds (startSeconds), tracktion::TimeDuration::fromSeconds (1.0) } },
                                            tracktion::DeleteExistingClips::no);
        return c.get();
    }

    /** With '/' on every platform (Windows writes relative paths with '\\'). */
    juce::String sourceOf (const tracktion::Clip& c)   { return c.state[tracktion::IDs::source].toString().replaceCharacter ('\\', '/'); }
}

class ProjectTests final : public juce::UnitTest
{
public:
    ProjectTests() : juce::UnitTest ("Projects and autosave", "engine") {}

    void runTest() override
    {
        juce::TemporaryFile holder;
        const auto& root = holder.getFile();
        root.createDirectory();

        tracktion::Engine engine ("CarthageTests", nullptr, std::make_unique<HeadlessBehaviour>());

        const auto songFolder = root.getChildFile ("Song");
        const auto songFile = project::fileIn (songFolder);
        const auto take = writeTone (project::recordingsIn (songFolder).getChildFile ("Take 1.wav"), 2.0);
        const auto sample = writeTone (root.getChildFile ("Elsewhere/Kick.wav"), 0.5);

        beginTest ("A project is a folder holding <name>.carthage");
        expectEquals (songFile.getFileName(), juce::String ("Song.carthage"));
        expect (songFile.getParentDirectory() == songFolder);

        auto info = project::Info::createNew();
        expect (info.id.isNotEmpty());

        beginTest ("Save and reopen: clips, lanes, tempo, view and id come back the same");
        {
            auto edit = project::createEdit (engine, songFile);
            edit->ensureNumberOfAudioTracks (3);
            edit->tempoSequence.getTempo (0)->setBpm (97.0);
            editing::lanes (*edit)[1]->setName ("Bass");
            addClip (*edit, 0, take, 0.5);
            addClip (*edit, 2, sample, 3.0);
            editing::addMarker (*edit, 8.0);
            editing::setNotes (*edit, "Verse riff in drop D.\nTry a slower take.");

            info.view = juce::ValueTree ("VIEW");
            info.view.setProperty ("pixelsPerBeat", 48.0, nullptr);

            const auto snap = project::snapshot (*edit, songFile, info);
            expect (project::write (snap, songFile).wasOk());
            expect (songFile.existsAsFile());
            expect (! songFile.getSiblingFile (songFile.getFileNameWithoutExtension() + ".tmp").exists(), "no temporary file left");

            // The live edit is untouched by the snapshot.
            expect (! edit->state.getChildWithName ("CARTHAGE").isValid());

            auto opened = project::open (engine, songFile, songFile);
            expect (opened.edit != nullptr, opened.error);
            auto& e = *opened.edit;
            expectEquals (opened.info.id, info.id);
            expectEquals (opened.info.formatVersion, project::formatVersion);
            expectEquals (static_cast<double> (opened.info.view.getProperty ("pixelsPerBeat")), 48.0);
            expect (! e.state.getChildWithName ("CARTHAGE").isValid(), "the CARTHAGE child is not left in the edit");
            expectWithinAbsoluteError (e.tempoSequence.getTempo (0)->getBpm(), 97.0, 1.0e-9);

            const auto lanes = editing::lanes (e);
            expectEquals (lanes.size(), 3);
            expectEquals (lanes[1]->getName(), juce::String ("Bass"));
            expectEquals (lanes[0]->getClips().size(), 1);
            expectEquals (lanes[2]->getClips().size(), 1);
            expectWithinAbsoluteError (lanes[0]->getClips()[0]->getPosition().getStart().inSeconds(), 0.5, 1.0e-9);
            expectEquals (e.getMarkerManager().getMarkers().size(), 1);
            expectEquals (editing::notes (e), juce::String ("Verse riff in drop D.\nTry a slower take."), "the notes come back");

            // Recordings inside the folder are written relative to the file (W32); outside, absolute.
            expectEquals (sourceOf (*lanes[0]->getClips()[0]), juce::String ("../Recordings/Take 1.wav"));
            expectEquals (sourceOf (*lanes[2]->getClips()[0]), sample.getFullPathName().replaceCharacter ('\\', '/'));

            auto* audio = dynamic_cast<tracktion::AudioClipBase*> (lanes[0]->getClips()[0]);
            expect (audio != nullptr && audio->getAudioFile().getFile() == take, "the take's audio is found");
        }

        beginTest ("Save as into another folder: recordings are copied and paths point at the copies");
        {
            auto opened = project::open (engine, songFile, songFile);
            auto& e = *opened.edit;
            const auto copyFolder = root.getChildFile ("Elsewhere/Song copy");
            const auto copyFile = project::fileIn (copyFolder);

            const auto snap = project::snapshot (e, copyFile, opened.info, songFolder);
            expect (project::copyMedia (songFolder, copyFolder).wasOk());
            expect (project::write (snap, copyFile).wasOk());
            expect (project::recordingsIn (copyFolder).getChildFile ("Take 1.wav").existsAsFile(), "the take was copied");

            auto copy = project::open (engine, copyFile, copyFile);
            expect (copy.edit != nullptr, copy.error);
            auto* takeClip = dynamic_cast<tracktion::AudioClipBase*> (editing::lanes (*copy.edit)[0]->getClips()[0]);
            expect (takeClip->getAudioFile().getFile() == project::recordingsIn (copyFolder).getChildFile ("Take 1.wav"),
                    "the copy plays its own recording");
            auto* kick = dynamic_cast<tracktion::AudioClipBase*> (editing::lanes (*copy.edit)[2]->getClips()[0]);
            expect (kick->getAudioFile().getFile() == sample, "a sample outside the project still points at itself");

            // Relinking the live edit after the move: its clips follow too.
            project::moveTo (e, copyFile, songFolder);
            auto* live = dynamic_cast<tracktion::AudioClipBase*> (editing::lanes (e)[0]->getClips()[0]);
            expect (live->getAudioFile().getFile() == project::recordingsIn (copyFolder).getChildFile ("Take 1.wav"));
        }

        beginTest ("The start screen's miniature reads lanes and clips from the file");
        {
            const auto m = project::miniatureOf (songFile);
            expect (m.has_value());

            if (m.has_value())
            {
                expect (m->lanes.size() >= 3, "every lane");
                int clips = 0;
                for (const auto& lane : m->lanes) clips += static_cast<int> (lane.size());
                expect (clips >= 2, "the take and the sample");
                expect (m->seconds > 0.0);
            }
            expect (! project::miniatureOf (root.getChildFile ("nothing.carthage")).has_value(), "no file, no miniature");
        }

        beginTest ("Collected samples: names are kept, a clash is numbered, Save as carries them along");
        {
            const auto samples = project::samplesIn (root.getChildFile ("Collecting"));
            samples.createDirectory();
            const auto other = writeTone (root.getChildFile ("Other place/Kick.wav"), 0.3);   // a different Kick: another size

            const auto first = project::collectTarget (samples, sample, {});
            expectEquals (first.getFileName(), juce::String ("Kick.wav"), "a sample keeps its name");
            expect (sample.copyFileTo (first));
            expect (project::collectTarget (samples, sample, {}) == first, "the same file already there is reused");
            expectEquals (project::collectTarget (samples, other, {}).getFileName(), juce::String ("Kick 2.wav"),
                          "a different file with the same name is numbered");
            expectEquals (project::collectTarget (samples, other, { samples.getChildFile ("Kick 2.wav") }).getFileName(), juce::String ("Kick 3.wav"),
                          "and so is one taken by another sample in the same batch");

            const auto moved = root.getChildFile ("Collected elsewhere");
            expect (project::copyMedia (samples.getParentDirectory(), moved).wasOk());
            expect (project::samplesIn (moved).getChildFile ("Kick.wav").existsAsFile(), "Save as copies the Samples folder");
        }

        beginTest ("A file from a newer Carthage is refused, an older or plain edit opens, junk is refused");
        {
            auto xml = juce::parseXML (songFile);
            xml->getChildByName ("CARTHAGE")->setAttribute ("formatVersion", project::formatVersion + 1);
            const auto newer = root.getChildFile ("Newer/Newer.carthage");
            newer.getParentDirectory().createDirectory();
            xml->writeTo (newer);
            auto n = project::open (engine, newer, newer);
            expect (n.edit == nullptr && n.error.contains ("newer Carthage"), n.error);

            xml->removeChildElement (xml->getChildByName ("CARTHAGE"), true);
            const auto plain = root.getChildFile ("Plain/Plain.carthage");
            plain.getParentDirectory().createDirectory();
            xml->writeTo (plain);
            auto p = project::open (engine, plain, plain);
            expect (p.edit != nullptr, "an edit without the CARTHAGE child opens as version 1");
            expect (p.info.id.isNotEmpty(), "and gets an id");

            const auto junk = root.getChildFile ("Junk/Junk.carthage");
            junk.getParentDirectory().createDirectory();
            junk.replaceWithText ("not a project");
            expect (project::open (engine, junk, junk).edit == nullptr);
            expect (junk.loadFileAsString() == "not a project", "a damaged file is never overwritten");
        }

        beginTest ("Autosave timing: after a 2 s pause, at most every 2 minutes (W30)");
        {
            const autosave::Policy policy;
            expect (! policy.isDue (10.0, false, 0.0, std::nullopt), "nothing changed");
            expect (! policy.isDue (10.0, true, 9.0, std::nullopt), "still editing");
            expect (policy.isDue (10.0, true, 8.0, std::nullopt), "paused 2 s, first autosave");
            expect (! policy.isDue (100.0, true, 90.0, 10.0), "paused, but the last autosave was 90 s ago");
            expect (policy.isDue (130.0, true, 90.0, 10.0), "paused and 2 minutes since the last");
            expect (! policy.isDue (130.0, true, 129.0, 10.0), "2 minutes, but not paused");
        }

        beginTest ("Autosaves keep the newest 10; a left-behind marker is found; a clean close clears it");
        {
            const autosave::Store store (root.getChildFile ("Autosave"));
            auto edit = project::createEdit (engine, songFile);
            addClip (*edit, 0, take, 0.0);
            const auto snap = project::snapshot (*edit, songFile, info);

            store.markOpen (info.id, songFile);

            for (int i = 0; i < 12; ++i)
            {
                const auto f = store.nextFile (info.id);
                expect (project::write (snap, f).wasOk());
                f.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (60.0 + i));
                store.prune (info.id, 10);
            }

            expectEquals (store.autosavesOf (info.id).size(), 10);

            auto unclosed = store.findUnclosed();
            expectEquals (static_cast<int> (unclosed.size()), 1);
            expect (unclosed.front().projectFile == songFile);
            expect (unclosed.front().autosave == store.autosavesOf (info.id)[0], "the newest autosave");

            // Recovering reads the autosave but lives at the project file: paths still resolve.
            auto recovered = project::open (engine, unclosed.front().autosave, songFile);
            expect (recovered.edit != nullptr, recovered.error);
            auto* clip = dynamic_cast<tracktion::AudioClipBase*> (editing::lanes (*recovered.edit)[0]->getClips()[0]);
            expect (clip->getAudioFile().getFile() == take, "a recovered project finds its recordings");

            store.markClosed (info.id);
            expect (store.findUnclosed().empty());

            // A project saved after its last autosave has nothing to recover.
            store.markOpen (info.id, songFile);
            songFile.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::hours (1.0));
            expect (store.findUnclosed().empty(), "the saved file is newer");
            expect (! store.folderFor (info.id).getChildFile ("running").exists(), "and the marker is cleared");
        }
    }
};

static ProjectTests projectTests;
