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
| 4. Timeline, grid, snapping, tools, shortcuts | Verified (4a 2026-11-18, 4b and 4c 2026-11-19); 4d and 4e done (2026-11-19; the wheel check moved to 4f); next 4f (design pass), 4g (loop region) |
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
- [x] Lanes that hold any clips; zoom and scroll at display refresh rate
- [x] Grid: default 1/4 beat with toggle (shortcut and toolbar button); bar, beat, 1/2–1/64, triplets, dotted, adaptive
- [x] Snap to grid, clip edges, markers and playhead; hold Cmd/Ctrl to bypass for one drag
- [x] Tools E P B C D T Z S with hold-to-switch; edge drag trims, Option/Alt stretches, Cmd/Ctrl slips
- [x] Split at playhead or cursor, duplicate, nudge, crossfades, loop range, markers, cached waveform thumbnails
- [x] Shortcut editor; every shortcut rebindable
- [x] Undo/redo for everything so far, project save and load, autosave and crash recovery (O5, confirmed W31); autosave edit-driven: after a 2 s pause, at most every 2 minutes, the last 10 kept (W30)

Exit checks:
- [x] Frame time while zooming and scrolling a project with a few hundred clips, measured on a 120 Hz display and reported
- [x] Each tool key, its hold-to-switch behaviour, and each edge-drag modifier exercised in the running app
- [x] Each snap target and the bypass key exercised
- [x] Every edit undoes and redoes; a saved project reopens identical; killing the app mid-session offers recovery on next launch

Report:

### Report — phase 4a (2026-11-18, 35b3137)
Works:        Eight lanes of clips with a bar and beat ruler; zoom (pinch, Cmd/Ctrl+scroll, the Zoom
              tool) and scroll. A grid of 1/4 beat by default (bar to 1/64, triplet, dotted, adaptive),
              toggled with G or Snap. Clips snap to the grid, other clips' edges, markers and the
              playhead; Cmd/Ctrl bypasses for one drag. Tools E P B C D T Z S, tap or hold to switch;
              edge drags trim, Option/Alt stretches, Cmd/Ctrl slips. Split at the playhead or with
              Slice, duplicate, nudge, mute, markers, a loop range; overlapping clips crossfade. Every
              edit is one named undo step. Projects as folders (New, Open, Save, Save as, Close with
              Cmd/Ctrl+W), autosave after a 2 s pause at most every 2 minutes keeping 10, and recovery
              after a crash. A shortcut editor in Settings: every command rebindable, with search and
              Reset all; menus, the toolbar letters and tooltips follow. Mac window keys: Cmd+M,
              Ctrl+Cmd+F; Return also returns to start.
Tested:       - In the running app on the owner's Mac (120 Hz), with the generated 300-clip demo
                (carthage-bench demo), partly by Claude through computer use, partly by the owner:
                every tool (move, trim, Stretch, Slice, Mute, Delete, Draw, Paint, Zoom); hold-to-switch,
                the Option and Cmd edge drags, every snap target and the bypass (owner: "everything
                works great"); five mixed edits undone and redone back to the identical state; G and
                Snap; a saved project reopened identical (zoom, scroll, moved clip); an edit autosaved
                2 s later, the app killed with kill -9, recovery offered on the next launch and the
                copy holding the edit; rebinding (Slice to X, saved to Shortcuts.xml), a key taken
                from another command with the note, Reset all; Cmd+W asks to save, Cmd+M, Ctrl+Cmd+F,
                Return to start. Playback heard by the owner.
              - Headless (ctest): grid steps, adaptive lines and every snap target with the bypass;
                move, trim, slip, stretch, split, duplicate, mute, markers, loop range, each as one
                undo step; project save/reopen, Save as with recordings, format refusals, autosave
                timing, keep-10 pruning, recovery markers. carthage-bench projects: PASSED.
Measured:     Frame time (CARTHAGE_FRAME_LOG, 120 Hz display, 300 clips): the owner's 13-minute
              session of zooming, scrolling, editing and playing: 766 s, 92,175 frames, 722 s with no
              late frame, 144 late frames in all. Zoom and scroll alone: 0-2 late frames a second,
              worst 16.7 ms, also zoomed out with every clip in view. Playback had 6-19 late frames a
              second (worst 41.7 ms) until 35b3137 took the disk out of painting; now 1-4 (worst
              16.7 ms), with one second of 11 late frames (33 ms) as playback starts. Launch: one
              1.2 s first frame and a second of up to 83 ms frames while 300 waveforms load.
Not done:     - Crossfades are made (Tracktion's auto-crossfade on overlaps) but not drawn on the
                clips, and not checked by ear.
              - The view does not follow the playhead during playback.
              - Which clip sits on top of an overlap follows start time; there is no "bring to front".
              - Recovery offers only the newest unclosed project; autosave folders of discarded
                untitled projects stay behind; a device change can leave an unnamed undo step.
              - The frame log has no timestamps, so the owner session's few slow seconds are counted,
                not tied to what was happening.
Decisions:    W32 (clip paths), W33 (opening is not an edit), W34 (Cmd/Ctrl+W, window keys),
              W35 (self-signed releases; amends F11).
Needs owner:  1. Make the signing certificate (docs/RELEASING.md, "Making the signing certificate").
              2. The go-ahead for phase 4b (tabs, the sample browser, the start screen).

### Phase 4b — projects as tabs, the sample browser, the start screen

Deliver (F19, F20, W30):
- [x] Projects as folders, several open as tabs; only the front one plays, the others pause with plugins loaded; copy and paste between tabs
- [x] "Collect samples into project", and always-copy as a setting
- [x] The sample browser on the left with the plugin browser: built from the user's own folders ("Add folder"), deep folders, WAV, AIFF, FLAC, MP3, OGG; a click plays the whole sound by default, or plays while held (setting); preview to the main output with its own volume; search and favourites; key and tempo detection; optional stretch to project tempo; panel width and text size in Settings; drag onto lanes
- [x] A start screen with recent projects and templates, on by default, switchable in Settings

Exit checks:
- [x] Two projects open as tabs; switching is instant and only the front one plays; a clip copied from one pastes into the other
- [x] A sample folder of a few thousand files browses, searches and previews without stalling (frame time reported); a dragged sample lands on a lane in time
- [x] Key and tempo detection checked on samples with known key and tempo; results reported
- [x] The start screen opens recent projects and templates; switching it off works

Report:

### Report — phase 4b (2026-11-19, c30dc66)
Works:        Several projects as tabs: only the front one plays, the others keep their plugins
              loaded; Ctrl+Tab switches, Cmd/Ctrl+W closes after asking, tabs drag to reorder.
              Cut, Copy and Paste of clips, also between tabs. File > Collect samples into project,
              and a setting to collect always. The sample browser beside the plugins (F7/F8): your
              folders as a tree, WAV, AIFF, FLAC, MP3, OGG; a click plays the sound from the top at
              the preview's own volume (or while held); search, favourites, key and tempo found in
              the background; drag onto a lane (a snapped ghost shows where), Finder files too.
              Signalsmith time-stretching (W36): stretched clips keep pitch; loops can fit the
              project tempo. A start screen with recent projects and templates in miniature, File >
              Save as template. Settings > Browser and > Projects. Sliders reset on double-click and
              follow the wheel (W37).
Tested:       - Owner, in the running app (2026-11-19): tabs and switching, only the front project
                playing, copy in one tab and paste in another, the sample browser (play, arrows,
                stars, search, drag from the browser and from Finder, the Browser settings), their
                own sample folder, the start screen, templates and switching it off: "All fine and
                works". Asked for, and got: a click on a playing sound starts it again.
              - carthage-bench projects: ten tab switches, nothing unsaved after switching, only the
                front project has a playback graph, cross-tab paste finds its audio, collecting the
                pasted clip's sample. carthage-bench samples on 2,976 generated sounds.
              - Headless: SampleTests (index, search, favourites, analysis cache, preview level,
                flush and end, key and tempo), EditingTests (copy and paste across folders and
                tempos, insertFile, stretch keeps pitch, fit to tempo), ProjectTests (collect target
                names, copyMedia, miniature).
              - Snapshots: samples, samples-playing, samples-search, start, tabs (carthage-snapshot).
Measured:     Tab switch 18-21 ms at worst. Library of 2,976 sounds: index 166 ms, slowest search
              4.0 ms, analysis 4.3 ms a file (about 13 s for all, in the background), preview ready
              2.6 ms after a click; the app held 120 Hz while indexing and analysing (0-1 late
              frames a second over 20 s, after one 250 ms frame at launch). Key and tempo on sounds
              of known key and tempo: A minor, C major, E minor, 90, 120 and 174 bpm all right;
              held chords no tempo; a noise burst nothing; a tuned hit its note. RTSan 60 s with
              NeuralAmpModeler and the preview playing: 0 failures, no report in Carthage's code, 0
              dropouts.
Not done:     - The preview plays loops as recorded; only a dropped loop is fitted to the tempo.
              - Key and tempo were checked on generated sounds and the owner's own folder by ear,
                not against a labelled real library; relative major and minor can swap.
              - Only one sample can be dragged at a time from the browser (Finder: several).
              - Tabs reopen as one project after a quit; the open tabs are not restored.
Decisions:    W34 (Cmd/Ctrl+W), W35 (self-signed releases), W36 (Signalsmith), W37 (value controls).
Needs owner:  The go-ahead for phase 5 (mixer, insert Lines, per-channel I/O).
### Phase 4c — views in their own windows, window layouts

Deliver (F21):
- [x] A view manager: each view (sample browser, plugin browser, timeline, signal path, Settings;
      later mixer, piano roll, tuner, big clock, meters, help) docked in the main window or in a
      window of its own; detach and reattach by a button on the view and a shortcut each; "Back to
      the default layout" in the menu bar and by shortcut
- [x] Layout slots (how many in Settings): save and recall from a Layouts menu in the menu bar, by
      shortcut too; app-wide; windows on a missing display come back to the main one
- [x] Plugin windows in layouts, kept above Carthage's windows, with a pin per window
- [x] Reopen the last session at launch (its tabs and layout), a setting beside the start screen
- [x] A command palette (Cmd/Ctrl+K): find and run any command, with its shortcut

Exit checks:
- [x] Every view detached to a second display and back, by button and by shortcut; the default
      layout restores everything
- [x] Two layouts saved and recalled across two displays; recalling one with a display unplugged
      brings its windows to the main display
- [x] Frame time with views in separate windows on a 120 Hz display reported; playback and
      recording unaffected (RTSan unchanged)
- [x] Quit and relaunch reopens the tabs and the layout; the palette finds and runs commands

Report:

### Report — phase 4c (2026-11-19, 7e121f8)
Works:        Every view (samples, plugins, timeline, signal path, Settings) moves to a window of
              its own and back: a pop-out button on the view, a pop-in button on its window (or
              closing it), and Cmd+Opt+S, P, T, I and comma. A Layouts menu: slots (Settings >
              Windows sets how many), Save layout as, Default layout, and a tick per view; Cmd+Opt+1
              to 9 recall, with Shift save, Cmd+Opt+0 the default. Layouts are app-wide; a window
              whose display is gone comes to the main one. Plugin windows stay on top (Plugin menu)
              and take the layout's place. When it opens: start screen, last session (projects and
              layout) or an empty project. Cmd+K finds and runs any command.
Tested:       - Owner, in the running app across three displays (2026-11-19): every view detached
                and back by button and shortcut, layouts saved and recalled, the default layout,
                the last session reopened, the palette: "4c checks pass with flying colors".
              - Before that, through the menus in the background: the Layouts menu, the timeline
                and plugins in their own windows, save layout 1, the default layout.
              - Snapshots: palette.png, views-away.png (carthage-snapshot). Unit tests and the
                project and sample benches unchanged and passing; CI green on both platforms.
Measured:     Frame log of the owner's session, 448 s on a 120 Hz display: 357 s with no late frame;
              182 late frames in all, in about a dozen seconds, worst 117 ms, when native windows
              are created, closed or moved (detaching, layouts), and the two launches (2.6 s and
              1.2 s first frames). In between, 120 Hz. RTSan not rerun: no audio-thread code changed
              in 4c (the last run, phase 4b, was clean).
Not done:     - A window hitch of up to about 0.1 s when a view is detached or a layout is applied.
              - Layouts are not covered by an automated test (they need real windows and displays).
              - The mixer, piano roll, tuner, big clock, meters and help join the views as they are
                built.
Decisions:    F21.
Needs owner:  The owner's next list of features (to be placed on the roadmap), then phase 5.


### Phase 4d — polish, workflow and tooling

Deliver (F22, W38, W39):
- [x] App-wide UI size (Settings 75-150 %, Cmd+= / Cmd+- / Cmd+0); the toolbar folds when narrow
- [x] Rename layouts; Cmd+W on a view's window sends it back
- [x] Tooltips on every tool; Cmd+C copies a showing tooltip, Copy tooltip on right-click
- [x] Toolbar: icons with their letters, space used well; the CPU bar hidden by default (Settings);
      a master volume slider beside the transport that also takes a typed value
- [x] Lanes: Lane 1, 2… renamable; add, delete, mute and solo from the lane header
- [x] Projects renamed from their tabs (asks first); tap tempo; a question when the tempo changes
      (stretch, move with the beat, leave in time; remember the choice)
- [x] Panels resizable by their border, double-click fits the longest text (the browser panel; the
      mixer's panels follow in phase 5)
- [x] Wheel: scroll on both axes and zoom time and lane height in every grid; lane-height shortcut
- [x] FL-style paste-by-click and right-click delete as Settings switches (off)
- [x] Metronome: built-in wooden click, hi-hat and cowbell, or your own sound
- [x] Plugin check at the first launch only, then a Settings switch (off)
- [x] Export logs (file or clipboard, macOS and Windows), a local crash report offered after a crash,
      project versions, an undo history list, project notes
- [x] Tooling: `.clang-tidy` with no findings in Carthage's code, RTSan, AddressSanitizer and UBSan in
      CI, fuzzing the project and sample readers, signposts, waveform overviews cached on disk;
      dependabot for GitHub Actions

- [x] Lanes (W41): Add lanes below 1/2/4/8, Option+Cmd+N, Cmd-click M and S, move by drag and
      Cmd+Up/Down, each lane's height from its border, the name column's border
- [x] Timeline (W41): free slice cut with a line, shading steps inside the beat
- [x] Tick-tock metronome (W41)
- [x] Settings search, views palette (Cmd+J), Cmd+1 to Cmd+9 for tabs (W41)
- [x] Settings test at the first launch and from Settings, with Yes/No (W41)
- [x] Pictures of every window with each project version, in a Restore window (W41)

Exit checks:
- [x] Every item exercised in the running app by the owner (the wheel check moved to 4f by the
      owner, 2026-11-19: no mouse at hand)
- [ ] CI runs clang-tidy, the sanitizers and RTSan on every push, and is green (the jobs run; the
      run covering the last commits was queued when this was written)
- [ ] Frame time and RTSan unchanged from 4c (RTSan clean; frame time at playback start is worse,
      see the report)

Report:

### Report — phase 4d (2026-11-19, 4a67d88)
Works:        Polish and workflow from the owner's two lists (F22/W38, W41): toolbar icons with keys,
              master level, tempo with Tap and the tempo-change question, a tick-tock metronome
              (K) with other sounds or your own; lanes named, renamed, added (1/2/4/8, Option+Cmd+N),
              moved (drag, Cmd+Up/Down), muted and soloed (Cmd-click for the others/alone), each with
              its own height, and a draggable name column; free slicing with a line; shading inside
              the beat; Option-scroll lane height; a resizable browser panel; interface size
              75-150 % with a folding toolbar; Cmd+W per window; named layouts; copyable tooltips;
              FL-style switches; Settings search; a views palette (Cmd+J); Cmd+1-9 tabs; projects
              renamed from tabs; notes; versions with pictures and a Restore window; undo history;
              Export log and a crash report; a settings test at the first launch; plugins checked at
              the first launch only. Tooling: clang-tidy with no findings, ASan/UBSan, RTSan tests and
              fuzzing in CI, dependabot, signposts, waveform overviews on disk.
Tested:       - Owner, two rounds in the running app (2026-11-19, checks page): round 1, 20 of 23 work,
                Cmd++ and the undo history fixed and then working in round 2; round 2, all 17 work.
                The wheel is not checked yet (no mouse). Owner notes acted on: Cmd+W in the notes
                window, the tab menu's text, Option wording, shortcuts kept with the FL switches.
              - Unit tests grew to cover lanes, tempo follow, tap tempo, metronome sounds (and their
                spectrum), the log, overview cache, versions, the settings test's rules; the
                bench's project run covers rename, versions with pictures; snapshots for each view.
              - Found by the new tools: a use-after-free deleting clips (ASan), JUCE's CoreAudio
                reader spinning on a damaged file (fuzzing, W40).
Measured:     RTSan soak, 60 s, built-in mic at 48 kHz / 128, metronome and preview playing: 25
              distinct reports, all on the third-party baseline, no failures. Normal build, same
              soak: no dropouts, no late blocks, load 1.3 % average, 9.6 % worst block. Frame time,
              300-clip demo on a 120 Hz display, 59 s with playback: 54 s with no late frame; 46 late
              frames in a 4-second burst as playback started (worst 50 ms), against one second and
              33 ms in 4a; steady 120 Hz otherwise. Fuzzing: 584k audio and 82k project inputs clean
              (after W40). Settings test on this Mac: clean at 32 samples, recommended 64.
Not done:     - The wheel check (owner, with a mouse): moved to 4f.
              - The longer hitch when playback starts: measured once; to look into in 4f.
              - Tracktion's clip reader still has the CoreAudio format (W40, not patched).
Decisions:    W38, W39, W40, W41.
Needs owner:  Nothing: the owner moved the wheel check to 4f and gave the go-ahead for 4e
              (2026-11-19).

### Phase 4e — the sample editor

Deliver (F22, W38, W42):
- [x] Double-click a clip: the sample editor with its waveform (click to play from there, as in the
      browser), key and tempo, gain, a volume envelope, fades, reverse, start and end, pitch,
      time-stretch and Fit to tempo
- [x] Edits belong to the sample: every clip of it follows; Make unique splits one off and names it
      ("Kick", "Kick (2)"); key and tempo shown on every clip on the grid
- [x] Fit one sample to the tempo: Option while dropping, the right-click menu, the editor
- [x] Fade shapes (linear, convex, concave, S-curve) and a shape drawn with points, FL-style:
      click adds a point, drag moves it, right-click removes it, a handle bends each line (owner)
- [x] The owner's additions: rename a sample, a formant shift (W44), lane colours, the preview
      volume figure, waveforms that follow the gain, Ctrl for fine shape drags, Cmd+M per window

Exit checks:
- [x] An edit changes every clip of the sample and undoes in one step; Make unique leaves the others
- [x] Owner's checks in the running app ("otherwise a pass"; the rising-line bend direction was
      fixed after and is the one item to confirm)

Report:

### Report — phase 4e (2026-11-19, e9648c2)
Works:        Double-click a clip: the sample editor opens under the lanes (Escape, the clip again or
              Cmd+W in front closes it; its border drags). The waveform plays from a click, its bright
              part is what plays and its edges drag; it grows and shrinks with the gain. Gain, pitch,
              formant, fades with four shapes, reverse, Fit to tempo, Make unique and a drawn shape
              (points, bends, straighten, Ctrl for fine work) act on every clip of the sample as one
              undo step, never on the file (W42). The sample can be renamed; its key and tempo show
              in the editor and on every clip. Option-drop and the clip menu fit a loop to the tempo.
              Lanes take colours; Cmd+M minimises the window in front.
Tested:       - Owner in the running app, two rounds (2026-11-19): a pass, with the bend direction for
                rising lines fixed after.
              - SampleEditTests: grouping, shared edits and undo, Make unique, rename, the envelope's
                timeline placement and following a move, fade shapes, formant; the formant plugin's
                blocks inside a real-time scope (clean under RTSan).
              - carthage-bench clip-envelope and clip-formant: rendered proof (unity matches no
                envelope, silence where it drops, follows a moved clip; formant starts within 0.4 ms
                of the plain render at the same 220 Hz, with a different colour).
Measured:     As above. The envelope and formant run on the clip at playback; no device soak with them
              playing yet.
Not done:     - The editor's layout (to 4f).
              - Rendering checks need the bench: Tracktion's renderer hangs in the headless tests.
              - A renamed sample does not take new drops of its file (they start as the file's own).
Decisions:    W42, W43, W44.
Needs owner:  Confirm the rising-line bend; then the go-ahead for 4f (the design pass).

### Phase 4f — the design pass

Deliver (F22, W38):
- [ ] A refinement of the whole UI: type (a bundled open-licence typeface, once the owner approves the
      choice), icons, spacing, motion that answers actions; same theme and density
- [ ] The sample editor's layout: its buttons and sliders placed with care (owner, 4e)
- [ ] Carried from 4d: the owner's wheel check (scroll, Shift, Cmd and Option with a mouse), and the
      longer frame hitch when playback starts (measure again; fix if it holds)

Exit checks:
- [ ] Before and after snapshots of every screen; owner's approval

Report: _not written yet_

### Phase 4g — loop region

Deliver (owner, 2026-11-19):
- [ ] A loop region drawn on the main grid: drag across the ruler (or with a modifier on the lanes)
      to set it, drag its edges to change it, and it snaps like everything else; playback loops it
      when looping is on; a double-click on it clears it
- [ ] The same loop region in the piano roll (built with phase 6, on the shared grid code)

Exit checks:
- [ ] Set, change, loop and clear by mouse in the running app; one undo step each
- [ ] Owner's checks in the running app

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
- [ ] **Owner's list (F22, W38):** plugins in their own processes except the live input chain,
      grouped automatically by crash history (on by default; your own grouping when off); presets
      under each plugin in the browser and from folders onto channels; the master volume becomes the
      master fader's twin
- [ ] **Export (owner, 2026-11-19; moved here from phase 7):** Cmd+R opens Export: the loop region if
      there is one, or the whole project from its first sound to its last; WAV 16/24/32-float and
      FLAC, with the quality settings from feature 8; it renders the mix through the mixer and master
- [ ] **Owner's ideas (F20):** CPU use per plugin; running chosen plugins in their own process; a tuner on the main view; recording the processed sound beside the dry one; take comping; freeze; stems export; MIDI learn

Exit checks:
- [ ] An exported file (Cmd+R) nulls against the same range played back in the app; the loop region
      and the whole project are both exported
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
- [ ] **Owner's list (F22):** scales to pick and snap notes to; swing for the project, the piano roll and each piano-roll channel
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
- [ ] (Export moved to phase 5, with Cmd+R and the loop-region or whole-project choice)
- [ ] README updated with real screenshots, install steps and the shortcut table; feature cards added to the README art
- [ ] A pass over every feature in the brief, 1–12, with anything missing listed
- [ ] **Owner's ideas (F20):** illustrated getting-started help (wiring an interface and microphone, what plugins do, starting with guitar), input level help, and troubleshooting that finds and fixes the actual problem

Exit checks:
- [ ] Every folder button opens the right folder
- [ ] The help page's shortcut list follows a rebind
- [ ] A full song made end to end: guitar tracked through an amp sim, drums and samples on the timeline, a melody in the piano roll, mixed and exported

Report: _not written yet_

---

## After phase 7

- [ ] **Owner's list (F22, W38):** a help assistant that knows Carthage: on-device (Apple's model on
      macOS 26+, an optional downloaded model elsewhere), reading Carthage's own measurements (levels,
      clipping, CPU, the plugin chain, settings), the help pages and the project, to answer "why is my
      kick clipping?"

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
