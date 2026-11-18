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
| 1. Skeleton, audio settings, monitored guitar, record a clip | Verified (2026-11-17) |
| 2. CI, releases and in-app updates | Verified (2026-11-18) |
| 3. Plugin manager and scanning | Verified (2026-11-18) |
| 4. Timeline, grid, snapping, tools, shortcuts | 4a in progress (all of it built; exit checks next); 4b after it |
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
- [x] Audio settings: device, buffer size, sample rate (44.1–192 kHz), recording bit depth; round-trip latency shown in ms; CPU / audio-thread load meter
- [x] One input monitored through one AU or VST3 plugin, in-process
- [x] Record that input to a clip on disk through a lock-free FIFO
- [x] Audio threads in the device's audio workgroup on macOS — shown on the iD44: 7 render threads joined, 0 failed (W24)
- [x] Commands in `AGENTS.md` filled in with the real configure, build, run and test lines
- [x] [ARCHITECTURE.md](ARCHITECTURE.md) updated to match what was built

Exit checks:
- [x] Clean configure and build from a fresh clone (with submodules) using only the documented commands
- [x] A real guitar (or line) input is heard through a real third-party amp sim, and a take is recorded and plays back in time
- [x] Round-trip latency **measured** with a loopback at two or more buffer sizes and reported next to the figure the app shows
- [x] No allocation, lock or I/O on the audio thread, shown by a real-time-safety check, not by reading the code (held as F18: none in Carthage's code; third-party sites baselined)
- [x] No dropouts over a ten-minute monitored recording at the smallest stable buffer size; the buffer size and load are reported — iD44 at 48 kHz / 32 with AmpKnob (16 was not stable)

Report:

### Report — phase 1 (2026-11-17, verified)
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
              Audio workgroup: the iD44 provides one for up to 8 threads. Counted at
              os_workgroup_join in carthage-bench (W24) during a monitored soak with AmpKnob at
              48 kHz / 32: 77 joins, 0 failed, 7 render threads in the workgroup from start to end;
              graph rebuilds leave and rejoin. The workgroup is the same object after a device
              restart and stays joinable. Earlier evidence, superseded: `ps -M` showed the render
              threads at real-time priority 47; an Instruments Audio System Trace read a concurrent
              thread count of 1 (its meaning was not established; tracing also caused 5 late
              blocks, so that run is not a soak result).
Not done:     The settings panel was checked through carthage-snapshot on the iD44; three faults it
              showed are fixed (loopback pickers now default to the first output not used for
              listening and the first input not used for the guitar, and remember the cable per
              interface; the round trip and load figures fill in at once). Its Measure button has
              not been pressed through the cable in the app itself (the same probe was, through
              carthage-bench). Windows: nothing built or tested (CI arrives in phase 2). Thread
              and Address Sanitizer presets: not yet (phase 2, with CI).
Decisions:    F16 (AGPLv3), F17 (JUCE 9.0.5 + Tracktion develop), F18 (how F8 is checked);
              W7–W21: pins, minimum OS, ASIO, C++26/23 split, real-time check mechanics, Carthage's own
              recorder and device callback, plugin load gate, two dropout counts, folders, developer
              tools, carthage-bench, microphone access, carthage-snapshot; W22 (take placement),
              W23 (first MIDI scan at startup), W24 (workgroup membership counted).
Needs owner:  1. The go-ahead for phase 2. Outputs 3+4 can go back to "main mix" in the iD app.
              2. Approve macOS's microphone prompt for Carthage when it appears.
              3. Approve or edit the upstream drafts in docs/upstream/ before anything is posted.

## Phase 2 — CI, releases and in-app updates

Brief features: 12, and the whole "Updates and releases" section. Details in [RELEASING.md](RELEASING.md).

Deliver:
- [x] `ci.yml`: build and test on every push to `main` (macOS arm64, Windows x64)
- [x] `release.yml`: tag-triggered; `.dmg` for macOS arm64, Inno Setup `.exe` for Windows x64; version injected from the tag into CMake, `Info.plist` and Windows resources
- [x] Sparkle 2 and WinSparkle behind one interface; background check on launch; non-blocking banner with release notes, "Install on quit" (default) and "Restart now"; "Check for updates…" menu item; "Receive beta updates" setting
- [x] Stable and beta appcasts, EdDSA-signed; the workflow still builds and skips the feed when `SPARKLE_ED_PRIVATE_KEY` is missing — signed feeds published and verified; the no-key path is written but has not run (accepted by the owner, 2026-11-18)
- [x] Ad-hoc signing now; Developer ID + `notarytool` step present and switched off until `APPLE_*` secrets exist (the switched-off steps have not run)
- [x] Release notes include the one-time Gatekeeper step
- [x] The owner has the exact, tested steps to generate the key and add the secret (in RELEASING.md)

Exit checks:
- [x] v0.1.0 installed from the published `.dmg` on a real Mac (owner, 2026-11-18, macOS 27: dragged to Applications, ran the `xattr` command from the notes, opened)
- [x] v0.1.1 published; the installed app finds it, shows the banner and updates itself on quit (owner: card shown, Install on quit, reopened as 0.1.1)
- [x] The update installs without repeating the Gatekeeper step (O6); if it does not, reported to the owner — it did not repeat: 0.1.1 opened directly. macOS did ask for microphone access again (ad-hoc signing; see report)
- [x] A `-beta.N` tag appears only on the beta feed, and only an app with the beta setting on sees it (0.1.2-beta.1 and beta.2: stable feed stayed at 0.1.1; the owner's app saw each beta once the setting was on)
- [x] An update is not offered a restart during playback or recording, and "Restart now" with unsaved work saves or asks first (owner, 0.1.2-beta.2: greyed while playing, restarted into the update after stopping; there is no unsaved work before phase 4, see report)
- [x] Windows installer builds in CI and the WinSparkle path is exercised as far as the available hardware allows; what could not be tested is stated (no Windows machine: see report)

Report:

### Report — phase 2 (2026-11-18, 3185793; verified, the owner accepted the untested paths below)
Works:        Carthage is released from a tag. Pushing vX.Y.Z (or vX.Y.Z-beta.N) builds macOS arm64 and
              Windows x64, signs the DMG and the Inno Setup installer with the owner's EdDSA key,
              writes the stable and beta feeds and publishes the GitHub Release with install notes and
              the one-time Gatekeeper step. A failed release is rebuilt from its existing tag
              ("Run workflow"); tags are never moved. Installed Carthage checks once on launch, offers
              an update in a small card ("Install on quit" by default, or "Restart now", which waits
              while playing or recording), has "Check for updates..." in the app menu and Check now
              in Settings, shows its version there, and switches to the beta feed with "Receive beta
              updates". CI builds and tests both platforms on every push to main.
Tested:       Exit checks, on the owner's Mac (macOS 27, Apple silicon), with released builds:
              - v0.1.0 installed from the published DMG: dragged to Applications, `xattr` command from
                the notes, opened. (The Open Anyway route in the notes was not tried.)
              - v0.1.1: the installed 0.1.0 showed the card, installed on quit, reopened as 0.1.1.
              - O6: 0.1.1 opened directly, no Gatekeeper step (W28).
              - Betas: 0.1.2-beta.1 and beta.2 published as pre-releases; fetched from outside, the
                stable feed stayed at 0.1.1 while the beta feed carried each beta; the app found
                them once "Receive beta updates" was on and installed them.
              - Restart now greyed while playing and installed and reopened after stopping (beta.2).
              - Windows: CI builds and tests Windows x64; every release built the installer and the
                feed's Windows item; its EdDSA signature verifies against the public key (v0.1.0,
                checked with openssl). Not installed or run: no Windows machine (below).
              Both v0.1.0 downloads' signatures were verified against the public key outside Sparkle.
              Unit tests pass on both platforms in CI.
Measured:     Release, tag to published, with the compiler cache warm: 7-10 minutes (v0.1.1: macOS 5,
              Windows 8). CI: macOS 5-6 minutes; Windows 17-18 (it also builds the tests and the
              developer tools; cache hits were 44 % on its first warm run).
Not done:     - Windows: the installer, WinSparkle's check, its window and its install were not run on
                Windows (no machine). The WinSparkle code compiles and links in CI; its feed item is
                signed and verified. "Install on quit" does not exist on Windows (WinSparkle
                installs from its own window), as documented.
              - The workflow's no-key path (publish without feeds, with a warning) is written and has
                not run, because the key was set before the first release.
              - Developer ID signing and notarytool are written, switched off, and have never run.
              - "Restart now with unsaved work saves or asks first": nothing is unsaved until projects
                exist (phase 4). The hook (Updater::Hooks::prepareToRestart) is in place and refuses
                during a take; phase 4 makes it save or ask.
              - macOS asks for microphone access again after every update (ad-hoc signing gives each
                build a new identity, W28). Fix proposed: a stable signing certificate.
              - Windows CI time; the next step is caching the compiled JUCE/Tracktion library.
Decisions:    W25 (updater pins), W26 (version scheme), W27 (ccache), W28 (O6 answered: updates need no
              Gatekeeper step; the microphone prompt repeats). F19 and O9 record the owner's added
              features and their open details.
Needs owner:  1. Accept the untested paths above (no-key publish, Developer ID steps, Windows install
                 and update) as stated, or ask for any of them to be tested first.
              2. Whether to add a stable self-signed signing certificate now (keeps the microphone
                 permission across updates; you would create it once, like the update key).
              3. Answers on the questions page (O9) before phase 4; they do not block phase 3.
              4. The go-ahead for phase 3.

## Phase 3 — Plugin manager and scanning

Brief feature: 6.

Deliver:
- [x] Out-of-process scanner; a crashing or hanging plugin cannot take the app down; automatic blocklist; rescan; progress shown
- [x] Plugin list with fast fuzzy search, favourites, filters by vendor, format and type, last-used
- [x] Drag a plugin onto a channel or the timeline — onto the plugin card, the one insert phase 1 has; channels and the timeline accept it when they exist (phases 4–5)
- [x] O3 (CLAP) answered and recorded (W29: no native CLAP hosting in the pins; AU and VST3 only)

Exit checks:
- [x] Full scan of the owner's real AU and VST3 folders completes; counts and time reported
- [x] A plugin that crashes on load (a deliberately broken one will do) ends up on the blocklist and the app keeps running
- [x] The UI stays responsive during a scan (frame time reported)
- [x] Search returns the expected plugin within a keystroke's time on the full list

Report:

### Report — phase 3 (2026-11-18, 490e83f)
Works:        A plugin browser on the left (F8): search by any part of a name or maker (letters can be
              spread out), filters for instruments or effects, format and maker, favourites (star),
              and the five most recently used at the top. A plugin goes into the signal path by
              double-click, Enter or a drag onto the plugin card. Carthage checks new and updated
              plugins in the background at every launch, four scanner processes at a time, with the
              progress in the panel; Rescan checks everything again. A plugin that crashes or hangs
              its scanner, or loads nothing usable, is listed under "plugins Carthage can not use"
              with the reason and Try again; Intel-only builds say "Built for Intel Macs only".
Tested:       - Full scan (carthage-bench scanall, fresh cache): 227 plugin files on the owner's Mac,
                34.9 s cold, 16.7 s warm, 4 scanners: 220 loaded (221 plugin types: 135 AU, 86 VST3,
                18 instruments), 7 refused, all Intel-only builds (Nexus VST3 and AU, Pigments VST3,
                ValhallaRoom and ValhallaVintageVerb VST3 and AU), confirmed with lipo.
              - Crash and hang: two broken test VST3s (one aborts when loaded, one never returns).
                Unit test: the scanner reports "crashed" and "hung" (2 s limit). Real background scan
                with them among the 227 real plugins (10 s limit): both blocked with their reasons,
                the other scanners carried on, 229 files done in 25.3 s, the process unharmed.
              - Responsiveness: the app on the owner's 120 Hz display, full rescan at launch,
                CARTHAGE_FRAME_LOG: 19 s of scanning, 2,275 frames, median 8.33 ms, 99th
                percentile at most 16.7 ms, 11 late frames (0.5 %), worst one frame of 117 ms two
                seconds in (the first list fill). Owner used the app during and after: "works nicely".
              - Search: unit test over 2,000 plugins, worst query 1.1-1.3 ms; ranking tests ("amp"
                puts Amped and AmpKnob, then Neural Amp Modeler, above names with scattered letters).
Measured:     As above. Search 1.3 ms worst at 2,000 plugins; scan 16.7-34.9 s for 227 files.
Not done:     - Drag onto channels and the timeline: they do not exist yet (phases 4-5); the drag
                lands on the plugin card, the only insert so far. The owner has not reported trying
                the drag itself.
              - AU plugins that load nothing are labelled "may be built for Intel Macs only": AUs are
                registered by ID, so their binary is not checked the way a VST3's is.
              - Extra plugin folders exist in the engine (used by the tests) but not yet as a setting.
              - Running chosen plugins in their own process during playback (F20) is phase 5.
Decisions:    W29 (no CLAP: answers O3), W30 (owner's answers, O9), F20 (owner's ideas).
Needs owner:  1. Try dragging a plugin from the browser onto the plugin card, if you have not.
              2. The go-ahead for phase 4 (timeline, grid, tools, shortcuts; with autosave, projects
                 and tabs, and the sample browser, as you placed them).


## Phase 4 — Timeline, grid, snapping, tools, shortcuts

Brief feature: 2, and fixed decisions F5–F7. Built in two parts, each verified and reported on its
own (owner, 2026-11-18, W31).

### Phase 4a — the timeline and editing

Deliver:
- [ ] Lanes that hold any clips; zoom and scroll at display refresh rate
- [ ] Grid: default 1/4 beat with toggle (shortcut and toolbar button); bar, beat, 1/2–1/64, triplets, dotted, adaptive
- [ ] Snap to grid, clip edges, markers and playhead; hold Cmd/Ctrl to bypass for one drag
- [ ] Tools E P B C D T Z S with hold-to-switch; edge drag trims, Option/Alt stretches, Cmd/Ctrl slips
- [ ] Split at playhead or cursor, duplicate, nudge, crossfades, loop range, markers, cached waveform thumbnails
- [ ] Shortcut editor; every shortcut rebindable
- [ ] Undo/redo for everything so far, project save and load, autosave and crash recovery (O5, confirmed W31); autosave edit-driven: after a 2 s pause, at most every 2 minutes, the last 10 kept (W30)

Exit checks:
- [ ] Frame time while zooming and scrolling a project with a few hundred clips, measured on a 120 Hz display and reported
- [ ] Each tool key, its hold-to-switch behaviour, and each edge-drag modifier exercised in the running app
- [ ] Each snap target and the bypass key exercised
- [ ] Every edit undoes and redoes; a saved project reopens identical; killing the app mid-session offers recovery on next launch

Report: _not written yet_

### Phase 4b — projects as tabs, the sample browser, the start screen

Deliver (F19, F20, W30):
- [ ] Projects as folders, several open as tabs; only the front one plays, the others pause with plugins loaded; copy and paste between tabs
- [ ] "Collect samples into project", and always-copy as a setting
- [ ] The sample browser on the left with the plugin browser: built from the user's own folders ("Add folder"), deep folders, WAV, AIFF, FLAC, MP3, OGG; a click plays the whole sound by default, or plays while held (setting); preview to the main output with its own volume; search and favourites; key and tempo detection; optional stretch to project tempo; panel width and text size in Settings; drag onto lanes
- [ ] A start screen with recent projects and templates, on by default, switchable in Settings

Exit checks:
- [ ] Two projects open as tabs; switching is instant and only the front one plays; a clip copied from one pastes into the other
- [ ] A sample folder of a few thousand files browses, searches and previews without stalling (frame time reported); a dragged sample lands on a lane in time
- [ ] Key and tempo detection checked on samples with known key and tempo; results reported
- [ ] The start screen opens recent projects and templates; switching it off works

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
- [ ] **Owner's addition (F19, W30):** plugin presets in one format for one plugin or a whole Insert Line, in a folder the user chooses; opening .vstpreset and .aupreset, and other DAWs' presets as far as their formats allow
- [ ] **Owner's ideas (F20):** CPU use per plugin; running chosen plugins in their own process; a tuner on the main view; recording the processed sound beside the dry one; take comping; freeze; stems export; MIDI learn

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
- [ ] **Owner's ideas (F20):** illustrated getting-started help (wiring an interface and microphone, what plugins do, starting with guitar), input level help, and troubleshooting that finds and fixes the actual problem

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
