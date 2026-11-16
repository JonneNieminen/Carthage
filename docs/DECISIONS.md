# Decisions

A running log of what has been decided, by whom, and what is still open. Read it before proposing a
change of direction; add to it when a decision is made. Newest entries go at the bottom of each list.

Three kinds of entry:

- **Fixed** — set by the owner in [BRIEF.md](BRIEF.md). Do not revisit. If one seems wrong or
  impossible, stop and ask the owner; do not work around it.
- **Working** — made while building. Can change, but only with a new entry saying why.
- **Open** — needs the owner's answer, or needs checking against current sources. Each says who
  resolves it and when it starts to block work.

## Fixed (from the brief)

| # | Decision |
| --- | --- |
| F1 | Native C++ app on JUCE + Tracktion Engine: latest stable of each, as git submodules pinned to commits. CMake + Ninja. C++23 where the toolchains allow. |
| F2 | macOS on Apple Silicon first, built natively. Windows x64 is the secondary target. |
| F3 | Licence GPLv3, personal/open source. |
| F4 | Hosts AU (macOS) and VST3 (both). **No stock plugins at all.** CLAP only if the current JUCE/Tracktion hosts it natively. |
| F5 | Routing is FL-style and decoupled: timeline lanes hold any clips; each audio clip, instrument or input routes to any mixer channel. |
| F6 | Select tool edge drag: plain drag trims non-destructively, Option/Alt+drag time-stretches, Cmd/Ctrl+drag slips content. |
| F7 | Tool keys: E select, P draw, B paint, C slice, D delete, T mute, Z zoom, S stretch. Holding a key switches temporarily; releasing returns to the previous tool. Every shortcut is rebindable. |
| F8 | Real-time safe engine: no allocation, lock or I/O on any audio thread. Audio workgroups on macOS, plugin delay compensation, monitored input chain in-process. |
| F9 | Plugin scanning runs out of process, with an automatic blocklist. |
| F10 | Updates: Sparkle 2 (macOS) and WinSparkle (Windows) from one EdDSA-signed appcast on GitHub Releases, plus a separate beta feed. Never interrupt playback or recording; never restart with unsaved work. |
| F11 | Releases: tag `vX.Y.Z` triggers the workflow; a suffixed tag is a pre-release on the beta feed only. Version comes from the tag. Ad-hoc signing now, Developer ID + notarisation ready but off until `APPLE_*` secrets exist. |
| F12 | Look: Cothon-dark icon; graphite `#1C2127` background, sky `#8FD0F5` accent, greys `#8B96A1` and `#D5DCE3`; flat, restrained, round-capped like Prism. **Dark only for v1.** Dense but readable, high-contrast text. |
| F13 | Build order is the seven phases in the brief. A phase must build, run and be verified before the next starts. |
| F14 | Commit and push straight to `main`. The "Bypassed rule violations" notice is expected. |
| F15 | Use the frontend-design plugin for every UI decision and clangd for C++ navigation. |

## Working

| # | Date | Decision | Why |
| --- | --- | --- | --- |
| W1 | 2026-11-16 | The theme lives in `design/tokens.css` and is documented in `design/README.md`. The app's C++ theme header mirrors the dark column value for value. | One source for colour and type before any UI exists. |
| W2 | 2026-11-16 | Surfaces: window `--bg` is graphite `#1C2127` (F12). Panels step lighter (`--bg2`, `--card`); the timeline and piano-roll canvas and meter wells step darker (`--well` `#14181C`). | The brief fixes the background; a DAW still needs a recessed canvas and raised panels to read as layers without borders everywhere. |
| W3 | 2026-11-16 | Eight track lane colours (`--lane-*`) and three signal colours (`--ok`, `--warn`, `--clip`) extend the brief's four-colour palette. | A mixer and timeline need to tell tracks apart, and meters need zones. The accent stays reserved for "live". |
| W4 | 2026-11-16 | Light values stay in `tokens.css` for the README artwork and a later light theme. The app does not ship them in v1. | F12 says dark only; the repo artwork follows GitHub's light/dark setting. |
| W5 | 2026-11-16 | The icon master is `design/icon/carthage-icon.svg`. It is byte-identical to the `01-cothon-dark.svg` concept the brief names; the concept files and their `make.py` are not in this repository. | Only the chosen design was committed. Build the `.icns`, Icon Composer bundle and `.ico` from this file. |
| W6 | 2026-11-16 | The source layout, module boundaries and threading model in [ARCHITECTURE.md](ARCHITECTURE.md) are a starting proposal. Phase 1 confirms or amends them and updates that file. | Nothing is built yet; the layout should be tested by real code before it hardens. |

## Open

| # | Question | Who resolves | Blocks |
| --- | --- | --- | --- |
| O1 | **Licence text.** `LICENSE` in the repo is MIT; the brief says GPLv3 (F3). Check the current open-source licence terms of JUCE and Tracktion Engine before replacing the file: if either is offered under AGPLv3 rather than GPLv3, confirm with the owner which licence Carthage itself carries. | Owner, after the agent reports what the current licences say | Phase 1 (first commit that adds the submodules) |
| O2 | **C++23 level.** Which C++23 features both Apple Clang (current Xcode) and MSVC accept, and whether JUCE/Tracktion headers compile cleanly at that standard. Record the chosen `CMAKE_CXX_STANDARD` and any banned features here. | Agent, by building | Phase 1 |
| O3 | **CLAP.** Does the pinned JUCE or Tracktion host CLAP natively? If not, CLAP is out for v1 (F4). Record the answer with the version checked. | Agent, from current changelogs | Phase 3 |
| O4 | **ASIO on Windows.** Confirm the ASIO SDK's current licence allows a GPLv3 build to be distributed, and how the SDK is obtained in CI. | Agent reports, owner decides if it is not clean | Windows audio in phase 1–2 |
| O5 | **Features the build order does not place.** Feature 10 (undo/redo, save/load, autosave, crash recovery) and feature 11 (metronome, count-in, loop recording with takes, punch) have no phase. [ROADMAP.md](ROADMAP.md) proposes where they go; the owner confirms. | Owner | Phase 4 |
| O6 | **Sparkle with ad-hoc signing.** The brief requires proof on a real install that updates after the first one install without the Gatekeeper step. If that cannot be made to work without a Developer ID, report it instead of shipping a workaround. | Agent tests, owner decides | Phase 2 exit |
| O7 | **"Latest stable" pins.** Which JUCE and Tracktion Engine commits are pinned, and that this Tracktion version supports this JUCE version. Record both with the date. | Agent | Phase 1 |
| O8 | **Minimum OS versions** for macOS and Windows. Not stated in the brief; they follow from the pinned JUCE, Sparkle 2 and the audio-workgroup API. Propose and record. | Agent proposes, owner confirms | Phase 1 |

## How to add an entry

Give it the next number in its list, today's date, and one or two sentences. When an open question is
answered, move it to Working (or Fixed, if the owner ruled on it) and keep its text so the history
reads straight through. Never delete an entry; supersede it with a new one that names the old number.
