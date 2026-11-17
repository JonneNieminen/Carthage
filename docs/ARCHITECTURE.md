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
    LatencyProbe     loopback round-trip measurement (chirp + cross-correlation)
    PluginScan       carthage-scan protocol, the known-plugin list and the blocklist
    RealtimeCheck.h  RealTimeSanitizer scope and CARTHAGE_NONBLOCKING
  app/               Carthage.app: application, window, menu bar, command table, path provider
    platform/        the only place for OS-specific APIs (MicrophoneAccess_mac.mm, ..._other.cpp)
  ui/                carthage_ui: MainComponent (transport, signal row, take lane), SettingsPanel,
    theme/           Theme.h (mirror of design/tokens.css) and the LookAndFeel
  scan/              carthage-scan
tools/bench/         carthage-bench: latency, soak, align, devices, plugins
tools/snapshot/      carthage-snapshot: the window content rendered to PNG in fixed states
tests/               carthage-tests (juce::UnitTest), no-shared-mutex, rtsan/baseline.txt
scripts/             rtsan_check.py
.github/workflows/   ci.yml, release.yml (phase 2)
packaging/           macos/, windows/ (phase 2)
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

The clip's start is the edit time the playhead mapped the take's first block to
(`EditPlaybackContext::globalStreamTimeToEditTimeUnlooped`, read while the transport still rolls),
minus the round trip: the driver's input + output latency, or a loopback measurement if the user
chooses it. Tracktion's own recorder is not used: it locks and allocates on the audio thread.

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
| Recordings | `<project>/Recordings` (phase 1: `<projects>/Untitled/Recordings`) | same |
| Autosaves and crash recovery (phase 4) | `<settings>/Autosave` | same |
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
