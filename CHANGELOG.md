# Changelog

What changed in each release of Carthage, written for the people who use it. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org/). The release workflow takes each release's notes from the
matching section of this file.

Add a line under **Unreleased** with every user-visible change. Group lines under Added, Changed,
Fixed or Removed. Write what the user can now do, not what the code does.

## Unreleased

### Added
- A loop region: drag on the ruler to set it, drag its edges to change it, double-click it to
  clear it; it snaps to the grid. L or the new loop button turns looping on and off. Each change
  is one undo step.
- Views in their own windows: the samples, plugins, timeline, signal path and Settings each move to
  a window of their own and back, by the button on the view or Cmd+Option+S, P, T, I and comma;
  closing that window brings the view back. Window layouts: save where every window is with
  Cmd+Option+Shift+1 to 9, recall with Cmd+Option+1 to 9 or the Layouts menu; Cmd+Option+0 puts
  everything back. Settings > Windows sets how many layouts the menu keeps.
- Find a command (Cmd+K): type part of a command's name and press Return to run it.
- Plugin windows stay above Carthage's own windows (Plugin > Keep plugin windows on top).
- Settings > Projects > When it opens: the start screen, the last session (the projects and windows
  you quit with), or an empty project.
- Projects open as tabs across the top of the window. Click a tab, or press Ctrl+Tab and
  Ctrl+Shift+Tab, to switch; only the front project plays, the others wait with their plugins
  loaded. File > New and Open add a tab; Cmd/Ctrl+W, the tab's cross or a middle click closes one,
  asking to save first. Drag tabs to reorder them.
- Cut, Copy and Paste for clips (Cmd/Ctrl+X, C, V), also from one project tab to another. Paste
  puts the clips at the playhead, on the lanes they came from.
- File > Collect samples into project copies the samples a project uses from other folders into
  its own Samples folder. Settings > Projects can do it whenever samples arrive; Save as takes the
  Samples folder along.
- A sample browser on the left, beside the plugins (switch at the top, or F7 and F8). Add the
  folders you keep your samples in: every WAV, AIFF, FLAC, MP3 and OGG file is listed, folders and
  all. A click plays a sound through the main output at its own preview volume; the arrow keys move
  and play. Search across every folder, star favourites, and drag a sound onto a lane (sounds
  dragged in from the Finder land there too). Settings > Browser sets the panel's width, the text
  size, and whether sounds play only while held.
- A start screen when Carthage opens: your recent projects and templates, each shown as a
  miniature of its arrangement, with New and Open. File > Save as template keeps a project to
  start new ones from. Switch the start screen off in Settings > Projects, or open it from File.
- The sample browser finds each sound's key and tempo in the background and shows them beside
  its name. Settings > Browser can fit a dropped loop to the project's tempo.

### Changed
- Shift with a mouse wheel scrolls the lanes in time on macOS; the time scrollbar starts at the
  lane names' border; a fade past the waveform's edge no longer draws over the editor's controls.
- A reversed sample is drawn reversed, in the sample editor and on the lanes; its shape points
  mirror with it, and clicking the waveform plays it reversed from there.
- Switches slide, the chosen tool cross-fades and the sample editor rises into place when it
  opens. Nothing moves during playback, and macOS Reduce motion (Windows: Animation effects off)
  turns it all off.
- The sample editor is laid out in groups: Play and the shape tools above the waveform, Reverse,
  Fit to tempo and Make unique below it, and Sound and Fades in a column on the right.
- The interface is set in Barlow, with Barlow Semi Condensed for dense rows and IBM Plex Mono for
  figures, built into the app so it looks the same on macOS and Windows.
- Clicking a sound in the sample browser always plays it from the top, even while it plays.
- Sliders: double-click puts one back to its default; the scroll wheel over it adjusts it, Shift
  for fine steps.
- Stretching a clip (the S tool, or Option/Alt on an edge) keeps its pitch.


## 0.1.2-beta.4 - 2026-11-18

### Changed
- Nothing new to use: this update checks that Carthage keeps its microphone permission across
  updates. It should open without asking.

## 0.1.2-beta.3 - 2026-11-18

### Added
- The usual Mac keys: Cmd+W closes the project (asking to save first), Cmd+M minimises, Ctrl+Cmd+F
  goes full screen (F11 on Windows), in File and a new Window menu. Return also goes back to the
  start, for keyboards without a Home key.
- Projects: File > New, Open, Save and Save as (Cmd/Ctrl+N, O, S, Shift+S). A project is a folder
  holding its file and its recordings; Save as copies the recordings along. Zoom, scroll and the
  grid are saved with it. Carthage asks before closing a project with unsaved changes.
- Keyboard shortcuts in Settings (and in the app menu on macOS, the File menu on Windows): every
  command with its keys. Click a key to change or remove it, or + to add one; a key another command
  had moves over and Carthage says which. Search, and Reset all. The toolbar's tool letters and
  every tooltip show the keys you chose.
- Autosave: after you pause editing for 2 seconds, at most every 2 minutes, keeping the last 10.
  If Carthage does not close properly, it offers the newest autosave the next time it opens.
- A timeline with eight lanes, a bar and beat ruler, and a grid (1/4 beat by default; bar to 1/64
  beat, triplet, dotted, or adaptive to the zoom), switched with G or the Snap button. Clips snap to
  the grid, to other clips' edges, to markers and to the playhead; hold Cmd (Ctrl on Windows) to
  move freely.
- Tools on the toolbar and on their keys: E select, P draw, B paint, C slice, D delete, T mute,
  Z zoom, S stretch. Tap a key to switch; hold it to use the tool until you let go. Drag a clip's
  edge to trim it, with Option/Alt to stretch it, with Cmd/Ctrl to slip its audio.
- Undo and Redo for every edit; split at the playhead, duplicate, delete, nudge with the arrow keys,
  markers, and loop the selection, from the Edit menu and their keys.
- A plugin browser on the left (F8): search by any part of a name or maker, filter by
  instruments or effects, format and maker, star favourites, and find recently used plugins at the
  top. Double-click a plugin, press Enter, or drag it onto the plugin card to use it.
- Carthage checks new and updated plugins in the background when it opens, several at a time, and
  Rescan checks them all again. Plugins that can not be used are listed with the reason, such as
  "Built for Intel Macs only", and can be tried again.

### Changed
- macOS no longer asks for microphone access after every update. It asks once more after the
  first update signed this way, then keeps your answer.

## 0.1.2-beta.2 - 2026-11-18

### Added
- With beta updates on, Settings says so under the switch.

## 0.1.2-beta.1 - 2026-11-18

### Added
- A Check now button beside the version in Settings, under Updates.

## 0.1.1 - 2026-11-18

### Added
- Settings shows which version of Carthage you are running, under Updates.

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
