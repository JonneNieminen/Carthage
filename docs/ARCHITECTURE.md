# Architecture

How Carthage is put together. Phase 1 built the layers, the device callback, recording, the plugin
scanner and the phase 1 window; the sections on the model, tools, grid and drawing describe what later
phases build and are still a proposal ([DECISIONS.md](DECISIONS.md) W6). Keep this file true: when the
code and this file disagree, fix one of them in the same commit.

What Carthage must do is in [BRIEF.md](BRIEF.md). This file is about how.

## Shape

```
┌──────────────────────────────── Carthage.app ────────────────────────────────┐
│  ui/        timeline · mixer · piano roll · plugin manager · settings · help │  message thread
│  ────────────────────────────── commands, model listeners ───────────────── │
│  app/       windows, menus, shortcuts, tools, projects, autosave, updates    │  message thread
│  ────────────────────────────── engine API (no UI types) ───────────────── │
│  engine/    session, routing, devices, recording, export, plugin hosting     │  audio + worker threads
│  ────────────────────────────────────────────────────────────────────────── │
│  Tracktion Engine  →  JUCE  →  CoreAudio / ASIO / WASAPI,  AU / VST3         │
└──────────────────────────────────────────────────────────────────────────────┘
        carthage-scan (separate process): loads one plugin at a time, reports, exits
        carthage-bench, carthage-snapshot (developer tools): measure engine/ on real hardware; render ui/ to PNG
```

Dependencies point downward only. `engine/` never includes anything from `ui/` or `app/`, so it can be
driven by tests and by a headless tool without a window.

## Source layout

```
CMakeLists.txt  CMakePresets.json    presets: debug, release, rtsan
cmake/               shared compile settings (warnings as errors, sanitizers)
external/            JUCE 9.0.5, tracktion_engine (git submodules, pinned: DECISIONS W7)
src/
  CMakeLists.txt     carthage_juce: JUCE + Tracktion compiled once (C++23), shared by every target
  engine/            carthage_engine (static library, no UI, runs headless)
    Session          owns tracktion::Engine, the edit, the track, input routing, the insert, takes
    AudioCallback    the one device callback: engine render, recorder, latency probe, meters, load
    TakeRecorder     audio thread -> lock-free FIFO -> writer thread -> WAV
    TakePlacement.h  where a take lands in edit time; TakeWatch ends a take whose playback stopped
    LatencyProbe     loopback round-trip measurement (chirp + cross-correlation)
    PluginScan       carthage-scan protocol, the known-plugin list, the blocklist (with why: incompatible,
                     crashed, hung, unusable), PluginScanner (background scan, several processes)
    PluginLibrary    the plugin browser's data: fuzzy search, filters, favourites, last used
    time/Grid        the one grid and snap resolver (beats; divisions, triplet, dotted, adaptive)
    RealtimeCheck.h  RealTimeSanitizer scope and CARTHAGE_NONBLOCKING
  app/               Carthage.app: application, window, menu bar, command table, path provider,
                     Updater (in-app updates behind one interface)
    platform/        the only place for OS-specific APIs (MicrophoneAccess_*, Updater_mac.mm
                     on Sparkle, Updater_win.cpp on WinSparkle, Updater_other.cpp)
  ui/                carthage_ui: MainComponent (transport, signal row, take lane), SettingsPanel,
                     UpdateBanner, PluginBrowser,
    theme/           Theme.h (mirror of design/tokens.css), Fonts.cpp (the embedded typefaces)
                     and the LookAndFeel
    Motion.h         motion that answers an action: Host, allowedFor, Tween (W45)
    platform/        the system's reduce-motion setting (Motion_mac.mm, Motion_win.cpp)
  scan/              carthage-scan
tools/bench/         carthage-bench: latency, soak, align, takes, wgprobe, devices, plugins;
                     libcarthage_bench_workgroups counts os_workgroup_join (bench only)
tools/snapshot/      carthage-snapshot: the window content rendered to PNG in fixed states
tests/               carthage-tests (juce::UnitTest), no-shared-mutex, rtsan/baseline.txt
scripts/             rtsan_check.py
.github/workflows/   ci.yml (build and test on every push), release.yml (on a v* tag)
.github/scripts/     msvc-env.ps1: the Visual Studio environment for Windows jobs
cmake/               CarthageOptions (warnings, sanitizers), CarthageVersion (tag -> versions),
                     Updaters (Sparkle / WinSparkle archives pinned by SHA-256)
packaging/           macos/ (sign.sh, make-dmg.sh, read-me), windows/ (Carthage.iss),
                     release_notes.py, make_appcast.py, update-public-key.txt
```

## The model

- **One source of truth.** The project is Tracktion's edit model. The UI never keeps its own copy of
  project state; it reads the model and listens for changes. View state that is not part of the project
  (zoom, scroll, selection, which tool is active) lives in `app/`, not in components.
- **Every change is a command.** A user action becomes one undoable transaction on the model's undo
  manager, with a name the Edit menu can show ("Trim clip", "Move 3 notes"). A drag is one transaction,
  not one per mouse move. If an action cannot be undone, that is a bug (brief, feature 10).
- **Routing is data, not structure** (F5). A timeline lane is only a place to put clips. Where sound
  goes is a property of the clip, instrument or input: a mixer channel id. Lanes and mixer channels are
  separate lists with separate orders. Map this onto Tracktion's tracks deliberately and write the
  mapping down here once phase 1 and phase 5 have proven it; do not let Tracktion's track-equals-channel
  default leak into the UI.
- **Grid and snap are one system** shared by the timeline and the piano roll (brief, features 2 and 7):
  one grid type, one snap resolver, one place that knows about triplets, dotted values and adaptive
  zoom. Neither view implements its own.
- **Tools and shortcuts are one table.** Each command has an id, a default key, a label and a tooltip.
  Menus, the shortcut editor, tooltips and the help page all read that table. Tool keys support
  hold-to-switch (F7). Keys are written platform-neutrally (primary modifier = Cmd on macOS, Ctrl on
  Windows) and never hard-coded in UI text.

### Phase 5a: lanes, channels and carriers (W47, W49)

The mapping onto Tracktion that F5 asked to be written down once proven:

- **A channel** is a Tracktion `AudioTrack` marked `carthageRole="channel"`, with its fader and meter
  (Tracktion's default plugins) and its insert Line. It holds no clips. Its output goes to master or
  to another channel (`setOutputToTrack`); a route that would loop back is refused (`editing::setOutput`).
- **A lane** is not a track: it is a `LANE` child of the edit's `CARTHAGELANES` (id, name, colour,
  height, mute, solo), edited with undo; mute and solo stay outside undo.
- **A carrier** is a hidden `AudioTrack` (`carthageRole="carrier"`, `carthageLane=<id>`, no plugins)
  holding one lane's clips that play through one route; it outputs into its channel, or to master.
  Carriers keep Tracktion's automatic crossfades within a lane, and lane mute and solo mute them.
  `editing::carrierFor` makes one when a clip first needs it.
- **Routing is per sample** (`editing::routeSample`): every clip of a sample moves to its lane's
  carrier for the new route. A new sound plays to master until routed.
- **Only what could sound is in the graph** (`editing::syncMix`, run by the session after every edit
  and undo): a carrier with clips; a channel with sound playing into it, plugins of its own or the
  live input. 50 empty channels cost nothing measurable (`carthage-bench idle-mixer`). The flags are
  written outside undo (see `setProcessing` in `engine/Mixer.cpp` for Tracktion's quirk).
- **The mixer view** (`ui/MixerView`) reads all of this and keeps nothing: master's strip and one
  per channel, rebuilt when the edit's tracks change; meters poll the channels' measurers at 30 Hz,
  the one thing in it that moves during playback. F9 shows it in place of the lanes (W48).
- **Inputs** (W51) are each channel's own: an interface input (mono or a stereo pair) and whether it
  is heard, kept on the channel's track state outside undo. `Session::applyInputRouting` connects
  each Tracktion wave input to every channel that takes it and is monitoring (Tracktion's monitor
  switch is per input, so a channel not monitoring is simply not connected). Inputs are grouped in
  stereo pairs for the whole interface if any channel takes a pair. One channel is the record
  channel: takes come from its input through Carthage's own recorder, onto the recording lane.
  The signal row is gone; its parts are channel features and master's output picker.
- **The live input** (before W51) played through channel 1, whose insert Line is the phase 1 input chain; takes go
  to the recording lane's carrier for channel 1.
- **Older projects** (before 5a) open with each old lane track turned into the carrier of lane n
  playing into channel n, so they sound the same (`editing::prepareMix`).

### Phase 4a: the timeline, its edits and the project file

- **Lanes.** In 4a each lane is a Tracktion `AudioTrack` that holds clips and no plugins; the phase 1
  input chain stays on its own track. Phase 5 routes each lane's clips to a mixer channel (F5) and
  writes the mapping here; until then a lane plays to the master output.
- **Time.** Positions are beats in the edit's tempo sequence; seconds are derived. The grid and the
  snap resolver (`engine/time/Grid.h`) work in beats, know nothing about drawing, and are tested on
  their own; the timeline and, in phase 6, the piano roll both call them.
- **Edits.** Each user action opens one transaction on `Edit::getUndoManager()` with a name
  ("Move clip", "Trim 3 clips"); a drag opens it on mouse-down and every change until mouse-up joins
  it. Undo and Redo are commands in the command table.
- **Project file** (`engine/Project.h`). A project is a folder: `<Name>.carthage` (the edit's XML,
  with a `CARTHAGE` child holding `formatVersion`, the project's `id` and a `VIEW` child that the
  timeline fills with zoom, scroll and grid), `Recordings/` and, in 4b, `Samples/`. Saving copies the
  edit on the message thread (`project::snapshot`) and a single writer thread (`project::Writer`)
  writes a temporary file beside the old one and renames it over, so a crash mid-save never leaves a
  half-written project. Clip paths are rewritten on the copy: inside the project folder relative to
  the file (W32), anything else absolute, so a moved folder keeps working. Save as copies
  `Recordings/` into the new folder and moves the open edit there (`project::moveTo`); an untitled
  project's old folder goes to the bin. A file from a newer format is refused with a message; an
  older one opens; a damaged one is never overwritten.
- **The session's edit is the open project.** `Session` starts with a new untitled project in
  `<projects>/Untitled` (or `Untitled 2`…), created only when something is recorded into it. New,
  Open and recovery build the new edit, then replace the old one (`onProjectClosing` lets plugin
  windows close first; `onProjectChanged` lets the timeline follow). Opening is not an edit: Tracktion
  registers the inputs with a new edit as an unnamed undo step a moment later, and the session drops
  that step once the edit settles (W33). Unsaved changes are Tracktion's changed flag.
- **Autosave and recovery** (`engine/Autosave.h`). After an edit, once the user pauses for 2 s and at
  most every 2 minutes (W30), the edit is copied on the message thread and written by the writer to
  `<settings>/Autosave/<project id>/<date time>.carthage`, keeping the last 10. Paths in an autosave
  are relative to the project file, so recovery opens it as that project. A `running` marker there
  names the project file; it is removed when the project closes cleanly. At launch, before the new
  session marks its own project, the app looks for markers: one whose newest autosave is newer than
  the project file means Carthage did not quit cleanly, and the newest such autosave is offered. A
  recovered project counts as unsaved until it is saved.

### Phase 4b: tabs, samples, the start screen

- **Several projects, one front.** `Session` holds the open projects in tab order
  (`Session::Project`: the edit, its first lane, file, view and autosave state) and an index of the
  front one. Everything that speaks of "the edit", the transport or the input means the front
  project. Bringing a tab forward (`showProject`) puts the old front one back first: its view is
  kept, its transport stops, its input targets are removed and its playback context is freed, so a
  background project costs no audio time while its plugins stay loaded (W30). The new front one
  allocates its context and takes the input (`applyInputRouting`); then `onProjectChanged` lets the
  UI follow. Switching the 300-clip demo and an empty project takes under 25 ms (carthage-bench
  projects). New and Open add a tab after the front one; an untitled, untouched front tab gives its
  place instead. Closing a tab asks to save (the app), then `closeProject` drops it; the last tab is
  never closed this way: closing it closes the window (W34). Quitting brings each unsaved tab
  forward in turn and asks. Every open project has its own autosave folder and `running` marker.
- **Copy and paste** (`editing::copy` / `paste`). Copy fills Tracktion's `Clipboard::Clips` with the
  selected clips' state (every property Tracktion keeps), each audio file written as an absolute
  path, positions in seconds from the first clip and lanes from the topmost one; the timeline holds
  it, so it outlives a tab switch. Paste puts it at the playhead of the front project on the same
  lanes; Tracktion makes new ids and writes the paths relative to the new project where it can. A
  clip pasted from another project keeps pointing at that project's file until its samples are
  collected.
- **Collected samples** (`Session::collectSamples`, W30). Clips may play files from anywhere; File >
  Collect samples into project copies every file the front project uses from outside its folder
  into `<project>/Samples/` (keeping names, numbering a clash with a different file of the same name,
  `project::collectTarget`) on the writer thread, then points the clips at the copies as one undo
  step. The "Copy samples into the project" setting runs it after every paste (and, with the sample
  browser, every drop). Save as copies `Samples/` along with `Recordings/` (`project::copyMedia`).
- **Samples** (`engine/SampleLibrary.h`). The folders the user adds, every WAV, AIFF, FLAC, MP3
  and OGG file under them (MP3 and Ogg through JUCE's own decoders), favourites and each file's
  key and tempo, in `<settings>/Samples.xml`. An indexer thread reads the folders and hands the
  finished index to the message thread in one piece; search reuses the plugin browser's fuzzy
  match over names and folders.
- **Preview** (`engine/SamplePreview.h`). A reader thread decodes the chosen file, converts it to the
  device rate and fills a fixed lock-free FIFO; `AudioCallback` adds it, at the preview's own
  volume, into the main output pair after the engine renders and before the latency probe. A new
  sound flushes the FIFO through the audio thread (a flush number it answers), so the audio thread
  never waits and never sees the FIFO change size.
- **The sample browser** (`ui/SampleBrowser.h`) shares the left panel with the plugin browser; a
  switch at the panel's top chooses one, remembered with the panel's width. Rows are folders (a
  tree, opened by click) and sounds (name, key and tempo, star); a press plays (or, in "play while
  held" mode, plays until the release). Sounds drag onto the timeline's lanes as
  `carthage-sample:<path>`; the lanes also take audio files from the Finder/Explorer. A drop shows
  a ghost of where and how long the sound lands, snapped like any edit, and is one undo step
  (`editing::insertFile`).
- **Key and tempo** (`engine/SampleAnalysis.h`). A background-priority thread analyses every listed
  sound that has no analysis yet (the first minute, mixed to mono) and stores the result in the
  library, which writes `Samples.xml` and tells the browser in batches. Tempo: spectral-flux onsets,
  their autocorrelation over 60 to 200 bpm, the faster of two related tempos when both are strong,
  and for a loop the tempo that makes it whole bars; none for one-shots. Key: a chromagram against
  the Krumhansl-Kessler profiles; none for noise and drums.
- **Time-stretching** (W36): Signalsmith Stretch through Tracktion; stretched clips and loops fitted
  to the tempo play from proxy files Tracktion renders in the background.
- **Start screen** (`ui/StartScreen.h`, F20). At launch unless a project was named or a recovery is
  offered, and from File > Start screen: recent projects (kept by the session, newest first, up to
  twelve) and templates (an empty project, then `<settings>/Templates`), each with its arrangement
  in miniature (`project::miniatureOf`, read from the file's XML on a worker). File > Save as
  template writes a copy of the front project there (`Session::saveCopy`). Settings > Projects
  switches it off.
- **The timeline across tabs.** Waveform thumbnails belong to audio files, not projects, so they are
  kept when the front project changes; resolved clip paths are keyed by project file.

### Phase 4c: views in their own windows, layouts

- **Views** (`MainComponent::View`): samples, plugins, timeline, signal path, Settings; later the
  mixer, piano roll, tuner, big clock, meters and help join the same list. A view is a component the
  main window owns. Detached, it is reparented into a `ViewWindow` (a native window: a slim strip with
  the way back, the view below; its close button also brings it back; the command manager's keys
  work there). Docked again, it returns to the main window, which lays out only the views it holds: a
  browser away leaves the panel to the other, the timeline away leaves a line and "Bring it back",
  the signal path away collapses. Pop-out buttons sit on each view; one command per view toggles it.
- **Layouts** (`getLayout` / `applyLayout`): a `LAYOUT` tree of the main window's bounds, the panel,
  Settings, each view's place and bounds, and the plugin window's place. Slots are engine properties
  `layout1`..`layout9`, how many offered is `layoutSlots` (Settings > Windows); the Layouts menu and
  Cmd+Opt+1-9 recall, Cmd+Opt+Shift+1-9 save, Cmd+Opt+0 is the default. Layouts are app-wide; a window
  whose display is gone comes to the main display (`onScreen`).
- **Last session**: at quit (and before an update restart) the saved projects open, the front one and
  the layout go to the `lastSession` property; at launch, with Settings > Projects > When it opens set
  to it, they reopen.
- **Command palette** (`ui/CommandPalette.h`, Cmd+K): every command that can run now, from the command
  manager, fuzzy-matched, with its menu and keys; Return runs it.
- Plugin windows are kept on top (`keepPluginOnTop`, Plugin menu) and take the last layout's place.

## Threads

| Thread | Does | Must never |
| --- | --- | --- |
| **Audio** (device callback, plus any engine render threads) | Render the graph: inputs, plugins, mix, meters' raw values, hand recorded samples to a FIFO | Allocate or free, lock, wait, do file or network I/O, log, call into the UI, touch the model directly |
| **Message** (UI) | All components, all model edits, all undo | Do work that can take longer than a frame: scanning, file decode, thumbnail building, export, saving large files |
| **Workers** (background) | Plugin scan supervision, waveform thumbnails, disk writer for recordings, autosave, export, update check | Touch components directly; edit the model except by posting back to the message thread |

### The device callback (phase 1, DECISIONS W14)

Carthage registers its own `AudioCallback` with the audio device instead of Tracktion's
`DeviceManager`, and calls the engine from inside it. In each block, in this order:

1. Tracktion renders: device inputs, the monitored input through the track's plugin, playback, the mix.
   Its worker threads (when it uses more than one core) join the device's audio workgroup.
2. `TakeRecorder::pushBlock` copies the raw input for the chosen channel(s) into its FIFO, with the
   engine's stream time for the block.
3. `LatencyProbe::process`, only while a measurement runs, owns the outputs.
4. Input peaks, audio-thread load and a missed-deadline count go to atomics the UI reads on its frame.

The whole block is one RealTimeSanitizer scope in the `rtsan` build. Carthage's code in it has no
violations; Tracktion's and JUCE's are listed in `tests/rtsan/baseline.txt` (DECISIONS F18).

### Startup

The window opens first. `app/platform` asks the OS for audio-input access without blocking (W20);
only then is the `Session` created, which opens the device. Teardown runs the other way: components,
then the engine.

### Recording (DECISIONS W13)

```
device thread: AudioCallback -> TakeRecorder::pushBlock -> juce::AbstractFifo (8 s, preallocated)
writer thread: polls every 4 ms -> juce::AudioFormatWriter (WAV 16/24/32-float) -> Recordings/
message thread: stop -> wait for the audio thread to acknowledge -> drain -> close -> insert clip
```

The clip's start is the take's first-sample stream time plus the stream-to-edit offset, plus one
block, minus the round trip (`engine/TakePlacement.h`, DECISIONS W22). The offset (edit time minus
stream time, `EditPlaybackContext::globalStreamTimeToEditTimeUnlooped`) is sampled every 5 ms on the
message thread while the take plays (`TakeWatch`), not at stop, because the playhead may have stopped
by then. If playback stops or jumps under a take, Session ends the take there, places it with the last
good offset and tells the UI (`Session::onTakeEnded`). The round trip is the driver's input + output
latency, or a loopback measurement if the user chooses it. The input and output pickers and the
device settings lock while a take records, because changing them rebuilds the engine's device list,
which stops playback. Tracktion's own recorder is not used: it locks and allocates on the audio thread.

Rules that follow:

- **Audio thread to UI:** atomics for single values, lock-free single-producer/single-consumer FIFOs for
  streams (meter values, recorded audio, MIDI activity). The UI polls on its frame timer. No async
  message posted from the audio thread.
- **UI to audio thread:** through the engine's own real-time-safe mechanisms (graph rebuilds swapped in
  atomically, parameter smoothing). Never a mutex the audio thread could wait on.
- **Recording** goes audio thread → lock-free FIFO → disk-writer thread → file (brief, feature 10). If
  the FIFO overflows, count and report the dropout; never block the audio thread to avoid it.
- **macOS audio workgroups:** every thread that renders audio joins the device's workgroup (F8). The
  device's own I/O thread is in it already; Tracktion's render threads join it because `Session` calls
  `EditPlaybackContext::enableAudioWorkgroup (true)` before the playback context exists (off by
  default in Tracktion). Carthage's writer and analysis threads are not audio threads and stay out.
- **The monitored input chain stays in-process** (F8): input → plugin chain → output with no hop to
  another process and no extra buffering. Latency is measured, not assumed (see [TESTING.md](TESTING.md)).
- **Plugin delay compensation** is on for playback and export. The low-latency monitoring mode
  (feature 8) is the one place it is deliberately relaxed for the armed channel; document exactly what
  that mode bypasses.

## Plugins

- **Hosting** is AU and VST3 through JUCE/Tracktion. No built-in effects or instruments, ever (F4):
  gain, pan, sends and meters are channel features, not plugins.
- **Scanning** happens in `carthage-scan`, a separate executable next to the app's executable (F9).
  The app runs it on one plugin (`--format VST3|AudioUnit --plugin <file or AU identifier>`) on a
  worker thread; it prints the plugin's descriptions as one `<PLUGINS>` XML element and exits 0, or 2
  if nothing loaded. A crash, a non-zero exit or a 60 s timeout puts the plugin on the blocklist.
  The engine's `shouldLoadPlugin` refuses anything the scanner has not confirmed (DECISIONS W15).
  Tracktion's own hook (`EngineBehaviour::canScanPluginsOutOfProcess`) is not used in phase 1; phase 3
  reads what it does in the pinned version and decides whether to build the manager on it or on this.
- **The monitored insert** in phase 1 is one `tracktion::ExternalPlugin` at index 0 of the track's
  plugin list, before the track's own volume/pan and meter, which are channel features.
- **The plugin list, blocklist, favourites and last-used list** are files in the settings folder, with a
  version field, so a newer Carthage can read an older cache.
- **A plugin's own editor window** is the plugin's business; Carthage gives it a plain host window and
  keeps its own UI responsive while a slow editor opens.

## Drawing

The brief asks for zoom and scroll at display refresh rate, including 120 Hz (feature 2). Design for it
from the first timeline commit:

- Animate from the display's frame callback, not from a fixed-rate timer.
- Draw only what is visible. Clips, notes and grid lines are culled to the viewport before any path is
  built.
- Waveforms come from cached thumbnails built on a worker thread, at several zoom levels. Painting
  never decodes audio.
- Playback moves the playhead and the meters and nothing else (see `AGENTS.md`). The playhead is drawn
  so that moving it does not repaint the clips under it.
- Measure frame time on the real machine. "Smooth" is a number in the phase report, not an impression.

Check the pinned JUCE's current rendering options on macOS and Windows before choosing how to draw;
do not assume the older software renderer is the only path.

## Files and folders

The help page must list every folder Carthage uses with a button to open each (feature 9), so exactly
one place knows these paths: `src/app/Paths.cpp`. The engine receives them; nothing else builds a path
to a user folder (DECISIONS W17).

| What | macOS | Windows |
| --- | --- | --- |
| Settings (audio device, later shortcuts and updates) | `~/Library/Application Support/Carthage` | `%APPDATA%\Carthage` |
| Plugin scan cache (`KnownPlugins.xml`, `Blocklist.xml`) | `<settings>/Plugins` | same |
| Projects | `~/Music/Carthage` | `Music\Carthage` |
| A new untitled project | `<projects>/Untitled` (then `Untitled 2`, …), created when something is recorded | same |
| Recordings | `<project>/Recordings` | same |
| Collected samples | `<project>/Samples` | same |
| Templates (File > Save as template) | `<settings>/Templates/<name>/` | same |
| The sample browser's folders, favourites, keys and tempos | `<settings>/Samples.xml` | same |
| Shortcuts the user changed (only the changes) | `<settings>/Shortcuts.xml` | same |
| Autosaves and crash recovery | `<settings>/Autosave/<project id>/` (the last 10, and the `running` marker) | same |
| AU and VST3 system folders (read only) | `/Library/Audio/Plug-Ins/{Components,VST3}`, and under `~` | `C:\Program Files\Common Files\VST3` |

Project files carry a format version from the first commit that writes one (phase 4); a project saved
by an older Carthage must always open. The plugin cache files carry a version already.

## Updates

Sparkle 2 on macOS and WinSparkle on Windows sit behind one small interface in `app/` (check, state,
install-on-quit, restart-now), so the banner and the settings toggle are written once. The updater
asks the app whether it may restart; the app says no while playing, recording or holding unsaved work
(F10). Pipeline details are in [RELEASING.md](RELEASING.md).

## Third-party code

| Dependency | Role | How it is brought in |
| --- | --- | --- |
| JUCE 9.0.5 | UI, audio devices, plugin hosting | Submodule `external/JUCE`, tag 9.0.5 (W7) |
| Tracktion Engine (develop) | Edit model, playback graph, time-stretch | Submodule `external/tracktion_engine`, commit c6293127 (W7); its nested JUCE is not initialised |
| Sparkle 2 | macOS updates | Decide in phase 2: pinned release binary or package manager; record it here |
| WinSparkle | Windows updates | Same |

Before using an API from any of these, read the pinned version's own headers, examples and changelog.
Both JUCE and Tracktion change between major versions; older tutorials and remembered APIs are often
out of date (brief, "Fixed decisions").
