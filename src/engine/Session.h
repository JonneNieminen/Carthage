#pragma once

#include "AudioCallback.h"
#include "Autosave.h"
#include "LatencyProbe.h"
#include "PluginLibrary.h"
#include "PluginScan.h"
#include "Project.h"
#include "SampleAnalysis.h"
#include "SampleLibrary.h"
#include "SamplePreview.h"
#include "TakePlacement.h"
#include "TakeRecorder.h"

#include <tracktion_engine/tracktion_engine.h>

#include <optional>

namespace carthage::engine
{
    /** Folders the engine writes to. The app's path provider decides them (AGENTS.md: "Paths to
        user folders come from one place in app/"); the engine only receives them. */
    struct Folders
    {
        juce::File settings;      ///< Audio device settings (the engine's property storage)
        juce::File pluginCache;   ///< Known plugins and the blocklist
        juce::File project;       ///< The folder of the untitled project the session starts with
        juce::File autosave;      ///< Autosaves and crash markers; none (tools, tests) turns autosave off
    };

    /** Phase 1's engine: one audio interface, one input, one plugin, one lane of takes.

        It owns the Tracktion engine and an edit with a single audio track. The chosen input is
        monitored live through the track's plugin, in-process (F8). Takes are recorded by
        TakeRecorder, not by the engine's own recorder, because the engine's path takes locks and
        allocates on the audio thread (docs/ARCHITECTURE.md, "Recording"). A finished take becomes
        a clip on the track at the position it was played, corrected for the interface's latency.

        Several projects can be open at once, as tabs (phase 4b, W30). Each has its own edit; only
        the front one is attached to the audio device, plays and hears the input. The others keep
        their plugins loaded but have no playback graph, so they cost no audio time. Everything
        below that speaks of "the project", "the edit" or the transport means the front one. New
        and Open add a tab (taking the place of an untouched untitled one); switching or closing
        the front tab calls onProjectClosing and then onProjectChanged, so the UI lets go of the old
        edit and takes up the new one. Saves and autosaves are written by a worker; the edit is
        copied on the message thread first.

        All methods: message thread.
    */
    class Session final : private juce::ChangeListener,
                          private juce::Timer
    {
    public:
        Session (Folders, juce::File scannerExecutable);
        ~Session() override;

        tracktion::Engine& getEngine() noexcept                 { return *engine; }
        juce::AudioDeviceManager& getDeviceManager() noexcept   { return engine->getDeviceManager().deviceManager; }
        tracktion::Edit& getEdit() noexcept                     { return *front().edit; }
        tracktion::AudioTrack& getTrack() noexcept              { return *front().track; }
        AudioCallback& getCallback() noexcept                   { return *callback; }
        TakeRecorder& getRecorder() noexcept                    { return recorder; }
        LatencyProbe& getProbe() noexcept                       { return probe; }
        SamplePreview& getPreview() noexcept                    { return preview; }
        SampleLibrary& getSampleLibrary() noexcept              { return *sampleLibrary; }
        PluginCatalogue& getPlugins() noexcept                  { return *plugins; }
        PluginScanner& getPluginScanner() noexcept              { return *pluginScanner; }
        PluginLibrary& getPluginLibrary() noexcept              { return *library; }

        /** After the scanner confirmed or blocked a plugin, and after a scan: the library is
            already up to date when this is called. */
        std::function<void()> onPluginsChanged;
        const Folders& getFolders() const noexcept              { return folders; }
        const juce::File& getScanner() const noexcept           { return scanner; }

        //==========================================================================================
        // The front project
        const juce::File& getProjectFile() const noexcept   { return front().file; }
        juce::File getProjectFolder() const                 { return getProjectFile().getParentDirectory(); }
        juce::String getProjectName() const                 { return getProjectFolder().getFileName(); }

        /** Never saved: Save has to ask where. */
        bool isUntitled() const noexcept                    { return front().untitled; }
        bool hasUnsavedChanges() const                      { return front().hasUnsavedChanges(); }

        /** The view state the project was opened with, or had when it last went to the back (the
            UI's own; may be invalid). */
        const juce::ValueTree& getOpenedView() const noexcept   { return front().info.view; }

        //==========================================================================================
        // The open projects (tabs), in tab order
        struct ProjectSummary
        {
            juce::String name;
            juce::File file;
            bool unsaved = false, untitled = false;
        };

        int getNumProjects() const noexcept                 { return static_cast<int> (projects.size()); }
        int getFrontIndex() const noexcept                  { return frontIndex; }
        ProjectSummary describeProject (int index) const;

        /** Brings a tab to the front: it plays and hears the input from now on. Refused while
            recording. */
        juce::Result showProject (int index);

        /** Closes the front project without asking (the app asks first) and brings its neighbour
            forward. The last project is never closed this way: closing it closes the window. */
        void closeProject();

        /** Moves a tab (dragged in the tab strip). */
        void moveProject (int from, int to);

        /** The UI's view state, asked for whenever the project is written. */
        std::function<juce::ValueTree()> getViewState;

        /** About to replace the edit: plugin windows and anything else holding its objects close. */
        std::function<void()> onProjectClosing;

        /** The edit was replaced (new, open, recover): everything holding the old one lets go. */
        std::function<void()> onProjectChanged;

        /** The name, the unsaved state or the folder changed (the window title follows). */
        std::function<void()> onProjectStateChanged;

        /** A new, empty, untitled project in a new tab, whose folder (created when something is
            recorded) is `folder`. `templateFile`, if given, is a project to start from. */
        juce::Result newProject (const juce::File& folder, const juce::File& templateFile = {});

        /** Opens a `.carthage` file (or a project folder holding one) in a new tab, or brings it to
            the front if it is open already. */
        juce::Result openProject (const juce::File&);

        /** Opens the autosave of a project Carthage did not close cleanly, in a new tab. The
            project counts as unsaved until it is saved. */
        juce::Result recover (const autosave::Store::Recovery&);

        /** Writes the project to its file in the background; `done` runs on the message thread. */
        void save (std::function<void (juce::Result)> done);

        /** Writes the project into `folder` (created if needed) as `<folder>/<name>.carthage`,
            copying its recordings along, and carries on there. */
        void saveAs (const juce::File& folder, std::function<void (juce::Result)> done);

        /** Copies every audio file the front project uses from outside its folder into its
            Samples folder, then points the clips at the copies, as one undo step ("Collect
            samples", W30). The copying runs on the writer; `done` gets how many files were copied.
            Files that no longer exist are left as they are. */
        void collectSamples (std::function<void (juce::Result, int)> done = {});

        /** Settings: collect a project's samples whenever clips from outside it arrive (W30). */
        bool getAlwaysCopySamples() const;
        void setAlwaysCopySamples (bool);

        /** What Carthage opens with (F20, F21): the start screen, the last session, or an empty
            project. Older settings with the start screen switched off read as an empty project. */
        enum class AtLaunch { startScreen, lastSession, emptyProject };
        AtLaunch getAtLaunch() const;
        void setAtLaunch (AtLaunch);

        /** Settings: a dropped loop whose tempo is known follows the project's tempo (W30). */
        bool getFitLoopsToTempo() const;
        void setFitLoopsToTempo (bool);

        /** Writes a copy of the front project into `folder` (with its recordings and samples) and
            carries on in the original: Save as template. */
        void saveCopy (const juce::File& folder, std::function<void (juce::Result)> done);

        /** Projects opened or saved, newest first, those still on disk (F20, the start screen). */
        juce::Array<juce::File> getRecentProjects() const;
        void forgetRecentProject (const juce::File&);

        /** Quitting: waits for writes in hand and records that every project closed cleanly. */
        void close();

        /** Null when the session was made without an autosave folder. */
        autosave::Store* getAutosaves() noexcept    { return autosaves ? &*autosaves : nullptr; }
        void setAutosavePolicy (autosave::Policy p) noexcept   { autosavePolicy = p; }

        /** Writes an autosave now if one is due (the timer calls it; tests call it directly). */
        void autosaveIfDue();

        //==========================================================================================
        // Input
        void setInput (int firstDeviceChannel, bool stereo);
        int getInputChannel() const noexcept        { return inputChannel; }
        bool isInputStereo() const noexcept         { return inputStereo; }
        void setMonitoring (bool);
        bool isMonitoring() const noexcept          { return monitoring; }

        //==========================================================================================
        // The insert. Only descriptions the out-of-process scanner confirmed are accepted.
        juce::Result insertPlugin (const juce::PluginDescription&);
        tracktion::Plugin* getInsertedPlugin() const;
        void removePlugin();

        //==========================================================================================
        // Transport and takes
        void setRecordFormat (TakeRecorder::SampleFormat f) noexcept    { recordFormat = f; }
        TakeRecorder::SampleFormat getRecordFormat() const noexcept    { return recordFormat; }

        juce::Result startRecording();
        TakeRecorder::Result stopRecording();
        bool isRecording() const noexcept       { return recorder.isRecording(); }

        /** Called when Session ends a take by itself because playback stopped under it (an engine
            graph rebuild after a device change can stop the transport). The take is saved and
            placed in time up to that point; TakeInfo::interrupted is set. */
        std::function<void (const TakeRecorder::Result&)> onTakeEnded;

        void play();
        void stop();
        bool isPlaying() const;
        void returnToStart();
        double getPositionSeconds() const;

        /** Where the last take landed, for reporting: its file, its position, the correction used. */
        struct TakeInfo
        {
            TakeRecorder::Result recording;
            double startSeconds = 0.0;
            int alignmentSamples = 0;
            juce::String clipName;
            bool interrupted = false;   ///< Playback stopped during the take, so Session ended it
            bool placedByGuess = false; ///< Playback never ran during the take; placed where it began
        };
        const std::optional<TakeInfo>& getLastTake() const noexcept     { return lastTake; }

        //==========================================================================================
        // Latency
        struct ReportedLatency
        {
            int inputSamples = 0, outputSamples = 0, bufferSize = 0;
            double sampleRate = 0.0;

            int roundTripSamples() const noexcept   { return inputSamples + outputSamples; }
            double roundTripMs() const noexcept     { return sampleRate > 0 ? roundTripSamples() * 1000.0 / sampleRate : 0.0; }
        };

        /** What the interface's driver says, which is what the app displays. */
        ReportedLatency getReportedLatency() const;

        /** Takes are shifted earlier by the round trip. By default that is the driver's figure; a
            loopback measurement can replace it. */
        void setMeasuredRoundTrip (std::optional<int> samples) noexcept    { measuredRoundTrip = samples; }
        std::optional<int> getMeasuredRoundTrip() const noexcept           { return measuredRoundTrip; }
        int getAlignmentSamples() const;

        /** The loopback cable's channels (device channel indices, from 0) for the current interface,
            remembered across launches. Until one is saved: the first output not used for
            listening, and the first input not used for the guitar. */
        struct LoopbackChannels
        {
            int output = 0, input = 0;
        };
        LoopbackChannels getLoopbackChannels() const;
        void setLoopbackChannels (LoopbackChannels);

        /** Called after the device changed (rate, buffer, channels). */
        std::function<void()> onDeviceChanged;

    private:
        Folders folders;
        juce::File scanner;
        PluginCatalogue* loadGate = nullptr;   // read by the engine's plugin load gate
        std::unique_ptr<PluginCatalogue> plugins;
        std::unique_ptr<PluginLibrary> library;
        std::unique_ptr<PluginScanner> pluginScanner;   // after the catalogue: destroyed first
        std::unique_ptr<SampleLibrary> sampleLibrary;
        std::unique_ptr<SampleAnalyser> sampleAnalyser;   // after the library: stopped first
        std::unique_ptr<tracktion::Engine> engine;
        TakeRecorder recorder;
        LatencyProbe probe;
        SamplePreview preview;
        std::unique_ptr<AudioCallback> callback;

        /** One open project: its edit and everything that follows it around. */
        struct Project final : public juce::ChangeListener   // its own undo manager
        {
            Project (Session&, std::unique_ptr<tracktion::Edit>, const juce::File&, project::Info, bool isUntitled);
            ~Project() override;

            Session& session;
            std::unique_ptr<tracktion::Edit> edit;
            tracktion::AudioTrack* track = nullptr;   // the first lane: the input and its takes
            juce::File file, recordings;
            project::Info info;
            bool untitled = true, recovered = false;

            // Autosave (W30): after an edit, once the user pauses.
            bool changedSinceAutosave = false;
            double lastChange = 0.0;
            std::optional<double> lastAutosave;
            juce::uint64 changeCount = 0;
            bool namedChangeSinceAdopt = false;
            juce::TimedCallback settleTimer { [this] { settle(); } };

            bool hasUnsavedChanges() const   { return recovered || edit->hasChangedSinceSaved(); }

            /** Untitled and never touched: New and Open take its place instead of adding a tab. */
            bool isPristine() const;

            void settle();
            void changeListenerCallback (juce::ChangeBroadcaster*) override;
        };

        std::vector<std::unique_ptr<Project>> projects;
        int frontIndex = -1;

        Project& front() const noexcept                 { return *projects[static_cast<size_t> (frontIndex)]; }

        std::optional<autosave::Store> autosaves;
        autosave::Policy autosavePolicy;
        project::Writer writer;
        juce::TimedCallback autosaveTimer { [this] { autosaveIfDue(); } };

        /** Sets up a new or opened edit and puts it in a tab (replacing a pristine front one). */
        void add (std::unique_ptr<tracktion::Edit>, const juce::File& file, project::Info, bool isUntitled, bool isNew);
        void putToBack();            // the front project lets go of the device and the input
        void bringForward (int);     // and another takes them up
        void savedAt (Project&, juce::uint64 changesWhenTaken);
        void noteRecent (const juce::File&);
        bool isOpen (const Project*) const;   // a tab may close while its file is being written
        void projectStateChanged();
        juce::ValueTree currentView() const;

        int inputChannel = 0;
        bool inputStereo = false, monitoring = true;
        TakeRecorder::SampleFormat recordFormat = TakeRecorder::SampleFormat::int24;
        std::optional<int> measuredRoundTrip;
        std::optional<TakeInfo> lastTake;
        int takeNumber = 0;
        double preparedRate = 0.0;
        int preparedInputs = -1;
        unsigned groupedChannels = 0;
        bool routing = false;

        // Take placement: the stream-to-edit offset is sampled while the take plays (TakeWatch),
        // not at stop, when the playhead may already have stopped.
        TakeWatch takeWatch;
        juce::uint32 takeStartedMs = 0;
        double takeStartPosition = 0.0;
        int takeBlockSamples = 0;

        std::optional<double> currentStreamToEditOffset() const;
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void prepareForDevice();
        void applyInputRouting();
        tracktion::WaveInputDevice* findWaveInput() const;
    };
}
