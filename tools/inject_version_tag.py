#!/usr/bin/env python3
"""Adds a small version tag (bottom-left corner) to an exported web index.html.

Keeps "every build shows its version" true for web builds even before the game
draws its own label. Usage: inject_version_tag.py <index.html> <build_info.json>
"""
import html
import json
import sys

MARK = "<!-- orbitfall-version-tag -->"


def main() -> int:
    index_path, info_path = sys.argv[1], sys.argv[2]
    with open(info_path, encoding="utf-8") as f:
        info = json.load(f)
    label = "{} · {} · {}".format(info.get("version", "dev"), info.get("commit_short", "?"), info.get("date", "")[:10])
    tag = (
        MARK
        + '<div id="orbitfall-version" style="position:fixed;left:6px;bottom:4px;z-index:10;'
        "font:11px/1.2 monospace;color:#9ab;opacity:.7;pointer-events:none;"
        'text-shadow:0 0 2px #000">' + html.escape(label) + "</div>"
    )
    with open(index_path, encoding="utf-8") as f:
        page = f.read()
    if MARK in page:
        return 0
    if "</body>" not in page:
        print("no </body> in " + index_path, file=sys.stderr)
        return 1
    page = page.replace("</body>", tag + "\n</body>", 1)
    with open(index_path, "w", encoding="utf-8") as f:
        f.write(page)
    return 0


if __name__ == "__main__":
    sys.exit(main())
