#!/usr/bin/env python3
"""Release notes from CHANGELOG.md for one version (docs/RELEASING.md, "Versions and tags").

    python3 -I packaging/release_notes.py CHANGELOG.md 0.1.1 --markdown notes.md --html notes.html

The section is the "## " heading that names the version ("## 0.1.1 - 2026-11-20" or
"## [0.1.1] - ..."). Markdown keeps the section as written, for the GitHub release. HTML is a plain
list of the changes, for the appcast: the in-app banner shows its items one per line.
Exits with an error if the section is missing or empty, so a release can not go out without notes.
"""
import argparse
import html
import re
import sys


def section(text: str, version: str) -> list[str]:
    lines = text.splitlines()
    pattern = re.compile(r"^##\s+\[?" + re.escape(version) + r"\]?(\s|$)")
    start = next((i for i, l in enumerate(lines) if pattern.match(l)), None)
    if start is None:
        sys.exit(f"CHANGELOG has no '## {version}' section. Move the Unreleased entries under it first.")
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("## ")), len(lines))
    body = [l for l in lines[start + 1:end]]
    while body and not body[0].strip():
        body.pop(0)
    while body and not body[-1].strip():
        body.pop()
    if not any(l.lstrip().startswith("- ") for l in body):
        sys.exit(f"The '## {version}' section of CHANGELOG has no entries.")
    return body


def items(body: list[str]) -> list[str]:
    """The "- " entries, with wrapped continuation lines joined."""
    out: list[str] = []
    for line in body:
        stripped = line.strip()
        if stripped.startswith("- "):
            out.append(stripped[2:])
        elif stripped and out and line.startswith("  "):
            out[-1] += " " + stripped
    return out


def inline(text: str) -> str:
    text = html.escape(text, quote=False)
    text = re.sub(r"`([^`]+)`", r"<code>\1</code>", text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", text)
    return re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("changelog")
    p.add_argument("version")
    p.add_argument("--markdown")
    p.add_argument("--html")
    a = p.parse_args()

    with open(a.changelog, encoding="utf-8") as f:
        body = section(f.read(), a.version)

    if a.markdown:
        with open(a.markdown, "w", encoding="utf-8") as f:
            f.write("\n".join(body) + "\n")

    if a.html:
        with open(a.html, "w", encoding="utf-8") as f:
            f.write("<ul>\n" + "".join(f"<li>{inline(i)}</li>\n" for i in items(body)) + "</ul>\n")


if __name__ == "__main__":
    main()
