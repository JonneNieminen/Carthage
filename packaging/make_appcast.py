#!/usr/bin/env python3
"""Writes a Sparkle appcast with one release: a macOS item and a Windows item (docs/RELEASING.md).

    python3 -I packaging/make_appcast.py --version 0.1.1 --build 10199 --tag v0.1.1 \\
        --repo JonneNieminen/Carthage --notes notes.html \\
        --mac Carthage-0.1.1-arm64.dmg --mac-signature SIG --win Carthage-0.1.1-x64-setup.exe \\
        --win-signature SIG --output appcast.xml

Sparkle reads only items for macOS; WinSparkle only the one marked sparkle:os="windows". Both
compare sparkle:version, the build number (cmake/CarthageVersion.cmake), and check the EdDSA
signature with the public key built into the app. Each feed holds the newest release only:
updaters only need the newest one.
"""
import argparse
import os
from email.utils import formatdate
from xml.sax.saxutils import escape, quoteattr

SPARKLE = "http://www.andymatuschak.org/xml-namespaces/sparkle"


def item(title, version, build, notes, url, length, signature, os_name, extra):
    attrs = f'url={quoteattr(url)} length="{length}" type="application/octet-stream" ' \
            f'sparkle:edSignature={quoteattr(signature)}'
    if os_name:
        attrs += f' sparkle:os="{os_name}"'
    for k, v in extra.get("enclosure", {}).items():
        attrs += f" {k}={quoteattr(v)}"
    tags = "".join(f"      <{k}>{escape(v)}</{k}>\n" for k, v in extra.get("tags", {}).items())
    return (
        "    <item>\n"
        f"      <title>{escape(title)}</title>\n"
        f"      <pubDate>{formatdate(usegmt=True)}</pubDate>\n"
        f"      <sparkle:version>{build}</sparkle:version>\n"
        f"      <sparkle:shortVersionString>{escape(version)}</sparkle:shortVersionString>\n"
        f"{tags}"
        f"      <description><![CDATA[{notes}]]></description>\n"
        f"      <enclosure {attrs}/>\n"
        "    </item>\n"
    )


def main():
    p = argparse.ArgumentParser()
    for name in ("--version", "--build", "--tag", "--repo", "--notes", "--output"):
        p.add_argument(name, required=True)
    p.add_argument("--title", default="Carthage")
    p.add_argument("--mac")
    p.add_argument("--mac-signature")
    p.add_argument("--win")
    p.add_argument("--win-signature")
    a = p.parse_args()

    with open(a.notes, encoding="utf-8") as f:
        notes = f.read().replace("]]>", "]]&gt;")

    base = f"https://github.com/{a.repo}/releases/download/{a.tag}/"
    items = []

    if a.mac:
        items.append(item(f"Carthage {a.version}", a.version, a.build, notes,
                          base + os.path.basename(a.mac), os.path.getsize(a.mac), a.mac_signature, "macos",
                          {"tags": {"sparkle:minimumSystemVersion": "13.0"}}))
    if a.win:
        items.append(item(f"Carthage {a.version}", a.version, a.build, notes,
                          base + os.path.basename(a.win), os.path.getsize(a.win), a.win_signature, "windows",
                          {"enclosure": {"sparkle:installerArguments": "/SILENT /SP- /NOICONS /CLOSEAPPLICATIONS"}}))

    with open(a.output, "w", encoding="utf-8") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n'
                f'<rss version="2.0" xmlns:sparkle="{SPARKLE}">\n'
                "  <channel>\n"
                f"    <title>{escape(a.title)}</title>\n"
                f"    <link>https://github.com/{a.repo}</link>\n"
                + "".join(items) +
                "  </channel>\n</rss>\n")
    print(f"wrote {a.output} with {len(items)} item(s)")


if __name__ == "__main__":
    main()
