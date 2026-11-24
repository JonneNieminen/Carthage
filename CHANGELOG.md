# Changelog

What changed in each release of Carthage, written for the people who use it. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org/). The release workflow takes each release's notes from the
matching section of this file.

Add a line under **Unreleased** with every user-visible change. Group lines under Added, Changed,
Fixed or Removed. Write what the user can now do, not what the code does.

## Unreleased

### Added
- Mixer chains: save a channel's plugins with their settings (Save on the mixer's Plugins row, or
  right-click a channel > Save mixer chain) and load them onto any channel (Load, or Load mixer
  chain). Chains are kept in Carthage's Mixer chains folder.
- Copy a channel to another: its level, pan, mute, solo, plugins and routing go to the next
  channel, the next empty one (or a new one), or any channel you pick (Copy to, or right-click >
  Copy channel to). Its name, colour, input and arm stay its own. One undo step.
- Reset routing: a channel plays into master again with its sends removed (Reset beside "Plays
  into", or right-click > Reset routing).
- A Routing section under the mixer's strips, with its own heading, shows where sound goes. By
  default it tells the selected channel's story: a sentence, what plays into it, the channel's
  input, plugins and level, and its way on to master. Settings > Mixer offers four other ways to
  draw it (lines per destination, signal flow, a patch grid you can click to route, a tree from
  master), each with a picture. Hover it to have the routing said in words; a tall one scrolls.
- Zoom the mixer by dragging either end of its scroll bar, as on the main grid.
- Keys name their mode: "D major", "D minor", "D dorian", "D mixolydian", "E phrygian", "F lydian",
  shown whole on the main grid's clips and in the sample editor; a short one-shot shows its note
  ("Note D"). Sounds analysed before are analysed again in the background.
- Zoom the main grid by its scroll bars: drag either end of the bottom bar's thumb to show more
  or less time, or of the side bar's to make lanes taller or shorter. The middle still scrolls.
- The sample editor shows the tempo beside the key, "BPM: 120", or that none was found.
- Drop several samples at once (from the sample browser, Cmd- or Shift-click to pick them, or from
  the Finder/Explorer) and each lands on its own lane, from where you drop down, and plays into
  its own free mixer channel; lanes and channels are added when there are not enough. While you
  drag, each sound's outline shows its lane and the channel it will go to. One undo takes it back.
- Each part of the app says what it is: "Main grid" at the end of the ruler, "Mixer" beside Add
  channel, "Sample editor" beside its close button; the timeline's window and menu item are now
  called Main grid too.
- Settings > Audio shows how to measure latency: an interface with a cable from an output back
  into an input, the arrows showing the way the sound goes; the cable lights while it measures.
- Bus channels: type any interval from 2 to 64 into "Bus every", or pick one of the usual ones.
- Escape anywhere in Settings closes it, back to the lanes or the mixer.
- Project loudness (Settings > Projects): new projects start with master at 0 dB, or at -3, -6,
  -9 or -12 dB if you like to mix with room above; double-clicking the master fader returns there.
  Channels always start at 0 dB.
- Ctrl- or Cmd-click a plugin on a mixer channel's Line to hear only it (every other plugin there
  bypassed); again, and they all play.
- Route a channel into several channels by clicking their route buttons one after another; click
  one again to stop routing there.
- Recording, part two:
  - Arm channels with the R on their mixer strip (or Arm in the mixer's column); R then records
    every armed channel at once, each its own input to its own take. Nothing records unless armed.
  - Each take goes to a lane that is free at that time, or a new lane if none is (FL-style).
  - Loop recording: with the loop on, every pass becomes its own clip; the newest full pass plays
    and the earlier ones are kept, muted.
  - Punch in/out: a Punch button beside Loop records only inside the loop region. Off by default;
    without it recording runs until you stop.
  - Count-in before recording from a stop: off, 1 bar or 2 bars (Settings > Recording); the click
    plays through it, and the take starts where you pressed record.
  - "Guitar in here" in a mixer channel's right-click menu: input 1, heard and armed, in one step.
  - Low-latency monitoring (Settings > Recording, off by default): while you play through an armed
    channel you hear, plugins elsewhere that delay their sound are paused, so what you hear is not
    held back. The Line marks them "(paused)"; exports always use every plugin.
- Export the mix with Cmd+R (or File > Export): the loop region if there is one, otherwise the
  whole project from its first sound to its last, letting reverbs and delays ring out. WAV 16-bit,
  24-bit or 32-bit float, or FLAC 16 or 24-bit, at the interface's rate or another; set in the
  window or under Settings > Recording. Files go to the project's Exports folder unless you choose
  another place; the export runs in the background with a progress bar and Cancel, then Show in
  Finder. The metronome is never in an export.
- Left and Right move between mixer channels; Up and Down make the lane above or below the
  current one current (both shown in Keyboard shortcuts for the lanes; rebindable).
- Add several mixer channels at once: right-click a channel (Add channels after it) or the Add
  channel button, and choose 2, 4, 8 or 16.
- Bus channels (Settings > Mixer, off by default): every 4th, 5th, 8th, 10th, 12th or 16th
  channel is drawn wider and marked "Bus", a place to route a group of channels.
- The plugin browser's manufacturer filter has its own search: type a few letters to narrow the
  list, the arrows and Return choose. Each manufacturer shows how many plugins it has.
- Settings is now a view like the mixer: it takes the place of the lanes (or a window of its own),
  with its pages listed on the left (Audio, Recording, Projects, Timeline, Mixer, Browser, Display,
  Shortcuts, Updates). The search looks through every page and shows how many settings match on
  each. Escape or Cmd+W closes it.
- Cmd+W closes the mixer or Settings when they are shown in place of the lanes, before it closes
  the project.
- Cmd-click a mixer channel's M to mute every other channel, or its S to solo it alone, as on the
  lanes; again undoes it.
- The routing under the mixer's strips is clearer: where the selected channel goes on the upper
  line, what plays into it on the lower one (in the colour of the channel it comes from). Main
  outputs, sends (with their level) and sidechains each look different, and hovering a cable says
  in words what it does.
- Rename a mixer channel from its right-click menu or by double-clicking its name in the column on
  the right; a colour dot beside that name changes the channel's colour.
- In the mixer the wheel adjusts only the selected channel's fader and knobs; over other strips it,
  and a trackpad swipe, scroll the mixer sideways. Cmd+scroll or a pinch makes the strips wider or
  narrower, as on the lanes.
- A Pre-Master channel beside master: everything heading to master passes through it, for the
  processing the whole mix shares. On by default; Settings can turn it off.
- FL-style routing: select a channel and every other strip shows a route button at its foot; click
  to play the selected channel into it (click again: back to master), Option-click to send a copy
  there instead (with a level knob), master's button turns the main output on or off; right-click
  for these and sidechain. A cable under the strips shows
  where the selected channel goes. A route that would loop back is refused.
- Every mixer channel picks its own interface input and can be heard as you play ("Hear the input");
  choose which channel takes record. The input row above the lanes is gone: its plugin is channel
  1's Line, and master's output is chosen at the foot of the master strip.
- Insert Lines: every channel and master take as many plugins as you like, in order. Double-click
  the empty slot to choose one, or drag a plugin from the browser onto a strip or a slot; drag slots
  to reorder them or onto another channel; each has a bypass dot and a wet/dry knob; a click opens
  its window. Pan is a half-ring knob; the fader has a larger cap.
- The mixer: F9, or the new mixer button, shows it in place of the lanes; it can move to its own
  window (Cmd+Option+X). Master on the left, 50 channels, each with fader, pan, mute, solo and a
  peak and RMS meter; add, rename, colour, move and delete channels; send a channel into another
  (a loop back is refused). Right-click a clip > Route to sends every clip of its sample to a
  channel. Lanes are now only where clips are drawn; projects saved before open sounding the same.
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
- App loudness in the toolbar is now the app's own volume: how loud Carthage plays
  through your interface, whatever the project. It no longer moves master's fader and never
  changes an export.
- A mixer channel's column on the right lines its headings up over their fields with
  even spacing; Level has its own heading like the rest.
- Snap is lit in the accent while it is on and plain like the tools while it is off.
- The main grid's ruler is half again as tall.
- Mixer strips say "→ Pre-Master" when a channel's sound passes through the Pre-Master
  on its way to master.
- The mixer's route buttons are much larger (each strip's full width), and a click on them, or
  around them, only routes: it never changes which channel is selected.
- The mixer's column has a Level row: drag it, or click the figure and type a level in dB.
- A mixer channel's "→" line lists every channel it plays into ("→ 1, 2, 10"), and its tooltip
  names them in full.
- The routing cables run all the way into the strips they reach, master's and the Pre-Master's
  too: sound bound for master goes into the Pre-Master and on into master.
- X also shows the mixer, beside F9 (some keyboards keep F9 for themselves).
- On the ruler, a left click or drag moves the playhead; a right-drag sets the loop region.
- The plugin wet/dry knob is now called plugin volume; the toolbar's Master slider is App loudness.
- The mixer and Settings buttons sit beside Snap.
- Carthage opens with the sample browser showing.
- The Settings search finds "recording set count": words like "set" or "change" need not match,
  and a page's or group's name counts.
- A plugin that delays its sound to look ahead (a lookahead limiter, some amp sims) shows by how
  much on its Line, and plays fully wet: its own mix control is the one to use, because the
  mixer's wet/dry would bring its dry sound in early. Everything else is delayed to match, so the
  mix stays in time.
- A mixer fader moves only when you drag its cap or scroll over the selected channel; a click
  anywhere else on a strip just selects it.
- The Settings button is a gear, and its tooltip says Settings.
- "Maker" is now "manufacturer" in the plugin browser.
- Double-clicking the metronome level returns it to 60 %.
- Plugin windows open at the plugin's own size whatever the interface size (they were shrunk with
  it, clipping or cramping the editor). The plugin list shows AU or VST3 beside each name.
- Choosing a channel's input, hearing it and the channel that takes record are undo steps; a wheel
  gesture on a fader or knob is one undo step. "No input" works from the mixer's column and turns
  hearing off; a channel playing only into its sends says so; Cmd+scroll zooms the strips anywhere.
- The Pre-Master switch has its own Mixer section in Settings and shows or hides the strip at once.
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


### Fixed
- Undoing a level change now puts the fader back where you hear it, not only on screen; redo
  works after it too. The same for pan and send levels.
- Choosing a manufacturer in the plugin browser takes one click, even if the pointer moves a
  little while clicking.
- The mixer no longer lags: moving between channels took about a third of a second, and the UI
  stuttered during playback, because every strip asked the audio interface for its input names
  each time it was drawn. They are now read once.
- The sample editor no longer opens empty after it was closed and opened again while playing
  (or with reduced motion on).
- The plugin browser's manufacturer filter could not be opened again after choosing one.
- An export no longer loses the first moment of its first sound when a plugin in the mix looks ahead.

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
