# Journal

Where development stands, for whoever picks it up next (a new agent session included). Read this
first, then `AGENTS.md`, `docs/ROADMAP.md` (status table and the latest report) and the newest
entries in `docs/DECISIONS.md`. Add a dated entry at the end of every working session: what was
done, what is in hand, what is next. Keep it short; the details live in the commits and the docs.

## Now (2026-11-19)

- **Phases 1 to 4b are verified** (4b report in ROADMAP). **In progress: phase 4c** (F21): views in
  their own windows, layout slots, a Layouts menu, reopen last session, command palette, plugin
  windows on top. Then phase 5 (mixer), after the owner's go-ahead.
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
