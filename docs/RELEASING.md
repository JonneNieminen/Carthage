# Releasing and updates

The plan for how Carthage is versioned, built, signed, published and updated. **None of this exists
yet**; phase 2 builds it. The requirements are in [BRIEF.md](BRIEF.md) ("Updates and releases") and
summarised in [DECISIONS.md](DECISIONS.md) F10–F11. This file turns them into a checklist and becomes
the release manual once the pipeline works. Rewrite the parts marked *to confirm* with what was
actually built and tested.

The model to follow is Prism's release workflow (`../Prism/.github/workflows/release.yml` on the
owner's machine, `JonneNieminen/Prism` on GitHub), adapted from Tauri's updater to Sparkle.

## Versions and tags

- The version is the tag. `v1.2.3` is a stable release; a suffixed tag such as `v1.2.3-beta.1` is a
  pre-release.
- The workflow injects the version into CMake, `Info.plist` (short version and build number) and the
  Windows resources. Nothing in the source tree hard-codes a version that a release has to remember to
  bump.
- Sparkle compares build numbers, so the build number must increase with every release, including
  betas. Decide the scheme in phase 2 and record it here. *To confirm.*
- Release notes come from [CHANGELOG.md](../CHANGELOG.md): the section for the tagged version.

## What a release produces

| Asset | From |
| --- | --- |
| `Carthage-X.Y.Z-arm64.dmg` | macOS arm64 build |
| `Carthage-X.Y.Z-x64-setup.exe` | Windows x64 build, Inno Setup |
| `appcast.xml` | Stable feed, EdDSA-signed entries. Updated only by stable tags |
| Beta appcast | Separate feed, published alongside. Updated by pre-release tags (and by stable tags, so beta users also get stable releases; *to confirm*) |

File names are a proposal; fix them in phase 2 and keep them stable, because the feed URL depends on
GitHub's `releases/latest/download/` path:

```
https://github.com/JonneNieminen/Carthage/releases/latest/download/appcast.xml
```

GitHub's `latest` never points at a pre-release, which is what keeps beta builds off the stable feed.
The beta feed therefore needs a URL that does not depend on `latest`. Work out where it lives and
record it here. *To confirm.*

## Workflows

- **`ci.yml`** — every push to `main`: configure, build and test on macOS arm64 and Windows x64.
- **`release.yml`** — on a `v*` tag:
  1. Build macOS arm64, sign (ad-hoc for now), package the `.dmg`.
  2. Build Windows x64, package the Inno Setup installer.
  3. Sign both downloads with the EdDSA key and generate the appcast(s) with the release notes.
  4. Publish everything to the GitHub Release; mark suffixed tags as pre-releases.

  If `SPARKLE_ED_PRIVATE_KEY` is missing, steps 1, 2 and 4 still run and step 3 is skipped with a clear
  notice, as Prism does.

## Keys and secrets

| Secret | Purpose | Status |
| --- | --- | --- |
| `SPARKLE_ED_PRIVATE_KEY` | Signs update archives and the appcast | To be created by the owner |
| `APPLE_*` (certificate, password, team id, notary credentials) | Developer ID signing and notarisation | Not available yet; the workflow step exists but stays off until they are set |

The private key lives **only** in the GitHub secret (and the owner's keychain). It is never committed,
never printed in a log and never written into a build artefact. The public key is embedded in the app
(`Info.plist` on macOS, the equivalent WinSparkle setting on Windows).

### Generating the EdDSA key (owner, once)

The brief asks for exact steps. These are the expected ones for Sparkle 2; the phase 2 agent must run
them against the pinned Sparkle version, correct anything that differs, and remove this sentence.

1. Download the pinned Sparkle release and open its `bin` folder in Terminal.
2. Run `./generate_keys`. It creates the key pair, stores the private key in the login keychain and
   prints the public key. Give the public key to the agent; it goes into the app.
3. Export the private key to a temporary file: `./generate_keys -x sparkle_private_key.txt`
4. Add it as a repository secret:
   `gh secret set SPARKLE_ED_PRIVATE_KEY --repo JonneNieminen/Carthage < sparkle_private_key.txt`
5. Delete the temporary file: `rm sparkle_private_key.txt`

The agent does not generate, read or handle the private key. If the key is ever lost, existing installs
cannot verify new updates; keep the keychain copy backed up.

## Signing and Gatekeeper

- Builds are ad-hoc signed until a Developer ID exists. A first install from a downloaded `.dmg`
  therefore needs a one-time Gatekeeper approval. The exact steps for the current macOS go in every
  release's notes; take the wording from Prism's releases and check it on a real install.
- The brief requires proof that updates **after** the first install go through Sparkle without that
  step (O6). Test it on a real install, not in a development build.
- The Developer ID signing and `notarytool` steps are written, reviewed and switched off behind a check
  for the `APPLE_*` secrets, so turning them on later is adding secrets, not writing a workflow.

## Update behaviour in the app

- Check in the background on launch. No dialog on launch.
- If an update exists: a small, non-blocking banner with the release notes and two buttons,
  **Install on quit** (default) and **Restart now**.
- Never interrupt playback or recording. While either is running, the banner waits and "Restart now"
  is unavailable.
- Never restart with unsaved work: save first, or ask.
- "Check for updates…" in the app menu; "Receive beta updates" in settings switches the feed.
- An update check that fails (offline, GitHub down) is silent unless the user asked for the check.

## Cutting a release (once the pipeline exists)

1. `main` is green.
2. Move the "Unreleased" entries in `CHANGELOG.md` under the new version and date; commit.
3. Tag `vX.Y.Z` (or `vX.Y.Z-beta.N`) and push the tag.
4. Watch the workflow; check the release page has every asset and the right pre-release flag.
5. On a real install of the previous version, confirm the update is offered and installs.

Tagging and pushing a tag publishes to everyone running Carthage. The agent asks the owner before
creating a release tag unless told to cut that release.
