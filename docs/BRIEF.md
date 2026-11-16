# The Carthage brief

This is the owner's build brief, kept word for word. It is the source of truth for **what** Carthage
is. Where another document disagrees with it, the brief wins and the other document needs fixing.

- How the brief maps onto this repository, and the points still open: [DECISIONS.md](DECISIONS.md)
- How it is built: [ARCHITECTURE.md](ARCHITECTURE.md)
- Phase status and exit checks: [ROADMAP.md](ROADMAP.md)

Do not edit the text below the line unless the owner changes the brief. Record a change to it as a new
entry in [DECISIONS.md](DECISIONS.md).

---

# Build Carthage — a native DAW (C++ / JUCE + Tracktion Engine)

You are building Carthage, a DAW for macOS (Apple Silicon first) with Windows as a secondary target.
Main uses: tracking electric guitar live through third-party amp sims, building tracks on a timeline
(drums, samples), writing melodies in a piano roll for third-party instruments, then mixing and mastering.
Goal: faster, smoother and more reliable than FL Studio on an ARM Mac. Efficiency and minimalism
come first, with no gimmicks.

Repo: JonneNieminen/Carthage (public). Commit and push straight to main. A ruleset protects main, and my
admin account bypasses it, so the "Bypassed rule violations" notice is expected.

Use the frontend-design plugin for every UI decision, and clangd for C++ navigation.

## Fixed decisions (do not revisit)
- **Stack:** the latest stable JUCE and Tracktion Engine (git submodules, pinned commits), CMake + Ninja,
  C++23 where the toolchains allow. Licence: GPLv3, personal/open source. Build Apple Silicon natively.
  Check the current JUCE/Tracktion changelogs and use their newest features instead of assuming older APIs.
- **Plugins:** host AU (macOS) and VST3 (both platforms). No stock plugins at all; Carthage relies
  entirely on third-party plugins. Add CLAP hosting only if the current JUCE/Tracktion supports it natively.
- **Routing is FL-style and decoupled:** timeline lanes hold any clips, and each audio clip, instrument
  or input routes to any mixer channel.
- **Edge drag with the Select tool:** a plain drag trims non-destructively. Option/Alt+drag time-stretches.
  Cmd/Ctrl+drag slips the content inside the clip.
- **Tool shortcuts are FL-style letters:** E select, P draw, B paint, C slice, D delete, T mute, Z zoom,
  S stretch. Holding a tool key switches temporarily and releasing it returns to the previous tool.
  All shortcuts are rebindable in a shortcut editor.

## Features for v1
1. **Engine:** real-time safe (no allocations, locks or I/O on the audio thread). Use macOS audio workgroups
   for every audio thread, plugin delay compensation, and keep the monitored input chain in-process.
2. **Timeline:** smooth zoom and scroll at display refresh rate (ProMotion 120 Hz). Default grid is
   1/4 beat (4 steps per beat), toggled with a shortcut and toolbar button. Grid options: bar, beat,
   1/2–1/64, triplets, dotted, plus adaptive grid that follows zoom. Snapping: to grid, to clip edges,
   to markers and to the playhead. Hold Cmd/Ctrl to bypass snap for one drag. Also: clip split at the
   playhead or cursor, duplicate, nudge, crossfades, loop range, markers, cached waveform thumbnails.
3. **Mixer:** master channel pinned on the left with 50 channels beside it by default; add, remove,
   rename, colour and reorder channels. Fader, pan, mute, solo, record arm, input monitoring and a
   peak/RMS meter on each. Sends and buses. Any channel can route to any other or to master.
4. **Insert "Lines":** each channel has an unlimited vertical list of plugin slots that grows as you add
   plugins. Drag to reorder, bypass, wet/dry, open UI, and drag a plugin between channels.
5. **Per-channel I/O:** a compact input/output picker on each channel (mono/stereo hardware inputs,
   output to master, bus or hardware). Include a quick "Guitar in → this channel, armed, monitoring"
   action and keyboard-driven selection.
6. **Plugin manager:** background out-of-process scanning so a crashing plugin can't crash Carthage, with
   an automatic blocklist, rescan and progress shown. Fast fuzzy search, favourites, filter by
   vendor/format/type, and drag onto a channel or timeline. Remember last-used plugins.
7. **Piano roll:** FL-style, using the same grid and snap system as the timeline. Draw/paint/slice/delete
   tools, velocity lane, quantize, note length presets, scale highlighting, ghost notes from other
   patterns. Drives third-party instruments.
8. **Audio settings:** pick the audio interface (CoreAudio; ASIO and WASAPI on Windows), buffer size,
   sample rate (44.1–192 kHz), bit depth for recording, and the export quality and format
   (WAV 16/24/32-float, plus FLAC). Show round-trip latency in ms, offer a low-latency monitoring mode,
   and show a CPU / audio-thread load meter.
9. **Help page:** an interactive in-app "How to use Carthage" page. It walks through tools and shortcuts
   and lists every folder Carthage uses (projects, recordings, autosaves, plugin scan cache, settings,
   AU/VST3 system folders), each with a button that opens it in Finder or Explorer.
10. **Reliability:** undo/redo everywhere, autosave with crash recovery, and project save/load. Recording
    goes to disk through a lock-free FIFO.
11. **Recording extras:** metronome, count-in, loop recording with takes, and punch in/out.
12. **In-app updates** (see below).

## Updates and releases (model on ../Prism/.github/workflows/release.yml)
- **Updaters:** Sparkle 2 on macOS and WinSparkle on Windows. Both read one EdDSA-signed `appcast.xml`
  published as a GitHub Release asset. The feed URL is
  `https://github.com/JonneNieminen/Carthage/releases/latest/download/appcast.xml`.
  Use a separate beta feed, published alongside it, for pre-releases.
- **When updates happen:** check in the background on launch, then show a small, non-blocking banner
  with the release notes and two choices, "Install on quit" (default) and "Restart now". Never interrupt
  playback or recording, and never restart with unsaved work: save the project first or ask.
  Put "Check for updates…" in the app menu, plus a settings toggle for "Receive beta updates".
- **Release workflow:** pushing a tag `vX.Y.Z` triggers it. A tag with a suffix, like `-beta.N`, is
  marked as a pre-release and goes only to the beta feed. Jobs:
  - Build macOS arm64 and package a `.dmg`.
  - Build Windows x64 and package an Inno Setup installer `.exe`.
  - Sign both with the EdDSA key, generate the appcast(s) with release notes from the tag/changelog,
    and publish everything to the GitHub Release.
- **Secrets:** the EdDSA private key lives only in the GitHub secret `SPARKLE_ED_PRIVATE_KEY`; the public
  key is embedded in the app. If the secret is missing, the workflow still builds but skips the feed,
  like Prism does. Give me the exact steps to generate the key and add it as a secret.
- **Signing:** no Apple Developer ID yet, so builds are ad-hoc signed like Prism. Include a ready but
  disabled Developer ID signing + `notarytool` step that switches on when the `APPLE_*` secrets exist.
  Release notes must include the one-time Gatekeeper step for a first install. Verify on a real install
  that later Sparkle updates install without it.
- **Versioning:** the version comes from the tag and is injected into CMake, `Info.plist` and the
  Windows resources. Add a CI build-and-test workflow for every push to main.

## Look
- **Icon:** the chosen "Cothon dark" concept in `design/icon/` (`01-cothon-dark.svg`, generated by
  `make.py`). Build a macOS `.icns`, an Icon Composer bundle and a Windows `.ico` the way
  `../Prism/scripts/render-icons.sh` does.
- **Theme:** graphite (#1C2127) background, light blue accent (#8FD0F5), greys #8B96A1 and #D5DCE3.
  Use the same restrained, flat, round-capped style as the Prism app. Dark only for v1, with high-contrast
  text and a dense but readable layout.

## Build order (each phase must build, run and be verified before the next)
1. Project skeleton, audio device settings, then a guitar input monitored through one AU/VST3 plugin and
   recorded to a clip. Measure and report the latency.
2. CI and the release pipeline with Sparkle/WinSparkle. Prove it works end to end: install v0.1.0,
   publish v0.1.1, and confirm the app updates itself on quit.
3. Plugin manager and scanning.
4. Timeline with grid, snapping, tools and shortcuts.
5. Mixer with insert Lines and per-channel I/O.
6. Piano roll.
7. Help page, icon and polish.
After each phase, report what works, what was tested (including with real plugins) and what isn't done yet.

Ask me before doing anything that conflicts with these decisions.
