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
| 1. Skeleton, audio settings, monitored guitar, record a clip | Not started |
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
- [ ] CMake + Ninja project with JUCE and Tracktion Engine as pinned submodules; presets for debug and release; Apple Silicon native build
- [ ] Open questions O1, O2, O7, O8 in [DECISIONS.md](DECISIONS.md) answered and recorded
- [ ] App window in the Carthage theme (dark), with the theme header mirroring `design/tokens.css`
- [ ] Audio settings: device, buffer size, sample rate (44.1–192 kHz), recording bit depth; round-trip latency shown in ms; CPU / audio-thread load meter
- [ ] One input monitored through one AU or VST3 plugin, in-process
- [ ] Record that input to a clip on disk through a lock-free FIFO
- [ ] Audio threads in the device's audio workgroup on macOS
- [ ] Commands in `AGENTS.md` filled in with the real configure, build, run and test lines
- [ ] [ARCHITECTURE.md](ARCHITECTURE.md) updated to match what was built

Exit checks:
- [ ] Clean configure and build from a fresh clone (with submodules) using only the documented commands
- [ ] A real guitar (or line) input is heard through a real third-party amp sim, and a take is recorded and plays back in time
- [ ] Round-trip latency **measured** with a loopback at two or more buffer sizes and reported next to the figure the app shows
- [ ] No allocation, lock or I/O on the audio thread, shown by a real-time-safety check, not by reading the code
- [ ] No dropouts over a ten-minute monitored recording at the smallest stable buffer size; the buffer size and load are reported

Report: _not written yet_

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
