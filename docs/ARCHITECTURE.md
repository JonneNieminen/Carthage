# Architecture

How Carthage is put together. **Nothing here is built yet.** This is the starting proposal
([DECISIONS.md](DECISIONS.md) W6): phase 1 tests it against real code and rewrites whatever turns out
wrong. Keep this file true: when the code and this file disagree, fix one of them in the same commit.

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
```

Dependencies point downward only. `engine/` never includes anything from `ui/` or `app/`, so it can be
driven by tests and by a headless tool without a window.

## Proposed source layout

```
CMakeLists.txt  CMakePresets.json
external/            JUCE, tracktion_engine (git submodules, pinned commits)
src/
  engine/            Carthage's layer over Tracktion: session, routing, devices, recording, export
  app/               application object, windows, command table, tool state, project files, updater
  ui/
    theme/           the theme header mirroring design/tokens.css, shared drawing helpers
    timeline/  mixer/  pianoroll/  plugins/  settings/  help/
  scan/              carthage-scan: the out-of-process plugin scanner
tests/               unit and engine tests (headless)
packaging/
  macos/             Info.plist template, entitlements, dmg script, icon build
  windows/           resource script, Inno Setup script
scripts/             developer scripts (icons, appcast, screenshots)
.github/workflows/   ci.yml, release.yml
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

Rules that follow:

- **Audio thread to UI:** atomics for single values, lock-free single-producer/single-consumer FIFOs for
  streams (meter values, recorded audio, MIDI activity). The UI polls on its frame timer. No async
  message posted from the audio thread.
- **UI to audio thread:** through the engine's own real-time-safe mechanisms (graph rebuilds swapped in
  atomically, parameter smoothing). Never a mutex the audio thread could wait on.
- **Recording** goes audio thread → lock-free FIFO → disk-writer thread → file (brief, feature 10). If
  the FIFO overflows, count and report the dropout; never block the audio thread to avoid it.
- **macOS audio workgroups:** every thread that renders audio joins the device's workgroup (F8). Check
  what the pinned JUCE and Tracktion already do before writing any of this by hand.
- **The monitored input chain stays in-process** (F8): input → plugin chain → output with no hop to
  another process and no extra buffering. Latency is measured, not assumed (see [TESTING.md](TESTING.md)).
- **Plugin delay compensation** is on for playback and export. The low-latency monitoring mode
  (feature 8) is the one place it is deliberately relaxed for the armed channel; document exactly what
  that mode bypasses.

## Plugins

- **Hosting** is AU and VST3 through JUCE/Tracktion. No built-in effects or instruments, ever (F4):
  gain, pan, sends and meters are channel features, not plugins.
- **Scanning** happens in `carthage-scan`, a separate executable (F9). The app hands it one plugin
  file at a time and reads the result. If the scanner dies or times out, the app records the file in
  the blocklist and moves on. The app process never loads an unscanned plugin binary. Check what the
  pinned JUCE/Tracktion provide for out-of-process scanning and use it before building a new one.
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

The help page must list every folder Carthage uses with a button to open each (feature 9), so there is
exactly one place in `app/` that knows these paths, and everything else asks it:

| What | Notes |
| --- | --- |
| Projects | User-chosen; default under the user's Music or Documents folder |
| Recordings | Inside the project folder, so a project is self-contained |
| Autosaves and crash recovery | Per project, plus a marker for "did not shut down cleanly" |
| Settings | Audio device, shortcuts, window layout, update preferences |
| Plugin scan cache | Known plugins, blocklist, favourites, last used |
| AU and VST3 system folders | Read-only to Carthage; listed for the user's benefit |

Exact paths are decided in phase 1 and recorded here. Project files carry a format version from the
first commit that writes one; a project saved by an older Carthage must always open.

## Updates

Sparkle 2 on macOS and WinSparkle on Windows sit behind one small interface in `app/` (check, state,
install-on-quit, restart-now), so the banner and the settings toggle are written once. The updater
asks the app whether it may restart; the app says no while playing, recording or holding unsaved work
(F10). Pipeline details are in [RELEASING.md](RELEASING.md).

## Third-party code

| Dependency | Role | How it is brought in |
| --- | --- | --- |
| JUCE | UI, audio devices, plugin hosting | Submodule, pinned commit |
| Tracktion Engine | Edit model, playback graph, recording, time-stretch | Submodule, pinned commit |
| Sparkle 2 | macOS updates | Decide in phase 2: pinned release binary or package manager; record it here |
| WinSparkle | Windows updates | Same |

Before using an API from any of these, read the pinned version's own headers, examples and changelog.
Both JUCE and Tracktion change between major versions; older tutorials and remembered APIs are often
out of date (brief, "Fixed decisions").
