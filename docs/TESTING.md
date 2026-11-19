# Testing and verification

Carthage is an audio application: most of what matters cannot be seen in a diff. A change is done when
it has been **run and observed**, not when it compiles. This file says what "verified" means here.

The commands are in `AGENTS.md` ("Commands"). This file says what has to be covered and how each
measurement is made.

## Layers

| Layer | What it covers | Runs |
| --- | --- | --- |
| **Unit** | Pure logic with no audio device: grid and snap maths, time conversion, the command table, fuzzy search, version parsing, project-file round trips | Every push (CI), headless |
| **Engine** | The engine driven offline: render a known project to a buffer and compare; routing, delay compensation, record-then-play alignment, export formats | Every push (CI), headless, no audio hardware |
| **Real-time safety** | Nothing on an audio thread allocates, locks or does I/O | Every push where the toolchain allows; always before a phase is marked Verified |
| **App** | The real app on the real machine: devices, real plugins, drawing speed, updates | By the agent before reporting; cannot be replaced by the layers above |

## Rules

- **Test the behaviour the brief asks for.** Each feature's exit checks in [ROADMAP.md](ROADMAP.md) are
  the minimum. Add a test when a bug is fixed, so it stays fixed.
- **Engine tests are deterministic.** Render offline with a fixed sample rate and block size, generated
  input (sine, impulse, click), and compare against an expected result with a stated tolerance. No test
  depends on a sound card, the wall clock or a third-party plugin being installed.
- **Never weaken a test to make it pass.** No disabling, skipping, loosening tolerances or deleting an
  assertion to get a green run. If a test is wrong, say why in the commit that changes it.
- **CI has no audio hardware and no third-party plugins.** Anything that needs them is an app-level
  check done on the owner's machine and written up in the phase report.

## Real-time safety

The engine's first rule (F8) is checked by a tool, not by review (how it is held: DECISIONS F18, W12).

- **The tool.** Clang's RealTimeSanitizer, from Homebrew LLVM (Apple Clang does not ship it). The
  `rtsan` preset builds everything with `-fsanitize=realtime`. `AudioCallback` marks the whole device
  callback as a real-time scope, so any allocation, free, lock, wait or blocking system call made
  during a block is reported with its stack, wherever it happens. In this build Tracktion renders on
  the device thread only, so every node is inside the scope.
- **The run.** Exercise the audio thread for at least a minute with a real plugin in the monitored
  path, while recording, with RTSan reporting every distinct violation instead of stopping at the first:

  ```bash
  RTSAN_OPTIONS=halt_on_error=false build/rtsan/tools/bench/carthage_bench_artefacts/RelWithDebInfo/carthage_bench \
      soak --device "<interface>" --rate 48000 --buffer 128 --input 1 \
      --plugin-format VST3 --plugin /Library/Audio/Plug-Ins/VST3/NeuralAmpModeler.vst3 --seconds 60 > rtsan.log 2>&1
  python3 -I scripts/rtsan_check.py rtsan.log tests/rtsan/baseline.txt
  ```

  Repeat with an Audio Unit and with no plugin. The check fails on any report owned by Carthage's
  code, and on any third-party report not in `tests/rtsan/baseline.txt`. It lists baseline entries
  that no longer occur, so the file shrinks when upstream fixes them.
- **Negative control.** A malloc and a mutex inside a `ScopedRealtime` scope must be reported; checked
  when the tool was set up (phase 1 report) and worth repeating after a toolchain change.
- **Timing is never judged in the RTSan build**: it renders on one thread and reports cost time.
  Dropouts and load come from the normal build.
- **In CI.** The tests' own audio-thread calls (`LatencyProbe::process`, `TakeRecorder::pushBlock`,
  `SamplePreview::render`) run inside a `ScopedRealtime`, so CI's `rtsan` job fails on any
  allocation, lock or blocking call there, without an audio device. The device soak above stays an
  owner-machine check.
- Third-party plugins break these rules inside their own code; those reports are baseline entries at
  the plugin boundary, and the report says which plugins were used.

## Static checks and sanitizers

How Carthage's C++ is held to its rules (DECISIONS W39, AGENTS.md "C++ practices"). CI runs all
three on macOS on every push, and fails on any finding.

- **clang-tidy**, from Homebrew LLVM, with the checks in `.clang-tidy`: bugs, the static analyser,
  concurrency, performance and the modern-C++ checks that match the rules. Build first, then:

  ```bash
  /opt/homebrew/opt/llvm/bin/run-clang-tidy -clang-tidy-binary /opt/homebrew/opt/llvm/bin/clang-tidy \
      -p build/release -quiet "$PWD/(src|tools|tests)/.*\.cpp$"
  ```

  A finding is fixed, not silenced. A `NOLINT` is allowed only for a proven false positive, names the
  check and says why on the same line (`.mm` files are not checked: the tool misreads Objective-C).
- **AddressSanitizer + UBSan.** `cmake --preset asan && cmake --build --preset asan && ctest --preset asan`
  runs the tests with out-of-bounds, use-after-free and undefined-behaviour checks; any report stops
  the run. As in the RTSan build, timing is not judged there: the release build's tests check it.
- **RealTimeSanitizer**: the `rtsan` preset's tests, above.

## Measuring

Numbers go in the phase report. An impression ("feels smooth", "low latency") is not a result.

- **Round-trip latency.** Physical loopback: an output wired to an input. `carthage-bench latency`
  (or "Measure" in the app's audio settings) plays a 40 ms chirp on the output and finds it in the
  input by cross-correlation; output and input share one sample clock, so the lag is the round trip.
  Do it at two or more buffer sizes and compare with the figure the app displays (the driver's input +
  output latency). Report the interface, sample rate and buffer size.
- **Take alignment.** `carthage-bench align` records Tracktion's metronome through the loopback and
  reports how far each recorded click lands from its beat, after Carthage's latency correction. The
  click plays on outputs 1-2, or on the pair holding `--click-out N`; `--measured-out N` measures the
  round trip first and places the take with it instead of the driver's figure; `--settle-ms N` waits
  before recording. Run it with and without `--measured-out`, and once with no settling (a take started
  right after a device change must still land in time, or end cleanly and say so).
- **Audio workgroup.** Every `carthage-bench` command that opens the device prints the device's
  workgroup and how many threads have joined one, counted at `os_workgroup_join` (DECISIONS W24).
  Report "joined / failed / in a workgroup now" from a monitored soak; `wgprobe` checks that the
  workgroup is valid and joinable before and after a device restart.
- **Plugin scan.** `carthage-bench scanall` checks every AU and VST3 plugin out of process, as
  the app's first scan does, in a fresh cache, and reports how many were checked, loaded and
  blocked (with why) and how long it took. Report the count, the time and the scanner count.
- **Projects.** `ProjectTests` (headless) saves and reopens a project, checks clip paths after
  Save as, a file from a newer Carthage, a damaged file, the autosave timing and the last-10 rule.
  `carthage-bench projects` does it through the app's `Session` on the real engine: Save as, reopen,
  an edit autosaved after a pause, the marker a crash leaves, recovery, and a clean close; then
  tabs (ten switches timed, nothing unsaved after switching, only the front project has a graph),
  copy in one tab and paste in another, and collecting the pasted clip's sample.
- **Samples.** `SampleTests` (headless): the index of a folder tree, search, favourites, the
  analysis cache, the preview's level, flush and end, and key and tempo on sounds of known key and
  tempo (A minor, C major, E minor at 90 bpm, clicks at 120 and 174, a noise burst, a tuned hit).
  `carthage-bench samples --folder F` indexes a real library, times the slowest of eight searches,
  analyses 60 files and starts the preview. `carthage-bench soak … --preview FILE` keeps the preview
  playing during the real-time check.
- **Crashing and hanging plugins.** The tests build two broken VST3 bundles (macOS:
  `build/<preset>/test-plugins/Carthage Test Crash.vst3` and `… Hang.vst3`): the unit test checks the
  scanner reports one as crashed and the other as hung; `carthage-bench scanall --folder
  build/<preset>/test-plugins` runs the real background scan with them among the real plugins.
- **Frame time.** `carthage-bench demo --out build/demo/Demo --clips 300` writes a project with 300
  clips over the eight lanes; pass its `.carthage` file to the app to open it at launch. Run the app with `CARTHAGE_FRAME_LOG=1` (on macOS: `open --env CARTHAGE_FRAME_LOG=1
  --stderr frames.log Carthage.app --args --rescan-plugins` to rescan everything at launch); once a
  second it writes the frame intervals (median, p99, worst, late frames) and the scan's state.
- **Back-to-back takes.** `carthage-bench takes` records takes the way a player does (record, stop,
  record again, every other one from the start, optionally `--while-playing` and with a plugin) and
  reports any that ended early. Run it at the smallest and the largest buffer you support.
- **Dropouts.** `carthage-bench soak` monitors through a plugin and records for the given time, and
  counts dropouts two ways: the driver's overload count, and blocks whose host timestamp arrives more
  than 1.5 blocks late (DECISIONS W16). It also compares the audio the device asked for with
  wall-clock time. Report the counts, duration, buffer size and audio-thread load.
- **Frame time.** Measure the time per frame while zooming and scrolling a heavy project on the 120 Hz
  display; report the worst frames, not the average.
- **CPU.** Report audio-thread load for a stated session, and idle load with an empty project.
- **Compared with what.** The brief's goal is "faster, smoother and more reliable than FL Studio on an
  ARM Mac". Where a comparison is made, state both setups exactly; do not claim it without one.

## Real plugins

Hosting is the product, so each phase that touches hosting is tested with real third-party plugins from
the owner's machine: at least one AU and one VST3, including an amp sim and an instrument. Name the
plugins and formats in the report. If a plugin misbehaves, record it in the report and, where it is
Carthage's fault, in a test.

Loading the owner's installed plugins is fine. Do not install, move or delete anything in the system
plugin folders without asking; if a test needs a plugin placed there temporarily, remove it afterwards.

## Checking the UI

Use the frontend-design plugin for every UI decision (F15), and look at the result: launch the app,
screenshot the view, and compare it with `design/README.md` (tokens, spacing, radii, the accent only
where something is live). Check at the smallest supported window size and with a long session loaded,
not only the empty state.

## Before every push

1. Configure and build with the documented commands, with warnings treated as errors.
2. Run the unit and engine tests.
3. Run the app and exercise what changed.
4. Update the docs the change touches (see the table in `AGENTS.md`).

CI repeats steps 1 and 2 on macOS and Windows, then clang-tidy and the ASan/UBSan and RTSan tests on macOS. A red `main` is fixed before anything else is pushed.
