# Security

## Reporting a problem

Please report security problems privately through GitHub's
[private vulnerability reporting](https://github.com/JonneNieminen/Carthage/security/advisories/new)
rather than a public issue. Say what you found, how to reproduce it and which version it affects.
This is a personal project, so there is no guaranteed response time, but reports are read.

## What matters most here

Carthage is not released yet. Once it is, these are the areas where a flaw would do real harm:

- **Updates.** The app installs updates from an EdDSA-signed feed. Anything that lets an unsigned or
  wrongly signed update install is critical.
- **Project files.** Opening a project from someone else must not run code or write outside the
  project's own folders.
- **Plugin scanning.** Third-party plugins are scanned in a separate process. A plugin crashing that
  process is expected; a plugin escaping it into the app during a scan is a bug.

Third-party plugins run with the user's permissions once loaded, as in every DAW. A plugin behaving
badly is a matter for its vendor.

## Keys

The update signing key exists only as a GitHub Actions secret and in the owner's keychain. It is never
in this repository. If you find a key or credential committed here, report it the same way.
