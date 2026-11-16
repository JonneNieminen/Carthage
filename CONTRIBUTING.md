# Contributing to Carthage

Carthage is a personal, open-source project in its first days. There is no application to build yet;
the repository holds the brand, the theme and the plan. This file grows with the code.

## Before you start

- Read [docs/BRIEF.md](docs/BRIEF.md) for what Carthage is and [docs/DECISIONS.md](docs/DECISIONS.md)
  for what is already settled. Fixed decisions are not up for discussion in a patch; open an issue
  if you think one is wrong.
- [docs/ROADMAP.md](docs/ROADMAP.md) shows which phase is in progress. Work outside the current phase is
  unlikely to be merged yet.
- [AGENTS.md](AGENTS.md) holds the working rules. They apply to people and to coding agents alike.

## Setup

Build instructions arrive with phase 1 (CMake + Ninja, JUCE and Tracktion Engine as git submodules).
Until then, the only thing to run is the artwork:

```bash
design/banners/render.sh
```

It needs Google Chrome and writes the banners and cards to `docs/art`.

## Guidelines

- **The audio thread is sacred.** No allocation, locks, I/O or logging on it. See
  [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
- **No built-in plugins.** Carthage hosts third-party AU and VST3 plugins and ships none.
- **Design tokens only.** Colours, type and radii come from [design/tokens.css](design/tokens.css) and
  its mirror in the app. No colour literals in components. See [design/README.md](design/README.md).
- **Everything is undoable.** An edit that cannot be undone is a bug.
- **Shortcuts live in one table.** Menus, tooltips, the shortcut editor and the help page read it.
- **Verify by running.** Say how you tested, with which plugins, and include the numbers where the
  change affects latency, CPU or frame time. See [docs/TESTING.md](docs/TESTING.md).
- **Keep the docs true.** The table at the end of `AGENTS.md` says which file follows which change.

## Reporting bugs

Use the bug report template. Include your machine, OS version, audio interface, buffer size and sample
rate, and the plugin (name, vendor, format, version) if one is involved. Please don't attach
copyrighted audio or commercial plugin files; describe them instead.

## Licence

Carthage is licensed under the GNU Affero General Public License v3.0 or later ([LICENSE](LICENSE)),
because JUCE is open source only under the AGPLv3 ([docs/DECISIONS.md](docs/DECISIONS.md), F16). By
contributing you agree that your contribution is distributed under the same licence.
