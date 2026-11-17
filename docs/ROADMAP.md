# Roadmap

The seven phases from [BRIEF.md](BRIEF.md), each with what it must deliver and how it is proven. This
file is the status board: tick items as they are verified, and write the phase report in the section
for that phase.

**The rule** (F13): a phase must build, run and be verified before the next one starts. Verified means
the exit checks below were actually performed on the real app, on this machine, with real plugins where
the check says so. See [TESTING.md](TESTING.md) for how.

| Phase | Status |
| --- | --- |
| 0. Brand, theme and docs | Done (2026-11-16) |
| 1. Skeleton, audio settings, monitored guitar, record a clip | In progress: exit checks met; audio workgroup and settings panel deliverables open |
| 2. CI, releases and in-app updates | Not started |
| 3. Plugin manager and scanning | Not started |
| 4. Timeline, grid, snapping, tools, shortcuts | Not started |
| 5. Mixer, insert Lines, per-channel I/O | Not started |
| 6. Piano roll | Not started |
| 7. Help page, icon and polish | Not started |

Statuses: Not started · In progress · Verified (date). Do not mark a phase Verified with an unticked
exit check; if a check cannot be met, say so in the report and ask the owner.

---

## Phase 1 — Skeleton, audio settings, monitored guitar, record a clip

Brief features: 1 (engine), 8 (audio settings, in part).

Deliver:
- [x] CMake + Ninja project with JUCE and Tracktion Engine as pinned submodules; presets for debug and release; Apple Silicon native build
- [x] Open questions O1, O2, O7, O8 in [DECISIONS.md](DECISIONS.md) answered and recorded
- [x] App window in the Carthage theme (dark), with the theme header mirroring `design/tokens.css`
- [ ] Audio settings: device, buffer size, sample rate (44.1–192 kHz), recording bit depth; round-trip latency shown in ms; CPU / audio-thread load meter — built; the settings panel has not yet been seen on screen (see report)
- [x] One input monitored through one AU or VST3 plugin, in-process
- [x] Record that input to a clip on disk through a lock-free FIFO
- [ ] Audio threads in the device's audio workgroup on macOS — configured (W14); not shown on the interface: an Instruments trace hints they may not be joined (see report)
- [x] Commands in `AGENTS.md` filled in with the real configure, build, run and test lines
- [x] [ARCHITECTURE.md](ARCHITECTURE.md) updated to match what was built

Exit checks:
- [x] Clean configure and build from a fresh clone (with submodules) using only the documented commands
- [x] A real guitar (or line) input is heard through a real third-party amp sim, and a take is recorded and plays back in time
- [x] Round-trip latency **measured** with a loopback at two or more buffer sizes and reported next to the figure the app shows
- [x] No allocation, lock or I/O on the audio thread, shown by a real-time-safety check, not by reading the code (held as F18: none in Carthage's code; third-party sites baselined)
- [x] No dropouts over a ten-minute monitored recording at the smallest stable buffer size; the buffer size and load are reported — iD44 at 48 kHz / 32 with AmpKnob (16 was not stable)

Report:

### Report — phase 1 (2026-11-17, interim: exit checks met, two deliverables open)
Works:        Carthage builds and runs natively on Apple Silicon. Its window, in the Carthage theme,
              shows the signal path on one line (input with a meter and a "Hear the input" switch,
              the plugin, the output, joined by a cable that lights when the input is audible), a
              lane of takes with waveforms and a playhead, and the audio-thread load and the driver's
              round trip in the top bar. Audio settings (interface, sample rate 44.1–192 kHz, buffer,
              bit depth 16/24/32-float, the driver's round trip, a loopback "Measure" button, engine
              and audio-thread load, dropouts) slide in from the right. A plugin is chosen from every
              AU and VST3 on the Mac (227 found, none loaded to list them), checked in carthage-scan
              first, then inserted in the monitored path in-process; a plugin that crashes or hangs
              the scanner is blocked. Recording writes a WAV through a lock-free FIFO and a writer
              thread; the take lands as a clip at the time it was played, moved earlier by the round
              trip. Every action is in one command table (menus, shortcuts, tooltips). Startup asks
              for microphone access without blocking; refusing it leaves the app usable with a note.
Tested:       Fresh clone with only the documented commands: clone, `git submodule update --init`,
              configure, build, test at 0cd6f98 (5 min 30 s) and again at 7fe7486 — 0 warnings, tests pass, app has
              carthage-scan beside it. Unit tests (66 expectations): take round trip sample-exact at
              32-bit float from a separate thread; FIFO overflow drops and counts whole blocks; probe
              finds a simulated 1234- and 4111-sample loopback to the sample (inverted, -20 dB, noisy)
              and reports "nothing came back" with no cable; scan protocol and versioned cache files;
              Theme.h equals tokens.css (a deliberate one-digit change makes it fail).
              Plugins hosted and run in the monitored path: Neural Amp Modeler 0.7.9 (Steven
              Atkinson) as VST3 and as AU, scanned out of process (0.7 s) and loaded. Real-time check:
              RTSan build (Homebrew LLVM 23), whole device callback in one real-time scope, graph on
              the device thread, 60 s VST3 + 30 s AU + 20 s no-plugin monitored recordings:
              38 distinct reports, 0 in Carthage's code, all 38 in Tracktion Engine, JUCE's plugin
              wrappers or inside the plugin, now the baseline (tests/rtsan/baseline.txt). Negative
              control: a malloc and a mutex in a real-time scope are reported; a planted carthage::
              report fails the checker. App window seen on screen with the iD44 attached (first
              screenshot, before the iD44 was unplugged).
Measured:     Built-in audio (MacBook mic in, silent virtual output, so nothing could feed back),
              48 kHz, Neural Amp Modeler VST3 monitored and recording, Release build:
              buffer 16 for 30 s: 1 late block; 32 for 30 s: 0; 64 for 30 s: 0.
              buffer 32 for 600 s: 0 driver dropouts, 0 late blocks, 0 recorder overflows, take
              600.98 s complete, audio-thread load 1.7 % average, 34.1 % worst block.
              (A first 600 s run at 32 while a Debug build of the app was also running had 1 late
              block the driver did not report; the clean re-run above had none.)
              Driver round trip shown by the app: iD44 at 44.1 kHz / 512: 648 + 538 = 1186 samples,
              26.89 ms; built-in path at 48 kHz: 140 (16), 172 (32), 236 (64) samples.
              Loopback, Audient iD44, line out 3 -> input 4 (outputs 3+4 on "DAW thru" in Audient's
              iD app; on its default "main mix" nothing came back), `carthage-bench latency`, five
              runs each, spread 0 samples in every case:
                48 kHz   buffer  32:  measured 191 samples (3.98 ms), driver 240 (5.00 ms)
                48 kHz   buffer  64:  measured 256 samples (5.33 ms), driver 304 (6.33 ms)
                48 kHz   buffer 128:  measured 383 samples (7.98 ms), driver 432 (9.00 ms)
                48 kHz   buffer 256:  measured 639 samples (13.31 ms), driver 688 (14.33 ms)
                44.1 kHz buffers 32/64/128/256: measured 183/247/376/632, driver 226/290/418/674
                96 kHz   buffers 32/64/128/256: measured 301/365/493/750, driver 408/472/600/856
              The driver overstates the round trip by a constant ~1 ms (43-49 samples at 44.1/48 kHz,
              107 at 96 kHz). The real figure moves by a few samples each time the device opens
              (255-261 at 48 kHz / 64 across runs). The app shows the driver's figure.
              Take alignment, `carthage-bench align` (metronome on out 3 through the cable), after
              fixing two placement bugs it found (W22, W23): with the measured round trip every click
              lands 7.9-8.0 samples early (0.16-0.18 ms) at 44.1/48/96 kHz and buffers 32-256, the
              same for all 16 clicks of a take; with the driver's figure 50-57 samples early (1.0-1.2
              ms). The 8-sample remainder is constant in time and comes from how the bench finds a
              click (threshold onset) against how the probe measures (cross-correlation). A take
              started right after a device change, which before the fix landed up to 150 ms off,
              now lands the same. Buffers 512 and 1024 at 48 kHz: also 7.9 samples early.
              Guitar: the owner played a guitar into the iD44's input 1 (instrument input) through
              AmpKnob - RevC 1.3.140 by Bogren Digital (AudioUnit, reported latency 0.06 ms), checked
              out of process, heard it in headphones, and recorded takes in the app at 48 kHz / 512:
              one of 2 min 44 s (24-bit mono, peak -2.3 dBFS, no clipped samples, no gaps). Five
              other takes ended themselves after 11 ms; the trace showed the one-block settle of the
              time line (512 samples, 10.67 ms) exceeding a fixed 10 ms jump tolerance. Fixed (W22);
              `carthage-bench takes` at buffer 512 with AmpKnob: 6 of 6 takes ran their full length
              (the fixed app has not yet been played in by the owner). Before the app run,
              `carthage-bench soak` with AmpKnob on input 1, buffer 64, 20 s: 0 dropouts, 0 late
              blocks, audio-thread load 7.2 % average, 19.7 % worst block.
              Ten-minute soak on the iD44, 48 kHz, AmpKnob monitored on input 1 and recorded,
              Release build, Carthage app closed:
                buffer 16 (the iD44's smallest), 30 s: 0 dropouts, 0 late blocks. 600 s: driver
                  dropouts 0, recorder overflows 0, load 5.5 % average, but 3 late blocks between
                  480 and 540 s from one stall (worst block 447 % of its 0.33 ms deadline). Not
                  stable; the cause was not found.
                buffer 32, 600 s: driver dropouts 0, late blocks 0, recorder overflows 0, take
                  600.47 s complete, audio-thread load 3.2 % average, 19.4 % worst block. This is
                  the smallest stable buffer: round trip 191 samples (3.98 ms) measured.
              Audio workgroup: the iD44 provides one and Tracktion's render threads ask to join it
              (W14), but Tracktion does not check the result. `ps -M` shows the device thread at
              real-time priority 97 and seven render threads at real-time 47, which does not show
              membership. An Instruments Audio System Trace attached to a running soak reported a
              concurrent thread count of 1 on the device's I/O thread, in one reading covering the
              last 0.5 s; with seven joined threads a higher count would be expected. Inconclusive.
              (Tracing caused 5 late blocks in that run, so it is not a soak result.)
Not done:     Showing that the render threads are in the device's audio workgroup (above). The
              settings panel has been seen only through carthage-snapshot on the iD44, which showed
              three faults to fix in the UI pass: the loopback pickers default to the last channels
              ("24 ADAT-16" / "20 ADAT-16"), and the round-trip figure and load values are blank
              right after opening. Windows: nothing built or tested (CI arrives in phase 2). Thread
              and Address Sanitizer presets: not yet (phase 2, with CI).
Decisions:    F16 (AGPLv3), F17 (JUCE 9.0.5 + Tracktion develop), F18 (how F8 is checked);
              W7–W21: pins, minimum OS, ASIO, C++26/23 split, real-time check mechanics, Carthage's own
              recorder and device callback, plugin load gate, two dropout counts, folders, developer
              tools, carthage-bench, microphone access, carthage-snapshot; W22 (take placement),
              W23 (first MIDI scan at startup).
Needs owner:  1. Decide how to close the two open deliverables: the audio workgroup (investigate
                 and fix if Tracktion's threads are not joined, or accept as unproven) and the
                 settings panel faults (fix now, or in the UI pass). Outputs 3+4 can go back to
                 "main mix" in the iD app.
              2. Approve macOS's microphone prompt for Carthage when it appears.
              3. Approve or edit the upstream drafts in docs/upstream/ before anything is posted.

## Phase 2 — CI, releases and in-app updates

Brief features: 12, and the whole "Updates and releases" section. Details in [RELEASING.md](RELEASING.md).

Deliver:
- [ ] `ci.yml`: build and test on every push to `main` (macOS arm64, Windows x64)
- [ ] `release.yml`: tag-triggered; `.dmg` for macOS arm64, Inno Setup `.exe` for Windows x64; version injected from the tag into CMake, `Info.plist` and Windows resources
- [ ] Sparkle 2 and WinSparkle behind one interface; background check on launch; non-blocking banner with release notes, "Install on quit" (default) and "Restart now"; "Check for updates…" menu item; "Receive beta updates" setting
- [ ] Stable and beta appcasts, EdDSA-signed; the workflow still builds and skips the feed when `SPARKLE_ED_PRIVATE_KEY` is missing
- [ ] Ad-hoc signing now; Developer ID + `notarytool` step present and switched off until `APPLE_*` secrets exist
- [ ] Release notes include the one-time Gatekeeper step
- [ ] The owner has the exact, tested steps to generate the key and add the secret (in RELEASING.md)

Exit checks:
- [ ] v0.1.0 installed from the published `.dmg` on a real Mac
- [ ] v0.1.1 published; the installed app finds it, shows the banner and updates itself on quit
- [ ] The update installs without repeating the Gatekeeper step (O6); if it does not, reported to the owner
- [ ] A `-beta.N` tag appears only on the beta feed, and only an app with the beta setting on sees it
- [ ] An update is not offered a restart during playback or recording, and "Restart now" with unsaved work saves or asks first
- [ ] Windows installer builds in CI and the WinSparkle path is exercised as far as the available hardware allows; what could not be tested is stated

Report: _not written yet_

## Phase 3 — Plugin manager and scanning

Brief feature: 6.

Deliver:
- [ ] Out-of-process scanner; a crashing or hanging plugin cannot take the app down; automatic blocklist; rescan; progress shown
- [ ] Plugin list with fast fuzzy search, favourites, filters by vendor, format and type, last-used
- [ ] Drag a plugin onto a channel or the timeline
- [ ] O3 (CLAP) answered and recorded

Exit checks:
- [ ] Full scan of the owner's real AU and VST3 folders completes; counts and time reported
- [ ] A plugin that crashes on load (a deliberately broken one will do) ends up on the blocklist and the app keeps running
- [ ] The UI stays responsive during a scan (frame time reported)
- [ ] Search returns the expected plugin within a keystroke's time on the full list

Report: _not written yet_

## Phase 4 — Timeline, grid, snapping, tools, shortcuts

Brief feature: 2, and fixed decisions F5–F7.

Deliver:
- [ ] Lanes that hold any clips; zoom and scroll at display refresh rate
- [ ] Grid: default 1/4 beat with toggle (shortcut and toolbar button); bar, beat, 1/2–1/64, triplets, dotted, adaptive
- [ ] Snap to grid, clip edges, markers and playhead; hold Cmd/Ctrl to bypass for one drag
- [ ] Tools E P B C D T Z S with hold-to-switch; edge drag trims, Option/Alt stretches, Cmd/Ctrl slips
- [ ] Split at playhead or cursor, duplicate, nudge, crossfades, loop range, markers, cached waveform thumbnails
- [ ] Shortcut editor; every shortcut rebindable
- [ ] **Proposed here, owner to confirm (O5):** undo/redo for everything so far, project save and load, autosave and crash recovery

Exit checks:
- [ ] Frame time while zooming and scrolling a project with a few hundred clips, measured on a 120 Hz display and reported
- [ ] Each tool key, its hold-to-switch behaviour, and each edge-drag modifier exercised in the running app
- [ ] Each snap target and the bypass key exercised
- [ ] Every edit undoes and redoes; a saved project reopens identical; killing the app mid-session offers recovery on next launch

Report: _not written yet_

## Phase 5 — Mixer, insert Lines, per-channel I/O

Brief features: 3, 4, 5.

Deliver:
- [ ] Master pinned left, 50 channels by default; add, remove, rename, colour, reorder
- [ ] Per channel: fader, pan, mute, solo, record arm, input monitoring, peak/RMS meter
- [ ] Sends and buses; any channel routes to any other or to master
- [ ] Insert Lines: unlimited growing list; reorder by drag, bypass, wet/dry, open UI, drag between channels
- [ ] Input/output picker per channel; "Guitar in → this channel, armed, monitoring" action; keyboard-driven selection
- [ ] Plugin delay compensation across the whole graph; low-latency monitoring mode
- [ ] **Proposed here, owner to confirm (O5):** metronome, count-in, loop recording with takes, punch in/out

Exit checks:
- [ ] A clip, an instrument and a live input each routed to an arbitrary channel, then re-routed, while playing
- [ ] Delay compensation verified by a null test with a plugin that reports latency
- [ ] A feedback route is refused or handled safely
- [ ] A realistic session with real third-party plugins on many channels plays without dropouts; load reported
- [ ] An idle mixer with 50 empty channels costs close to nothing; load reported

Report: _not written yet_

## Phase 6 — Piano roll

Brief feature: 7.

Deliver:
- [ ] FL-style piano roll on the shared grid and snap system
- [ ] Draw, paint, slice and delete tools; velocity lane; quantize; note length presets; scale highlighting; ghost notes from other patterns
- [ ] Drives third-party instruments

Exit checks:
- [ ] A melody drawn, edited, quantized and played through a real third-party instrument
- [ ] Grid and snap behave identically to the timeline (same code path, shown by a shared test)
- [ ] Frame time with a dense pattern reported

Report: _not written yet_

## Phase 7 — Help page, icon and polish

Brief feature: 9, and "Look".

Deliver:
- [ ] Interactive "How to use Carthage" page: tools, shortcuts (read from the command table), and every folder Carthage uses with an open-in-Finder/Explorer button
- [ ] macOS `.icns`, Icon Composer bundle and Windows `.ico` built from `design/icon/carthage-icon.svg`
- [ ] Export: WAV 16/24/32-float and FLAC, with the quality settings from feature 8
- [ ] README updated with real screenshots, install steps and the shortcut table; feature cards added to the README art
- [ ] A pass over every feature in the brief, 1–12, with anything missing listed

Exit checks:
- [ ] Every folder button opens the right folder
- [ ] The help page's shortcut list follows a rebind
- [ ] An exported file nulls against the same range played back in the app
- [ ] A full song made end to end: guitar tracked through an amp sim, drums and samples on the timeline, a melody in the piano roll, mixed and exported

Report: _not written yet_

---

## Phase report format

Write it under the phase's **Report** line, then post the same thing to the owner.

```
### Report — phase N (date, commit)
Works:        what a user can now do
Tested:       each exit check, how it was checked, and the result; real plugins named
Measured:     latency, frame time, CPU load, scan time: the numbers
Not done:     anything in the phase that is missing or only partly working
Decisions:    new entries added to DECISIONS.md
Needs owner:  questions that block the next phase
```
