# Testing and verification

Carthage is an audio application: most of what matters cannot be seen in a diff. A change is done when
it has been **run and observed**, not when it compiles. This file says what "verified" means here.

The commands to build and run the tests do not exist yet. Phase 1 creates them and records them in
`AGENTS.md`; this file describes what has to be covered.

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

The engine's first rule (F8) is checked by a tool, not by review:

- Build a configuration that detects allocation, locking and blocking system calls on the audio thread
  and fails loudly. Check what the current toolchain offers for this (for example Clang's real-time
  sanitizer, if the installed compiler has it) before writing a custom allocator hook.
- Run the engine tests and a monitored-recording session under it.
- Run Thread Sanitizer and Address Sanitizer builds of the test suite regularly; they catch the data
  races and lifetime bugs that show up as rare crashes in an audio app.
- Third-party plugins break these rules inside their own code. Scope the checks to Carthage's code and
  say so in the report.

## Measuring

Numbers go in the phase report. An impression ("feels smooth", "low latency") is not a result.

- **Round-trip latency.** Physical loopback: an output wired to an input, play a click, record it,
  count the samples between the two. Do it at two or more buffer sizes and compare with the figure the
  app displays. Report the interface, sample rate and buffer size.
- **Dropouts.** Count device overruns/underruns during a long monitored recording; report the count,
  duration, buffer size and audio-thread load.
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

CI repeats steps 1 and 2 on macOS and Windows. A red `main` is fixed before anything else is pushed.
