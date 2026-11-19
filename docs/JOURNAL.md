# Journal

Where development stands, for whoever picks it up next (a new agent session included). Read this
first, then `AGENTS.md`, `docs/ROADMAP.md` (status table and the latest report) and the newest
entries in `docs/DECISIONS.md`. Add a dated entry at the end of every working session: what was
done, what is in hand, what is next. Keep it short; the details live in the commits and the docs.

## Now (2026-11-19)

- **Phases 1 to 4c are verified** (reports in ROADMAP). **Phase 4d is in progress** (the owner said
  "Start phase 4d"); then 4e (sample editor), 4f (design pass), phase 5 (mixer). The owner's list is
  F22, the answers W38, the C++ rules W39 (AGENTS.md "C++ practices").
- **4d done so far:** clang-tidy with zero findings and CI jobs for clang-tidy, ASan/UBSan
  (`asan` preset) and RTSan tests; dependabot for Actions; toolbar tool icons with keys, master
  level, load meter hidden by default (Settings > Load); lanes rename/add/delete/mute/solo; rename a
  project from its tab; tempo figure with tap tempo.
- **4d still to do, in this order:** the tempo-change question (stretch / move with the beat / leave
  in time, with Remember); wheel scroll and zoom on both axes plus lane height (W38 keys); panels
  resizable by their border; UI size 75-150 % (Cmd+= / - / 0); Cmd+W on a view window; rename
  layouts; copyable tooltips; FL-style paste and right-click delete switches; plugin check at first
  launch only; metronome; export logs, crash report, project versions, undo list, project notes;
  fuzzing the readers, signposts, waveform overviews cached on disk; then the owner's checks and the
  4d report. The toolbar is full at 1280 pt (min window 960): the UI-size work should make it fold.
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

