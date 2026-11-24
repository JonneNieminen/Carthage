# Journal

Where development stands, for whoever picks it up next (a new agent session included). Read this
first, then `AGENTS.md`, `docs/ROADMAP.md` (status table and the latest report) and the newest
entries in `docs/DECISIONS.md`. Add a dated entry at the end of every working session: what was
done, what is in hand, what is next. Keep it short; the details live in the commits and the docs.

## Now (2026-11-21)

- **Phases 1 to 4g are verified.** **Phase 5 is mostly built** (W47 to W57): channels and routing,
  insert Lines, sends, buses, Pre-Master, per-channel inputs, export (Cmd+R), delay compensation
  (verified headless), and recording (arming, several takes at once, loop takes, punch, count-in,
  low-latency monitoring). Settings is a view with pages.
- **Next session starts with:** check CI for 1dd4f12 (`gh run list`); then the owner's recording
  checklist with the iD44 (last entry below; the new build needs microphone access allowed);
  fix what they find. Then the remaining exit checks: re-routing while playing, a busy session
  without dropouts (bench), 50 idle channels (bench `idle-mixer`), a whole-project export null;
  then 5e presets.
- Stray file from a test: `build/demo/Demo/Recordings/Take 1 2026-11-20 171237.wav` (undone take).
- **Open:** CI's Windows job on the 4f motion code (Motion_win.cpp) had not finished when the 4f
  report was written; check `gh run list` before the next code push.
- The owner's macOS Zoom gesture was on Cmd + scroll and swallowed Carthage's zoom; they moved it.
- **Rule learnt:** run the full clang-tidy pass (AGENTS.md "Commands") before every push; CI's
  tidy job failed twice on new test files that were not re-checked.
- **In hand, not finished:**
  - The owner's `~/Library/Application Support/Carthage/Samples.xml` points at the generated test
    pack `build/samples-test` (six sounds of known key and tempo) for the 4b checks. Remove that
    folder from the browser (right-click it > Remove from the browser) when the owner is done with
    it, or replace the file only if the owner agrees.
  - CI runs keep being cancelled by the next push (concurrency); check that the newest code commit's
    run went green before pushing again (`gh run list`). Docs-only commits do not run CI.
- **Last release:** v0.1.2-beta.4 (beta feed), self-signed (W35); the owner confirmed updates keep
  the microphone permission. The next release needs the owner's OK for its tag.

## How the owner works

- Owner's rules are in `AGENTS.md` and the brief; the important ones: ask before anything that
  conflicts with a fixed decision, before a new dependency (a library bundled inside a pinned one
  counts: W36 was asked), before a `v*` tag, before touching the system plugin folders; never touch
  the Sparkle or signing private keys; never weaken tests; `main` stays green; small commits pushed
  straight to `main`; docs true in the same commit.
- Report after each phase in the ROADMAP format, then wait for the go-ahead.
- The owner likes to try things hands-on: give a short numbered checklist with what should happen,
  launch the dev build for them, and keep each step to one action. They answer "all fine" or name
  what is wrong; their requests become decisions (W-entries) when they set a rule.
- UI work goes through the frontend-design plugin, dark theme tokens only.

## This machine

- Build, run and measure: `AGENTS.md` "Commands". Dev app with frame log:
  `open --env CARTHAGE_FRAME_LOG=1 --stderr build/demo/frames.log build/release/src/app/Carthage_artefacts/Release/Carthage.app --args "$PWD/build/demo/Demo 300/Demo 300.carthage"`
  (regenerate the demo with `carthage-bench demo --out "build/demo/Demo 300" --clips 300`).
- The owner has three monitors (built-in, XV273K at 120 Hz, VG248); the app window may open on any.
  Computer use: background `app_*` clicks do not reach JUCE components (only menus, accessibility
  presses such as buttons, and screenshots work); full-screen clicks land unreliably across the
  monitors; the tool refuses Copy/Paste menu items and Cmd+C/V. Prefer `carthage-snapshot` (renders
  the real window offscreen; `--samples FOLDER`, `--project FILE`) and the bench for checks, and the
  owner's hands for clicks.
- An Audient iD44 is connected (inputs "Analogue 1…"); RTSan soak:
  `RTSAN_OPTIONS=halt_on_error=false build/rtsan/tools/bench/carthage_bench_artefacts/RelWithDebInfo/carthage_bench soak --device "Audient iD44" --rate 48000 --buffer 128 --input 1 --plugin-format VST3 --plugin /Library/Audio/Plug-Ins/VST3/NeuralAmpModeler.vst3 --seconds 60 [--preview FILE] > rtsan.log 2>&1`
  then `python3 -I scripts/rtsan_check.py rtsan.log tests/rtsan/baseline.txt`.
- Generated test material (not committed): `build/samples-test` (known keys and tempos),
  `build/samples-big` (2,976 sounds), `build/demo/Demo 300`.

## Log

- **2026-11-18.** Phase 4a checked in the app (computer use and the owner); fixes: shortcut editor
  focus and search scroll, solid clip bodies, disk access while painting during playback. Mac
  window keys and Cmd+W (W34). Self-signed releases (W35); v0.1.2-beta.3 and beta.4 confirmed the
  microphone permission sticks. Phase 4a verified. Phase 4b built: tabs, copy/paste, collect
  samples, sample index/preview/analysis/browser, Signalsmith (W36), start screen and templates.
- **2026-11-19.** Owner checked 4b in the app: all fine; clicks now retrigger samples. Value
  controls reset on double-click and follow the wheel (W37, a rule in AGENTS.md). Phase 4b verified
  and reported. This journal started.
- **2026-11-19 (later).** Owner asked for detachable views and window layouts across displays;
  decided as F21, phase 4c, now (before the mixer), app-wide layouts, plus reopen last session,
  command palette and plugin windows on top. Plan written into ROADMAP 4c.
- **2026-11-19 (4c, in progress).** Built so far: `MainComponent::View` and `ViewWindow` (each view
  docks or lives in its own window; pop-out buttons; closing a window brings the view back), layouts
  (`getLayout`/`applyLayout`, slots in the engine's properties as `layout1`..`layout9`, the count as
  `layoutSlots`), the Layouts menu, commands (Cmd+Opt+S/P/T/I/, for views, Cmd+Opt+0 default,
  Cmd+Opt+1-9 recall, Cmd+Opt+Shift+1-9 save, Cmd+K palette, keep plugin windows on top), Settings >
  Windows > Layout slots. Not yet: the command palette itself (`onCommandPalette` is unset), reopen
  last session, tests and snapshots for 4c, docs (ARCHITECTURE, CHANGELOG), the owner's checks.
- **2026-11-19 (4c, part 2).** Command palette (Cmd+K, toggles), reopen last session (`lastSession`
  property; Settings > Projects > When it opens), docs. All 4c items built; next: the owner's checks
  (detach each view by button and shortcut onto a second display, save/recall two layouts, unplug a
  display, quit and relaunch with "Reopen the last session", the palette), then the 4c report. Seen
  working already through menus: Layouts menu, detach timeline and plugins, save layout 1, default
  layout; recall was cut off by the screen locking.
- **2026-11-19 (4c verified).** Owner: "4c checks pass with flying colors". Report written (frame
  hitches up to ~0.1 s only when native windows open or move). Waiting for the owner's feature list.
- **2026-11-19 (feature list).** The owner sent a long list (UI scale, layout renaming, Cmd+W for view
  windows, copyable tooltips, plugin scan at first launch only, lane names, toolbar icons, export
  logs, tap tempo, tempo-change question, a UI refinement pass, rename projects from tabs, swing,
  FL-style paste and right-click delete, resizable panels, easier lane add/delete/mute, wheel zoom
  and scroll on both axes, metronome, a sample editor with shared edits and Make unique, fit one
  sample to tempo, presets in the plugin browser, master volume slider, a local AI helper, CPU bar
  hidden by default, plugins in their own processes with automatic grouping, dependabot, efficiency
  ideas). Questions are on https://claude.ai/artifact/ADos2N5cAn7KMqsGRGALiq ; answers are in its
  database, collection `answers` (read with ArtifactData list). Next: read the answers, write the
  decisions (F22+) and the roadmap placement, then build.
- **2026-11-19 (answers).** All recommended answers chosen, plus notes (logs on Windows too; master
  volume takes a typed value; the sample editor's waveform plays on click). Recorded as F22, W38;
  ROADMAP 4d, 4e, 4f and additions to 5, 6 and after 7. Owner asked for C++ best practices fitting
  this project: AGENTS.md "C++ practices", W39, enforced by clang-tidy, sanitizers and RTSan in CI (4d).

### 2026-11-19 (phase 4d, part 1)

- clang-tidy: fixed to zero findings across src/tools/tests (.cpp; .mm is left out because the
  tool misreads Objective-C). ASan found a real use-after-free in `editing::remove`, now fixed.
  Tracktion keeps mute and solo out of undo, so ours do the same.
- Toolbar, lanes, tab rename and tap tempo committed, with tests (EditingTests lanes,
  TapTempo) and a bench check (`carthage-bench projects`: rename). Renaming moves the old folder to
  the bin, and the bench run puts two temp folders there too.
- Not yet checked by the owner in the app.

### 2026-11-19 (phase 4d, part 2)

- Built: the tempo question (editing::setTempo/followTempo: Tracktion's setBpm remaps the edit,
  Carthage sets it without the remap and places clips itself), lane height, panel edge, UI size
  with a folding toolbar, Cmd+W on view windows, layout names, copyable tooltips, FL-style
  switches, first-launch plugin check, metronome (synthesized sounds written as WAVs for
  Tracktion's click track), log + Export log + crash report, undo history, notes (in the edit
  state), versions (Versions/ folder, restore in place), overviews on disk, signposts, fuzzing.
- Fuzzing found JUCE's CoreAudio reader spinning on a damaged CAF; Carthage now registers only its
  five formats (W40). Known gap: Tracktion's clip reader still has CoreAudio (not patched).
- Tracktion facts found: mute/solo are outside undo; Click::setClickWaveFile is internal (set
  SettingID::clickTrackSampleBig/Small); a freshly loaded edit's changed flag clears ~200 ms later.

### 2026-11-19 (phase 4d, the owner's checks)

- Checks page https://claude.ai/artifact/R5jmicCHEmADsRHY994jm3 (db `results`): 20 work. Interface
  size failed: Cmd++ did not enlarge, because + is its own key on the owner's keyboard; fixed by
  binding '+' as well as '='. Undo history "didn't do anything": not reproduced yet; the questions
  page asks how it was opened. The wheel check is skipped until the owner has a mouse: remind them.
- The owner's next list (lanes add-many, Cmd-click M mutes others, lane move by drag and keys,
  per-lane height, header/grid border drag, settings search, views palette, beat shading, a
  softer metronome, free slice cut with indicator, tab menu Notes, Cmd+1-9 tabs, settings
  benchmark at first launch, version pictures). Questions page
  https://claude.ai/artifact/FbCnpQEk9EXD482AfH7Xcd (db `answers`, with metronome auditions).
  Done already: the tab menu's Rename text, Notes in the tab menu, Option/Alt wording by platform.

### 2026-11-19 (phase 4d, the second list: W41)

- Built and pushed, all items of W41: lanes (add 1/2/4/8, Option+Cmd+N, Cmd-click M/S, move by
  drag and Cmd+Up/Down, per-lane height, name column border), free slice with a line, beat
  shading, tick-tock metronome (accent on in new projects), Settings search, views palette
  (Cmd+J), Cmd+1-9 tabs, settings test (first launch + Settings; bench `settings-test`),
  version pictures with a Restore window. Fixes: Cmd++ (keyboards with + on its own key), undo
  history and Restore opened from a key (menu key equivalents closed the popup), Settings
  headings for Timeline/Metronome never showed.
- Round 2 checks are on the same checks page (ids r2-*, plus `wheel`, still waiting for a mouse).
  Next: read them, fix what fails, then write the 4d report and wait for the go-ahead (4e next).
- Rule kept: commit only when the full clang-tidy pass is clean (one push slipped through;
  fixed in the next commit).

### 2026-11-19 (phase 4d report written)

- Round 2: all 17 work. Fixed after: Cmd+W in notes/versions windows, Cmd+Backspace deletes.
- 4d report in ROADMAP. Open: the wheel check (owner needs a mouse), a longer frame hitch when
  playback starts (one run: 4 s, 46 late frames, worst 50 ms), CI run 44565369503 was queued.
- Next after the owner's go-ahead: phase 4e, the sample editor.

### 2026-11-19 (phase 4e in progress)

- Owner: 4d closed, wheel check moved to 4f; go-ahead for 4e. W42 (editor answers), W43 (loop
  region = 4g and phase 6; Cmd+R export moved to phase 5).
- 4e done so far: sample model (SampleEdits: sample per file until Make unique, shared edits,
  tested), the editor panel (double-click a clip; Escape/Cmd+W/clip again close; waveform click
  plays from there; gain, pitch, fades, reverse, start/end, Fit to tempo, Make unique), key and
  tempo on clips (memory cache, never the disk while painting), Option-drop fit, clip right-click
  menu.
- 4e left: the volume envelope. Plan: a VolumeAndPan plugin in the clip's own plugin list (live,
  works with Reverse; Tracktion's ClipEffects are disabled on reversed clips), its volume
  automated; first check with an offline render whether a clip plugin's curve runs in clip or
  timeline time. Then the owner's 4e checks and report.
- Found: Tracktion's Renderer::renderToFile hangs in the headless unit-test binary (no message
  loop to finish on). Do the envelope's time-base check in carthage-bench, which has one.
- Also tried in the bench (on the message thread, useThread true): renderToFile returned false.
  Next session: read tracktion_Renderer.cpp for what renderToFile needs (a free playback context?
  the track mask's indexing?) before building the envelope on clip-plugin automation; or check
  the time base without rendering, by reading the curve the ClipNode is given.

### 2026-11-19 (phase 4e built)

- Owner liked the editor and asked for fade shapes and an FL-style drawn shape: both built.
  Fade shapes are Tracktion's four curves. The envelope is points in the clip's own state,
  played by a marked VolumeAndPan plugin on the clip (PanLaw6dBCenter = unity), its curve in
  timeline time, re-synced by the session after every change (SampleEdits::syncEnvelope).
  Proven by `carthage-bench clip-envelope` (renders off the message thread; Tracktion's
  useThread=true needs UI hooks Carthage leaves empty).
- 4e checks are on the checks page (ids e-*). Next: read them, fix, write the 4e report; then
  4f (design pass, plus the wheel check and the playback-start hitch carried from 4d), 4g (loop
  region).
- Not yet run: an RTSan soak with clip envelopes playing (the soak records; add a playback case).

### 2026-11-19 (4e, the owner's additions)

- Built: rename a sample; "Key: …" apart from the clip count; bend drag direction flipped,
  double-click straightens, Ctrl halves drag speed; waveforms scale with gain (editor and clips);
  preview volume figure; formant shift (W44: FormantPlugin on the clip, Signalsmith direct,
  latency compensated; bench `clip-formant`; RTSan-clean test); lane colours (Colour submenu,
  kept in the lane); Cmd+M minimises the front window. Editor layout tidy is listed for 4f.
- Checks page has the 4e items (e-*), including these. Next: read results, fix, 4e report.
- Owner: 4e a pass; the rising-line bend fixed (e9648c2). 4e report written. Next: 4f after the
  owner confirms the bend and gives the go-ahead.

### 2026-11-19 (phase 4f, design pass)

- Owner chose on the typeface page: Barlow, Barlow Semi Condensed for dense rows, IBM Plex Mono
  for figures; the proposed sample editor layout; motion "answers" (W45).
- Built: fonts compiled in (`juce_add_binary_data`), OFL texts in the bundle; tokens, theme guide,
  README and the repo art follow. Sample editor: Play + shape tools above the waveform, Reverse /
  Fit to tempo / Make unique below, Sound and Fades on the right, figures on one edge.
- Motion: `ui/Motion.h` (Host, allowedFor, Tween on JUCE's juce_animation, display-synced),
  `ui/platform/Motion_mac.mm` / `Motion_win.cpp` read the system's reduce-motion setting.
  `ui::Switch` replaces every `juce::ToggleButton`. MotionTests cover the rules.
- The owner let me quit the older Carthage; in the new build the editor opens in place and the
  Reverse switch lands right both ways. 4f checks (f-*) are on the checks page.
- Owner asked for reversed samples to look reversed: the editor and the lanes draw them mirrored
  (Tracktion's offset already counts from the file's end), shape points mirror with them, clicks
  play the clip's reversed file. Checked in the app on the demo; undo restores both.
- Owner's 4f checks: all pass but the wheel (Shift + scroll did nothing on the lanes: macOS sends
  a Shift wheel turn as deltaX, the lanes read deltaY; fixed, `Timeline::wheelScroll` tested), the
  time scrollbar's track under the lane names (corner painted), and the fade line crossing into the
  controls (the waveform's drawing is clipped to it). Sliders already took Shift (checked by test).
- Owner retested: all pass. 4f report written. Next: the go-ahead for 4g.

### 2026-11-20 (phase 4g, loop region)

- The region lives on the edit with undo (W46), the transport follows (`editing::syncLoop`, run by
  the session after every edit). Ruler: click moves the playhead, drag draws, edges drag, double-
  click clears; L and a loop button toggle looping. The piano-roll part waits for phase 6.
- The background drag tool delivers its press late (partway along the path), so ruler drags looked
  wrong with it; a real press (full-screen control, owner present) placed the region exactly.
- Open for the owner: a modifier for drawing the region on the lanes (all are taken there).
- CI caught a flaky wheel test (two events in one clock tick; JUCE's slider drops the second as a
  duplicate). With its own time per event the test showed a real bug: JUCE reads a sideways wheel
  as -deltaX, so Shift + wheel on a Mac moved sliders backwards. ValueSlider takes it as the wheel
  turn it was; the test fails without that.
- Owner: all 4g checks pass.

### 2026-11-20 (phase 5a, footing)

- Owner: mixer replaces the grid via a toolbar button and F9, pops out like other views (W48).
- `carthage-bench overlap`: two identical clips at the same time on one track both play (RMS
  0.1000 → 0.2001), so lanes can share channel tracks.
- `carthage-bench idle-mixer` (built-in audio, 128 samples): 50 empty tracks add 2.66 points of
  audio-thread load; with `Track::setProcessing (false)` on them, -0.01. 5a's rule: a channel with
  nothing that could sound through it (no clips, input, plugin or incoming route) is left out.
- Built the 5a engine (W49): `engine/Mixer.h` (lanes in CARTHAGELANES, channels, carriers,
  per-sample routing, feedback refused, delete channel to master, syncMix, prepareMix migration).
  Timeline, tests, bench and snapshot moved onto it; MixerTests added. Found and handled:
  Tracktion's track and plugin "process" flags write through undo, and its reaction to a track's
  flag reads the old value (setProcessing in Mixer.cpp). `project::createEdit` now prepares the mix.
  Bench renders (overlap, carrier, clip-envelope, clip-formant) pass on the new model.
  Then channel mute/solo and meters (W50), and the mixer view: MixerView (strips, master pinned,
  column with "Plays into" and "Sound from"), F9 / mixer button / Cmd+Option+X window, clip menu
  "Route to" (master, a new channel, any channel). Snapshot `mixer.png`. Next: the owner's 5a
  checks; then routing while playing (exit check) measured with the bench.

### 2026-11-20 (owner's mixer list, W51)

- Plugin windows "flash and vanish": not reproduced in the bench (fresh or pre-5a project, Pro-C 2
  instance and editor stay 3 s; `carthage-bench plugin-window [--old-project]`). The old window path
  (signal row) is being replaced; the new `ui/PluginWindows` opens one window per plugin, holds the
  plugin, never forces a size, and closes windows whose plugin left its Line. Checked through the
  real mixer code: `CARTHAGE_SNAPSHOT_PLUGIN=<vst3> carthage_snapshot` loads it onto a Line, opens
  it, and finds window and instance alive after 3 s. Owner to confirm in the app.
- Insert Lines built (engine: pluginListOf, placeInsert, moveInsert, bypass, mix; UI: InsertLine in
  the mixer column, PluginChooser call-out, strip and slot drops). Half-ring knobs and fader caps in
  the LookAndFeel. Next: per-channel input and monitor, signal row removed, master output.
- Per-channel input, monitoring and record channel (Mixer.h ChannelInput; Session setChannelInput,
  setChannelMonitoring, setRecordChannel; applyInputRouting connects inputs to every monitoring
  channel). Signal row removed (its command id `windowSignal` kept, inactive, so saved shortcuts
  keep their ids; old layouts naming it are skipped). Master output at the master strip's foot and in
  its column. Next: FL-style routing (sends, sidechain, cables), then Pre-Master, then scroll/zoom.
- Routing (W51): sends via Tracktion aux send/return (a return per destination, first on its
  channel; sends after the fader, before a "main output off" gate). Tracktion's output "(none)"
  still reached master in a render, so main output off is a -100 dB volume stage at the end
  (`setPlaysToOutput`); `faderOf` replaces Tracktion's getVolumePlugin, which returns the last
  volume stage. Bench `carrier` renders a send (2x) and send-only (1x). Sidechain through
  Tracktion's sidechain source. UI: route buttons, send knobs, cable band, route menu, slot menu.
- Pre-Master (W51): a track with role "premaster", made by prepareMix for every project; routes
  name master (null) and physically go to it while on (`physicalMaster`, `logicalDestination`);
  the setting `usePreMaster` (default on) rewires all. Bench renders now take the whole mix (a
  lane's carrier plays through the Pre-Master, which a one-track render left out).
- Wheel and zoom: ValueSlider::acceptsWheel gates the mixer's controls to the selected strip,
  passing the wheel to the parent (the viewport scrolls); Cmd+wheel and pinch zoom strip widths
  (0.75 to 2.5, kept in the settings).

### 2026-11-20 (the owner's mixer list, tested by computer use)

- A side session (mixer-checks) drove the app: plugin windows now stay open (the owner's bug is
  gone); drag and drop, sends, Cmd-click, loop refusal, sidechain, wheel scrolling and playback
  work. Found and fixed here: plugin windows inherited Carthage's 77.5 % interface scale (native
  editors clipped/cramped; windows now report getDesktopScaleFactor 1, as JUCE's plugin host does);
  the Pre-Master switch did not rebuild the mixer (and sat under Browser); inputs and the record
  channel were outside undo (now named steps; the session re-applies routing after an undo when
  `inputSignature` changes); one wheel tick per undo step (ValueSlider::onWheelStart); the column's
  "No input" decoded as input 1 stereo; main output off still read "Master"; no cable to master;
  Cmd+scroll over a fader moved it; the chooser did not say AU or VST3. Not fixed yet: the first
  click back in the main window after a plugin window had focus is ignored (macOS window focus).

### 2026-11-20 (owner's requests: mixer Cmd-click, cables, Settings view, W52)

- Cmd-click on a strip's M/S as on the lanes (`editing::muteOtherChannels`, `soloChannelAlone`,
  tested in MixerTests). Rename from the strip menu or the column title (double-click); a colour
  dot beside the column title opens the colour menu.
- Cable band (58 px) shows the selected channel both ways: out on the upper line in the accent,
  in on the lower line in the sender's colour; main heavy, send with its level, sidechain dashed;
  hover lifts a cable and its tooltip says what it does. `carthage-snapshot` now writes
  `mixer-in.png` (the Drum bus selected) beside `mixer.png`.
- Settings is a view in place of the lanes (or its own window, 920x660): pages on the left,
  search across pages with counts, Escape clears the search then closes; the page shown is kept
  in the settings (`settingsPage`). Cmd+W in the main window closes Settings, then the mixer,
  before the project.
- Checked in the app by computer use: Cmd-click M/S (on and back), cable tooltip, Cmd+W from the
  mixer and from Settings, page list clicks and arrow keys, Cmd+, opens Settings. Not confirmed:
  the toolbar's Settings button; full-screen clicks stopped reaching the toolbar after the window
  moved (the mixer button failed the same way), so it is untested, not known broken.

### 2026-11-20 (gear, manufacturer search, bus channels, W53)

- Settings glyph is a spoked gear; its tooltip starts "Open or close Settings". Metronome level
  double-click returns to 60 %. "Maker" renamed "manufacturer"; the filter (a ComboBox whose
  showPopup opens a call-out) has a search field, counts, arrows and Return.
- Bus channels: Settings > Mixer, off by default; every Nth channel 1.5x wide with a "Bus" tag.
  `carthage-snapshot` writes `mixer-buses.png` (every 4th).
- Checked in the app: the gear opens Settings (the earlier failed clicks were the tool), the
  manufacturer search ("fab" + Return filters to FabFilter's 28; arrows back to Any manufacturer).
  Bus channels checked by snapshot only. Each new ad-hoc build asks for microphone access again.

### 2026-11-20 (mixer clicks and keys, W54)

- Route button: click = main output here / back to master; Option-click = send. Fader: cap only;
  wheel over the selected strip anywhere. Left/Right between strips (MixerView has keyboard
  focus after a click on a strip); Up/Down commands previousLane/nextLane (ids after afterTabs).
  Add 1/2/4/8/16 channels from the strip menu or a right-click on Add channel.
- CI run for a683709 ended "failure" only because GitHub had no macOS arm64 runner (jobs
  cancelled: "not acquired by Runner"); every job that ran passed. The next push re-runs all.

### 2026-11-20 (export, Cmd+R, W55)

- `engine/Export` (rangeOf, parametersFor, render, start/Job), `ui/ExportWindow` (Cmd+R, File >
  Export, Cmd+W closes it), Settings > Recording > Export, `project::exportsIn` copied by Save as.
- ExportTests: range (empty, whole project, loop region), every format at 44.1/96 kHz read back,
  and the exit check's null: loop region played through a hosted device vs the export, -200 dB,
  offset 0. Found: Tracktion's `renderToFile (desc, params)` needs a non-empty track set and a UI
  behaviour that runs the task (headless engines do nothing), so `exporting::render` runs the task.
- Snapshot `export.png` (the window). Not tried on screen: the Mac was locked. Next: the owner
  tries Cmd+R; then a whole-project null and delay compensation (5b).

### 2026-11-20 (plugin delay compensation, W56)

- DelayCompensationTests: Tracktion's LatencyPlugin (tests only) on channel, send destination, bus,
  Pre-Master, master; exports null against the mix without it, from 0 s and 0.5 s; playback nulls
  against the export (whole mix 480 samples late, as expected; the first 2 ms after Play are
  playback's de-click fade and are skipped).
- Found: Tracktion's render fades in 32 samples when anything reports latency (export now renders
  a 10 ms lead-in as float and writes the format without it); Tracktion's wet/dry is not latency
  compensated (latent plugins: no knob, fully wet, delay shown on the Line).
- Next: low-latency monitoring mode with 5d (ask the owner what it bypasses); a whole-project null.
- CI on Windows failed for a04d9fd: `JUCE_MAC` used as a value in ExportWindow (only defined on
  macOS). Fixed with an `#if`-chosen constant. Rule: platform macros only in `#if`.

### 2026-11-21 (recording, 5d, W57)

- Arming (Mixer `isArmed`/`setArmed`, R pills, column switch), several recorders (AudioCallback
  `setTakeInputs`, Session `recorders`), `engine/TakePlan` (count-in cut, loop passes, punch),
  `freeLaneFor`, Punch button and command, count-in and low-latency settings, "Guitar in here".
- Tests: TakePlanTests, arming and free-lane cases in MixerTests, and a frozen plugin out of
  playback but in the export (DelayCompensationTests). Bench `takes` arms the record channel.
- Not tried with an interface yet: the new build waits for macOS microphone access. Next: the
  owner's checklist with the iD44 (two armed channels, loop takes, punch, count-in).

### 2026-11-21 (owner's list after trying recording, W58)

- CI green for 1dd4f12. Owner's findings fixed: see W58 (ruler, loudness, plugin solo, plugin
  volume name, multi-route clicks, toolbar, samples at start, settings search, manufacturer box).
- F9: works when sent to Carthage (checked by computer use); on the owner's keyboard it does not
  arrive (fn or not). Likely macOS or Wootility takes F9. Asked the owner; a second default key is
  an option.
- Not tried in the app: the owner's Carthage was running the previous build with their takes.
- Then W59: cables in a band over the whole mixer (into master and the Pre-Master), the strip's
  "→" lists every destination, X shows the mixer too. Checked by snapshot.
- Route buttons: full strip width, 34 px tall, a send knob row above (both always kept); the
  foot of a strip routes and never selects while another channel is selected. Level row (typed
  dB) in the column. Owner asked for 5 design options for a Routing section next.

### 2026-11-21 (owner's list while waiting for CI, W60)

- Pushed 7f3cabf (route buttons, typed level) once CI for 639ea8e was green.
- Column: stacked sections, 8 px heading-to-field, 24 px between sections; "Sound from" as tall
  as its lines. Snap in the accent when on. Escape closes Settings (focus moves into it on open).
- Loopback drawing in Settings > Audio (`SettingsPanel::LoopbackPicture`); typed bus interval.
- W60: App loudness as an output gain in AudioCallback (OutputGainTests); several dropped sounds
  each on its own lane and free channel (EditingTests); view names; ruler 42 px; "→ Pre-Master".
- Roadmap: more colours and a Carthage mark on each project tab (phase 7); phase 8 performance
  optimization (the owner hears serious dropouts at the recommended settings); phase 9 refine
  animations.
- Main grid scroll bars zoom when their ends are dragged (`ZoomScrollBar`); the sample editor shows
  "BPM: 120" (or "none found") beside the key.
- Fixed: the sample editor reopened see-through (a finished fade-out leaves alpha 0 and the rise
  was skipped while playing); `motion::showAgain`, MotionTests.
- Keys with modes: KK still picks tonic and third; then the 4th/7th (major third) or 2nd/6th (minor
  third) decide lydian/mixolydian or phrygian/dorian when one leads by 1.8x and is above half the
  mean (tuned on SampleTests' sounds: the E minor bass has overtone energy on F at 1.6x). Stored
  analyses carry `method` 2; older ones are redone.

### 2026-11-21 (Routing section, the mixer lag, W61 W62)

- The owner chose D from the five options (design/routing-options.html, published as an artifact)
  and asked for the rest in Settings with pictures. `ui::routing` (RoutingView.h/.cpp): one model,
  five painters, `Section` under the strips (replaces CableBand), `Preview` cards in Settings >
  Mixer. Snapshots: mixer-routing-*.png, settings-mixer.png.
- Lag (W62): 300 ms per channel change, found with the snapshot tool's timing and `sample`;
  `inputName` asked CoreAudio per call. Cached: 0.4 ms. AGENTS.md rule; the snapshot prints SLOW.
- Mixer zoom by its scroll bar's ends (`ZoomViewport`); the rule is now in AGENTS.md for every
  scrolling view (piano roll listed in phase 6). Meters at 30 fps noted in phase 9.
- Asked for, not started: per-channel reset routing (column and right-click), the plugin
  browser's manufacturer list sometimes needing several clicks, mixer chain presets (load button
  and right-click; save), copy a channel's whole state to the next, next empty or a chosen channel.
- Not yet looked into: the stuck lane and dropout after switching windows (asked the owner for
  details). The owner's general dropouts are phase 8.

### 2026-11-24 (mixer chains, reset routing, undo of levels, W63)

- Chains: save/load (`.carthagechain`, `Paths::mixerChains`), copy a channel (next, next empty or
  new, any), reset routing; column buttons (Reset; Load, Save, Copy to) and right-click items.
- Found while testing copy's undo: Tracktion leaves a fader's parameter behind on undo (the old
  "volume undo" retest item). `editing::undo`/`redo` now resync it; MixerTests covers undo and redo.
- Manufacturer list: rows are components that choose on mouse-up (not tried in the app yet).
- Not done: 5e's other presets; the stuck lane after switching windows (waiting on the owner);
  meters at display rate (phase 9); dropouts (phase 8).

### 2026-11-24 (phase 5 exit checks, one plugin's presets, W64)

- The owner asked to keep developing; testing comes later, all together.
- Exit checks: the whole-project export nulls too (Tracktion fades a start-on-sound in over ~40
  samples; the comparison skips 128); a clip re-routed while playing keeps sounding (no quiet
  block); feedback refusal ticked from MixerTests; idle mixer measured on the iD44 (0.89%).
- 5e: `engine/Presets` (W64), the Line's Presets menu, the browser's right-click "Add with a
  preset", `Paths::presets`. Tested with Tracktion's reverb as a stand-in.
- Real plugins (bench `presets`, no preset files ship on this Mac, so the plugin writes them): Pro-C 2
  VST3 and AU, Apple AUDelay: all pass. Found on the way: set a parameter through JUCE behind
  Tracktion's back and an AU's saved state never holds it; set through Tracktion it does. An AU's
  whole-state load leaves Tracktion's parameter view stale (the unit is right; W64).
- Next: 5f (plugins in their own processes) and 5g.
- Still asked for, not started: 5 Routing section designs, then UAT. The owner's dropouts are
  phase 8's first task; not looked into yet.

