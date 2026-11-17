# Changelog

What changed in each release of Carthage, written for the people who use it. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org/). The release workflow takes each release's notes from the
matching section of this file.

Add a line under **Unreleased** with every user-visible change. Group lines under Added, Changed,
Fixed or Removed. Write what the user can now do, not what the code does.

## Unreleased

## 0.1.0 - 2026-11-17

The first version: one guitar, one amp sim, takes on a lane.

### Added
- Choose your audio interface, sample rate (44.1–192 kHz), buffer size and recording bit depth
  (16-bit, 24-bit, 32-bit float).
- Hear an input live through one Audio Unit or VST3 plugin, such as an amp sim. A plugin is checked in
  a separate process before Carthage loads it, and one that crashes the check is blocked.
- Record takes from that input. A take lands at the time you played it, corrected for the interface's
  latency, and shows as a waveform on a lane of takes.
- See the round trip latency the driver reports, and measure the real one with a cable from an output
  to an input. Carthage remembers which output and input the cable uses on each interface.
- See the audio-thread load, and how many dropouts there have been.
- While a take records, the input, output and audio device settings stay locked. If playback stops
  under a take, the take ends there, is saved in time, and the status line says so.
- Carthage checks for a new version when it opens and offers it in a small card: install it when
  you quit, or restart now. "Check for updates…" is in the app menu, and Settings can switch to
  beta versions.
