# Releasing and updates

How Carthage is versioned, built, signed, published and updated. The requirements are in
[BRIEF.md](BRIEF.md) ("Updates and releases") and [DECISIONS.md](DECISIONS.md) F10–F11, W25–W26.
Phase 2 built this; parts not yet proven on a real release are marked *not yet tested* and are
listed in the phase 2 report in [ROADMAP.md](ROADMAP.md).

## Versions and tags

- The version is the tag. `v1.2.3` is a stable release; `v1.2.3-beta.4` is a pre-release. No other
  suffix is accepted: the workflow and the build stop on one.
- The release workflow passes the version to CMake as `-DCARTHAGE_RELEASE=1.2.3[-beta.4]`;
  `cmake/CarthageVersion.cmake` turns it into the numeric version (`project()`, `Info.plist` short
  version, the Windows installer), the version the app shows, and the build number.
- **Build number** (`CFBundleVersion`, `sparkle:version`, WinSparkle's build version):
  `major·1000000 + minor·10000 + patch·100 + (N for -beta.N, 99 for stable)`. It grows with every
  release and a beta sorts before its own stable release: 0.1.0 = 10099, 0.1.1-beta.1 = 10101,
  0.1.1 = 10199. Minor and patch stay below 100; betas run from 1 to 98.
- Local and CI builds are `0.0.0-dev`, build 0. They never update themselves.
- Release notes come from [CHANGELOG.md](../CHANGELOG.md): the `## X.Y.Z` (or `## X.Y.Z-beta.N`)
  section. A tag without its section fails before anything is built.

## What a release produces

| Asset | From |
| --- | --- |
| `Carthage-X.Y.Z-arm64.dmg` | macOS arm64 build, signed, in a drag-to-install disk image with a read-me. Also the Sparkle update archive |
| `Carthage-X.Y.Z-x64-setup.exe` | Windows x64 build, Inno Setup (`packaging/windows/Carthage.iss`). Also the WinSparkle update; WinSparkle runs it with `/SILENT /SP- /NOICONS /CLOSEAPPLICATIONS` |
| `appcast.xml` | Stable feed: this release's macOS and Windows items, EdDSA-signed. Stable tags only |
| `appcast-beta.xml` | Beta feed, on the fixed `beta-feed` release: rewritten by every release, stable or beta |

## Feeds

```
stable  https://github.com/JonneNieminen/Carthage/releases/latest/download/appcast.xml
beta    https://github.com/JonneNieminen/Carthage/releases/download/beta-feed/appcast-beta.xml
```

GitHub's `latest` never points at a pre-release, which keeps betas off the stable feed. The beta
feed lives on one fixed pre-release called `beta-feed` that only carries `appcast-beta.xml`; being
a pre-release it can never become `latest`. Every release, stable or beta, rewrites it, so beta
users also get stable releases. Each feed holds only the newest release: an updater only needs that.
The app reads the beta feed when "Receive beta updates" is on (Settings, Updates).

## Workflows

- **`ci.yml`**: every push to `main`, configure, build and test with the release preset on macOS
  arm64 (`macos-26`) and Windows x64 (`windows-2025-vs2026`, MSVC through
  `.github/scripts/msvc-env.ps1`). Both workflows compile through ccache, cached between runs
  (DECISIONS W27).
- **`release.yml`**: on a `v*` tag.
  1. Check the tag and that CHANGELOG has its section.
  2. macOS: build with the version, sign (`packaging/macos/sign.sh`: ad-hoc, or Developer ID once
     the secrets exist), package the DMG (`packaging/macos/make-dmg.sh`), notarise when the secrets
     exist.
  3. Windows: build with the version, package the Inno Setup installer.
  4. Sign both downloads with `sign_update` from the pinned Sparkle release (the key goes in on
     stdin, never to a file or the log), write the appcasts (`packaging/make_appcast.py`), publish
     the GitHub Release (pre-release for `-beta.N`) with install notes, and rewrite the beta feed.

  If a release run fails before publishing, fix the workflow on `main`, then run **Release** from
  the Actions tab ("Run workflow") with the existing tag: it builds that tag with the fixed workflow.
  Tags are never moved or deleted.

  If `SPARKLE_ED_PRIVATE_KEY` is missing, the release is still built and published, without
  signatures or feeds, and the run shows a warning: installed apps will not see it.

## Keys and secrets

| Secret | Purpose | Status |
| --- | --- | --- |
| `SPARKLE_ED_PRIVATE_KEY` | Signs the update downloads for Sparkle and WinSparkle | Set by the owner, 2026-11-17 (keychain account `carthage`) |
| `CARTHAGE_SIGNING_CERTIFICATE`, `CARTHAGE_SIGNING_CERTIFICATE_PASSWORD` | Carthage's self-signed certificate (base64 `.p12`, its identity named "Carthage self-signed"), so macOS keeps the microphone permission across updates (DECISIONS W35) | Set by the owner, 2026-11-18 (certificate in their login keychain, valid 20 years) |
| `APPLE_CERTIFICATE`, `APPLE_CERTIFICATE_PASSWORD`, `APPLE_SIGNING_IDENTITY` | Developer ID signing; wins over the self-signed certificate | Not available; the steps exist and are skipped |
| `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD` | Notarisation with `notarytool` | Not available; skipped |

The private key lives only in the GitHub secret and the owner's login keychain. It is never
committed, printed or written into a build. The **public** key is not secret: it is committed in
`packaging/update-public-key.txt`, from where the build puts it into `Info.plist` (`SUPublicEDKey`)
and into the Windows app. While that file is empty, builds do not update and say so.

### Generating the EdDSA key (owner, once)

Run these in Terminal on your Mac. The agent never generates, reads or handles the private key.
Steps 1–3 were run by the agent on 2026-11-17 against the pinned Sparkle 2.10.1 (download, checksum,
`generate_keys --help`); step 4 onward, which creates the key, is yours alone.

1. Download the pinned Sparkle release into a new folder:

   ```bash
   mkdir -p ~/carthage-key && cd ~/carthage-key
   ```

   ```bash
   curl -fsSL -o Sparkle-2.10.1.tar.xz https://github.com/sparkle-project/Sparkle/releases/download/2.10.1/Sparkle-2.10.1.tar.xz
   ```

2. Check it is the same file Carthage pins:

   ```bash
   echo "1a856d2a9edb8e628de88121d7be04cf0e2167f8641650b78fca00b14244cf27  Sparkle-2.10.1.tar.xz" | shasum -a 256 -c -
   ```

   It must print `Sparkle-2.10.1.tar.xz: OK`.

3. Unpack it:

   ```bash
   mkdir Sparkle && tar -xJf Sparkle-2.10.1.tar.xz -C Sparkle
   ```

4. Create the key pair. It is stored in your login keychain under the account `carthage` (macOS may
   ask to allow keychain access; allow it). It prints the **public** key: give that line to the
   agent, which commits it to `packaging/update-public-key.txt`.

   ```bash
   ./Sparkle/bin/generate_keys --account carthage
   ```

5. Export the private key to a temporary file:

   ```bash
   ./Sparkle/bin/generate_keys --account carthage -x sparkle_private_key.txt
   ```

6. Store it as the repository secret (needs `gh` signed in as the repository owner):

   ```bash
   gh secret set SPARKLE_ED_PRIVATE_KEY --repo JonneNieminen/Carthage < sparkle_private_key.txt
   ```

7. Delete the temporary file and the download:

   ```bash
   rm sparkle_private_key.txt && cd ~ && rm -r ~/carthage-key
   ```

Keep the keychain backed up. If the private key is lost, installed copies can not verify any new
release and must be reinstalled by hand. `./Sparkle/bin/generate_keys --account carthage -p`
prints the public key again at any time.

## Signing and Gatekeeper

- Without a Developer ID, macOS builds are ad-hoc signed (`packaging/macos/sign.sh`, inside-out:
  Sparkle's helpers, the framework, `carthage-scan` and `carthage-plughost`, the app). A first install from a downloaded
  `.dmg` needs the one-time Gatekeeper step, which is in every release's notes and in the DMG's
  "Read me first.txt": open Carthage once, choose Done, then System Settings > Privacy & Security >
  Open Anyway; or `xattr -dr com.apple.quarantine /Applications/Carthage.app`. Tested on v0.1.0 with
  the `xattr` route (2026-11-18); the Open Anyway route has not been tried.
- Updates after the first install go through Sparkle without that step: 0.1.0 updated to 0.1.1 on a
  real install and opened directly (DECISIONS W28).
- An ad-hoc signature changes with every build, so macOS asks again for microphone access after every
  update (seen with 0.1.1, W28). Releases are therefore signed with Carthage's self-signed
  certificate when its secrets exist (W35): the signature is stable, so the permission is asked for
  once more after the first such update and then kept. No hardened runtime, as with ad-hoc, so
  plugins load the same. The run's summary says which signature it used. Verified on the owner's
  Mac (2026-11-18): 0.1.2-beta.2 (ad-hoc) updated to beta.3 (self-signed) and asked once; beta.3
  updated to beta.4 and opened without asking.
- Developer ID signing and notarisation are written into `release.yml` and switch on when the
  `APPLE_*` secrets exist.
- Windows builds are unsigned; SmartScreen warns on first install (the release notes say what to do).

### Making the signing certificate

The owner does this once, on their Mac; the agent never sees the certificate or its password.

1. Keychain Access > Certificate Assistant > Create a Certificate. Name: `Carthage self-signed`
   (exactly; the workflow signs with this name). Identity type: Self-Signed Root. Certificate type:
   Code Signing. Tick "Let me override defaults", set the validity to 3650 days, and accept the
   rest.
2. In the login keychain's My Certificates, right-click "Carthage self-signed" > Export, as a
   `.p12`, with a new password.
3. Store both as secrets, from the folder holding the file:
   `base64 -i Carthage.p12 | gh secret set CARTHAGE_SIGNING_CERTIFICATE --repo JonneNieminen/Carthage`
   and `gh secret set CARTHAGE_SIGNING_CERTIFICATE_PASSWORD --repo JonneNieminen/Carthage` (it asks
   for the password). Then delete the `.p12`; the certificate stays in the keychain as the backup.

To check the secrets without releasing, run the **Signing check** workflow (Actions tab, or
`gh workflow run signing-check.yml`): it signs a small test program with the certificate and
verifies it. A new certificate later means one more permission prompt; updates keep working (W35).

## Update behaviour in the app

`app::Updater` (`src/app/Updater.h`), with `platform/Updater_mac.mm` (Sparkle with Carthage's own
user driver) and `platform/Updater_win.cpp` (WinSparkle).

- One quiet check on launch. No dialog. A failed background check says nothing.
- An update shows in a small card at the bottom right of the window (`ui::UpdateBanner`) with what
  changed and two choices: **Install on quit** (default) downloads it now and installs it when
  Carthage quits; **Restart now** installs it now and reopens Carthage.
- While playing or recording, "Restart now" waits (it is greyed out, with the reason in its tooltip).
- Before a restart the app is asked to save or ask (`Hooks::prepareToRestart`). Phase 1 has nothing
  unsaved except a take in progress, which already blocks the restart; phase 4's projects hook in
  there.
- "Check for updates…" is in the app menu (macOS) or the Help menu (Windows) and reports what it
  found, including "up to date". "Receive beta updates" is in Settings, Updates.
- Windows: WinSparkle can not install on quit. Its card offers **Install now**, which opens
  WinSparkle's own window with the notes; WinSparkle then asks Carthage to quit, which it refuses
  while playing or recording.

## Cutting a release

1. `main` is green.
2. Move the "Unreleased" entries in `CHANGELOG.md` under `## X.Y.Z - YYYY-MM-DD` (or
   `## X.Y.Z-beta.N - …`); commit and push.
3. Tag and push the tag: `git tag vX.Y.Z && git push origin vX.Y.Z`.
4. Watch the Release workflow; check the release page has the DMG, the installer and (stable)
   `appcast.xml`, the right pre-release flag, and that `beta-feed` was updated.
5. On a real install of the previous version, confirm the update is offered and installs.

Tagging publishes to everyone running Carthage. The agent asks the owner before creating a release
tag unless told to cut that release.
