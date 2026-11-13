#!/usr/bin/env python3
"""Regenerates index.html and builds.json at the root of the gh-pages tree.

Scans <root>/*/build_info.json and <root>/toys/*/build_info.json, so the page
always lists exactly what is published. Usage: pages_index.py <pages-root>
Standard library only.
"""
import html
import json
import os
import re
import sys

ORDER = {"latest": 0, "milestone": 1, "pr": 2, "toy": 3}
TITLES = {"latest": "Latest", "milestone": "Milestones", "pr": "Pull request previews", "toy": "Toys"}


def natural(s: str):
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", s)]


def collect(root: str):
    builds = []
    dirs = [d for d in sorted(os.listdir(root)) if os.path.isdir(os.path.join(root, d)) and not d.startswith(".")]
    paths = [d for d in dirs if d != "toys"]
    toys = os.path.join(root, "toys")
    if os.path.isdir(toys):
        paths += ["toys/" + d for d in sorted(os.listdir(toys)) if os.path.isdir(os.path.join(toys, d))]
    for rel in paths:
        info_path = os.path.join(root, rel, "build_info.json")
        if not os.path.isfile(os.path.join(root, rel, "index.html")):
            continue
        info = {}
        if os.path.isfile(info_path):
            try:
                with open(info_path, encoding="utf-8") as f:
                    info = json.load(f)
            except (OSError, ValueError):
                info = {}
        kind = info.get("kind")
        if kind not in ORDER:
            kind = "toy" if rel.startswith("toys/") else "pr" if rel.startswith("pr-") else "latest" if rel == "latest" else "milestone"
        builds.append(
            {
                "path": rel + "/",
                "kind": kind,
                "version": info.get("version", rel),
                "commit_short": info.get("commit_short", ""),
                "ref": info.get("ref", ""),
                "date": info.get("date", ""),
                "run_url": info.get("run_url", ""),
            }
        )
    builds.sort(key=lambda b: (ORDER[b["kind"]], natural(b["path"])))
    return builds


def render(builds) -> str:
    out = [
        "<!doctype html>",
        '<html lang="en"><head><meta charset="utf-8">',
        '<meta name="viewport" content="width=device-width, initial-scale=1">',
        "<title>Orbitfall playtest builds</title>",
        "<style>",
        ":root{color-scheme:dark}",
        "body{margin:0;padding:16px;background:#05050d;color:#dfe6f0;font:16px/1.4 system-ui,sans-serif}",
        "main{max-width:720px;margin:0 auto}",
        "h1{font-size:22px;margin:8px 0 4px}h2{font-size:15px;margin:24px 0 8px;color:#8fa3bd;"
        "text-transform:uppercase;letter-spacing:.06em}",
        "p{color:#8fa3bd;margin:0 0 8px}",
        "a.build{display:block;padding:12px 14px;margin:0 0 8px;border:1px solid #26304a;border-radius:8px;"
        "background:#0d1122;color:#dfe6f0;text-decoration:none}",
        "a.build:hover,a.build:focus{border-color:#f2c14e}",
        "a.build b{display:block;font-size:17px}",
        "a.build span{display:block;color:#8fa3bd;font:12px/1.5 ui-monospace,monospace;overflow-wrap:anywhere}",
        "</style></head><body><main>",
        "<h1>Orbitfall playtest builds</h1>",
        "<p>Web builds are for feel; use the APK or a desktop build for performance.</p>",
    ]
    if not builds:
        out.append("<p>No builds published yet.</p>")
    last = None
    for b in builds:
        if b["kind"] != last:
            out.append("<h2>{}</h2>".format(TITLES[b["kind"]]))
            last = b["kind"]
        meta = " · ".join(x for x in (b["commit_short"], b["ref"], b["date"].replace("T", " ").replace("Z", " UTC")) if x)
        out.append(
            '<a class="build" href="{}"><b>{}</b><span>{}</span></a>'.format(
                html.escape(b["path"], quote=True), html.escape(b["path"].rstrip("/")), html.escape(meta)
            )
        )
    out.append("</main></body></html>")
    return "\n".join(out) + "\n"


def main() -> int:
    root = sys.argv[1]
    builds = collect(root)
    with open(os.path.join(root, "builds.json"), "w", encoding="utf-8") as f:
        json.dump(builds, f, indent=2)
        f.write("\n")
    with open(os.path.join(root, "index.html"), "w", encoding="utf-8") as f:
        f.write(render(builds))
    open(os.path.join(root, ".nojekyll"), "w").close()
    print("index: {} build(s)".format(len(builds)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
