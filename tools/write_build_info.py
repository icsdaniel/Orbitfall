#!/usr/bin/env python3
"""Writes build_info.json (project root) so every export can show its version.

Read in-game by game/app/build_info.gd. Values come from flags, then GitHub
Actions environment variables, then local git. Standard library only.
"""
import argparse
import datetime
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def git(*args: str) -> str:
    try:
        return subprocess.check_output(("git",) + args, cwd=ROOT, text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--version", default="", help="label shown in game, e.g. latest, pr-12, m1, toy-rotation")
    p.add_argument("--kind", default="local", choices=["local", "latest", "pr", "milestone", "toy"])
    p.add_argument("--commit", default="")
    p.add_argument("--ref", default="", help="branch or tag name")
    p.add_argument("--pr", type=int, default=0)
    p.add_argument("--target", default="", help="template_debug or template_release")
    p.add_argument("--out", default=os.path.join(ROOT, "build_info.json"))
    a = p.parse_args()

    commit = a.commit or os.environ.get("GITHUB_SHA", "") or git("rev-parse", "HEAD")
    ref = a.ref or os.environ.get("GITHUB_HEAD_REF", "") or os.environ.get("GITHUB_REF_NAME", "")
    ref = ref or git("rev-parse", "--abbrev-ref", "HEAD")
    run_url = ""
    if os.environ.get("GITHUB_RUN_ID"):
        run_url = "{}/{}/actions/runs/{}".format(
            os.environ.get("GITHUB_SERVER_URL", "https://github.com"),
            os.environ.get("GITHUB_REPOSITORY", ""),
            os.environ["GITHUB_RUN_ID"],
        )
    info = {
        "version": a.version or ("pr-%d" % a.pr if a.pr else a.kind),
        "kind": a.kind,
        "commit": commit,
        "commit_short": commit[:7],
        "ref": ref,
        "pr": a.pr,
        "target": a.target,
        "date": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "run_url": run_url,
    }
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(info, f, indent=2)
        f.write("\n")
    print("build_info: {version} {commit_short} {ref} {date}".format(**info))
    return 0


if __name__ == "__main__":
    sys.exit(main())
