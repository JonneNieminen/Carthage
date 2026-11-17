# Carthage — agent notes

Carthage is a native DAW: C++ on JUCE + Tracktion Engine, macOS on Apple Silicon first, Windows x64
second. It hosts third-party AU and VST3 plugins and ships none of its own. Uses: tracking guitar live
through amp sims, arranging on a timeline, writing in a piano roll, mixing and mastering. The goal is
to be faster, smoother and more reliable than FL Studio on an ARM Mac: efficiency and minimalism
first, no gimmicks.

**State of the repo:** phase 1 verified (2026-11-17): the build system, the engine layer, the phase 1 app (audio
settings, one input monitored through one plugin, takes recorded to clips), the plugin scanner, tests
and the measuring tool exist. See `docs/ROADMAP.md` for what is verified.

## Read first

| File | Read it for |
| --- | --- |
| `docs/BRIEF.md` | What to build. The owner's brief, word for word. It wins over every other file. |
| `docs/DECISIONS.md` | What is fixed (F1–F15), what was decided while building, what is still open. |
| `docs/ROADMAP.md` | The seven phases, their exit checks, current status, phase reports. |
| `docs/ARCHITECTURE.md` | Layers, the model, threads, plugins, drawing. A proposal until phase 1 proves it. |
| `docs/TESTING.md` | What "verified" means, how to measure, real-time safety checks. |
| `docs/RELEASING.md` | Versions, the release workflow, Sparkle/WinSparkle, keys. |
| `design/README.md` + `design/tokens.css` | The theme. Read before drawing anything. |

## How to work here

- **Follow the build order.** One phase at a time; a phase must build, run and be verified before the
  next starts. Find the current phase in `docs/ROADMAP.md`.
- **Ask before conflicting with a fixed decision.** If a fixed decision (F-numbered) looks wrong,
  impossible or in tension with another, stop and ask the owner. Do not work around it. Open questions
  (O-numbered) say who resolves them.
- **Check current sources, not memory.** JUCE and Tracktion Engine change between versions. Before
  using an API, read the pinned submodule's headers, examples and changelog. Use their newest features
  over older patterns. Use clangd to navigate. The same goes for Sparkle, WinSparkle and GitHub Actions.
- **Use the frontend-design plugin for every UI decision**, then look at the running result.
- **Verify by running.** Compiling is not verification. Launch the app, exercise the change, measure
  what the roadmap says to measure, test with real third-party plugins where it says so.
- **Report after each phase** in the format at the end of `docs/ROADMAP.md`: what works, what was tested
  and how (plugins named), the numbers, what is not done. Say plainly when something failed or was not
  tested.
- **Keep the docs true** in the same commit as the code (table below).

## Commands

Tools: Xcode 27 command line tools (Apple Clang 21), CMake 3.30+ and Ninja. On the owner's Mac they
come from Homebrew (`brew install cmake ninja`, in `/opt/homebrew/bin`); there is no `timeout` command.
The real-time-safety build also needs Homebrew LLVM (`brew install llvm`), because Apple Clang has no
RealTimeSanitizer.

```bash
# Fresh clone. Not --recurse-submodules: Tracktion Engine's own nested JUCE is not used (DECISIONS W7).
git clone https://github.com/JonneNieminen/Carthage.git && cd Carthage
git submodule update --init

# Configure and build (presets: debug, release, rtsan). Warnings are errors in Carthage's code.
cmake --preset release
cmake --build --preset release

# Run the app (it has carthage-scan beside its executable)
open build/release/src/app/Carthage_artefacts/Release/Carthage.app

# Unit and engine tests (headless, no audio hardware)
ctest --preset release

# Real-time safety: build with RTSan, then exercise the audio thread (docs/TESTING.md)
cmake --preset rtsan && cmake --build --preset rtsan
build/rtsan/tools/bench/carthage_bench_artefacts/RelWithDebInfo/carthage_bench soak --seconds 60 \
    --device "Audient iD44" --rate 48000 --buffer 64 --input 1 \
    --plugin-format VST3 --plugin /Library/Audio/Plug-Ins/VST3/NeuralAmpModeler.vst3

# A release build with a version, as the release workflow makes it (docs/RELEASING.md)
cmake --preset release -DCARTHAGE_RELEASE=0.1.0 && cmake --build --preset release --target Carthage
packaging/macos/sign.sh build/release/src/app/Carthage_artefacts/Release/Carthage.app
packaging/macos/make-dmg.sh build/release/src/app/Carthage_artefacts/Release/Carthage.app 0.1.0 dist
# Releasing is a tag: git tag v0.1.0 && git push origin v0.1.0 (ask the owner first)

# UI snapshots of the real window content, no screen needed (tools/snapshot/SnapshotMain.cpp)
build/release/tools/snapshot/carthage_snapshot_artefacts/Release/carthage_snapshot snapshots

# Hardware measurements (loopback latency, dropouts, take alignment): see tools/bench/BenchMain.cpp
# (owner's loopback: iD44 line out 3 -> input 4, with outputs 3+4 set to "DAW thru" in Audient's iD app)
build/release/tools/bench/carthage_bench_artefacts/Release/carthage_bench latency --device "Audient iD44" \
    --rate 48000 --buffers 32,64,128,256 --out 3 --in 4
build/release/tools/bench/carthage_bench_artefacts/Release/carthage_bench align --device "Audient iD44" \
    --rate 48000 --buffer 64 --in 4 --click-out 3 --measured-out 3
build/release/tools/bench/carthage_bench_artefacts/Release/carthage_bench takes --device "Audient iD44" \
    --rate 48000 --buffer 512 --plugin-format AudioUnit --plugin "AudioUnit:Effects/aufx,Akrc,Bgrn"
```

- clangd reads `build/debug/compile_commands.json` (see `.clangd`); configure the `debug` preset once.
- Formatting: `.clang-format` at the root; there is no automatic formatter run yet.
- README art: `design/banners/render.sh` (needs Google Chrome; `CHROME=…` overrides the path).
- Preview one piece of art: open `design/banners/art.html?art=hero&theme=dark`
  (`art` = `hero|social|mark|palette|lanes|type`).

## Layout

- `src/engine/` — the engine layer over Tracktion (no UI; runs headless). `src/app/` — application,
  windows, command table (`Commands.h`), path provider (`Paths.cpp`), platform files
  (`platform/*_mac.mm`). `src/ui/` — components (the `carthage_ui` library);
  `src/ui/theme/Theme.h` mirrors `design/tokens.css`. `src/scan/` — `carthage-scan`.
- `tools/bench/` — `carthage-bench`, the hardware measuring tool. `tools/snapshot/` — `carthage-snapshot`,
  renders the window content to PNG. `tests/` — unit/engine tests and the
  RTSan baseline. `scripts/` — `rtsan_check.py`.
- `external/` — JUCE and Tracktion Engine submodules (pinned, DECISIONS W7). Never edit inside them.
- `design/tokens.css` — every colour, font, radius, spacing step and duration. Single source of truth.
- `design/README.md` — the theme guide.
- `design/icon/carthage-icon.svg` — app icon master (1024 canvas, 824 tile), plus a 1024 px PNG. This is the "01-cothon-dark" concept the brief names (DECISIONS W5).
- `design/logo/` — the mark on dark, on light, mono (`currentColor`), favicon.
- `design/banners/` — `art.html` + `render.sh` → `docs/art/*.png`. Never edit a PNG by hand.
- `docs/` — the documents in the table above. Full layout: `docs/ARCHITECTURE.md`.
- `packaging/` — release packaging: `macos/` (signing, DMG), `windows/` (Inno Setup), the release-notes
  and appcast scripts, and the update public key. `.github/workflows/` — `ci.yml`, `release.yml`
  (docs/RELEASING.md). `cmake/` — warnings, the tag-to-version scheme, the updater pins.

## Rules

### Engine
- **Nothing on an audio thread allocates, locks, waits, does I/O, logs or calls the UI.** This covers the device callback and every render thread. It is checked by a tool (`docs/TESTING.md`), not by review. Known violations inside Tracktion, JUCE and plugins are listed in `tests/rtsan/baseline.txt`; Carthage's code can never be listed there (DECISIONS F18).
- Audio → UI goes through atomics and lock-free FIFOs that the UI polls. UI → audio goes through the engine's real-time-safe mechanisms. No mutex the audio thread can wait on.
- Recording reaches disk through a lock-free FIFO and a writer thread. An overflow is counted and reported, never hidden and never fixed by blocking.
- Every audio thread joins the device's audio workgroup on macOS.
- The monitored input chain stays in-process. Latency is measured with a loopback and reported.
- Plugin delay compensation is always on, except where the low-latency monitoring mode documents what it relaxes.
- `engine/` includes nothing from `ui/` or `app/` and can run headless.

### Plugins
- **No stock plugins.** Never add a built-in effect or instrument. Gain, pan, sends and meters are channel features.
- The app process never loads a plugin that has not been scanned out of process. A plugin that crashes or hangs the scanner goes on the blocklist automatically.
- AU and VST3 only. CLAP only if the pinned JUCE/Tracktion hosts it natively (O3).

### Model and editing
- Project state lives in one model. Components read it and listen; they never keep their own copy.
- Every user edit is one named, undoable transaction. A drag is one transaction. Anything that cannot be undone is a bug.
- Routing is decoupled: a lane is only a place for clips; each clip, instrument or input routes to any mixer channel. Do not let "track = channel" leak into the UI.
- One grid and snap system for the timeline and the piano roll. One command table for menus, shortcuts, the shortcut editor, tooltips and the help page.
- Shortcuts: tool keys E P B C D T Z S with hold-to-switch; all rebindable; written with a platform-neutral primary modifier (Cmd on macOS, Ctrl on Windows) and never hard-coded in UI text.
- Project files carry a format version. A project saved by an older Carthage always opens.

### UI
- **Dark only in v1.** Flat graphite surfaces, one accent, round caps, dense but readable. No glass, gradients, glows, or shadows on controls.
- Colours, sizes and radii come from the app's theme header, which mirrors `design/tokens.css` value for value under the same names. No colour literals in components. A new colour is a new token: add it to `tokens.css` and `design/README.md` first.
- The accent (sky) means **live**: playhead, selection, focus, active tool. Tracks use the eight `--lane-*` colours. `--ok`, `--warn`, `--clip` are for meters, record arm, warnings and destructive actions only.
- Every control has a tooltip, including its shortcut, read from the command table.
- Draw at display refresh rate. Cull to the viewport, paint from cached thumbnails, never decode audio or touch disk while painting. Nothing animates during playback except the playhead and the meters.
- Heavy work never runs on the message thread: scanning, decoding, thumbnails, export, saving and update checks go to workers.
- Text: sentence case, plain verbs, no all-caps labels. Name things the way a musician would, not the way the engine does.
- The mark is fixed (64 grid: ring radius 17, stroke 5, 84° opening east, dot radius 6). Do not redraw, rotate or recolour it. The wordmark is lowercase `carthage`; Futura is for the wordmark artwork only and is not shipped in the app.

### Code
- C++26 for Carthage's code; JUCE and Tracktion compile as C++23 (DECISIONS W11). Warnings are errors. Format with the repo's `.clang-format`.
- Dependencies are git submodules pinned to commits; the exception is Sparkle and WinSparkle, pinned as release archives by SHA-256 in `cmake/Updaters.cmake` (DECISIONS W25). Changing a pin is its own commit with the reason and a DECISIONS entry. Do not patch code inside a submodule.
- Do not add a dependency the brief does not name without asking.
- Paths to user folders come from one place in `app/`; the help page lists them from there.
- Windows is a real target: no macOS-only API outside a platform file, and CI builds both.

### Safety
- Never commit secrets. The Sparkle private key exists only as the `SPARKLE_ED_PRIVATE_KEY` GitHub secret; the agent never generates, reads or prints it. `APPLE_*` likewise.
- Do not install, move or delete anything in the system AU/VST3 folders without asking; remove anything placed there for a test.
- Pushing a `v*` tag publishes a release to every installed copy. Ask before creating one unless told to cut that release.
- Never weaken, skip or delete a test to get a green run.
- The README and art must not claim a feature the app does not have.

## Git

- Commit and push straight to `main`; no pull requests. A ruleset protects `main` and the owner's admin account bypasses it, so the "Bypassed rule violations" notice on push is expected.
- Small commits that build. Message: imperative summary under ~70 characters, then why if it is not obvious.
- `main` stays green. A broken CI run is fixed before anything else is pushed.
- User-visible changes get a line under "Unreleased" in `CHANGELOG.md`.

## Keep these in sync

| When you change… | Also update |
| --- | --- |
| A colour, font, radius or spacing value | `design/tokens.css` first, then the theme header, then `design/README.md`; re-render art if it shows |
| A shortcut or tool | The command table (the help page and tooltips follow), and the README shortcut table once it exists |
| Build, run or test commands | "Commands" above and `CONTRIBUTING.md` |
| Source layout, threading or a module boundary | `docs/ARCHITECTURE.md` |
| A decision, a dependency pin, an answered open question | `docs/DECISIONS.md` |
| Progress on a phase | `docs/ROADMAP.md` checkboxes, status and report |
| The release pipeline, feed or signing | `docs/RELEASING.md` |
| A folder Carthage reads or writes | The path provider in `app/`, the help page, `docs/ARCHITECTURE.md` |
| Anything a user would notice | `CHANGELOG.md`, and `README.md` if it is described there |
