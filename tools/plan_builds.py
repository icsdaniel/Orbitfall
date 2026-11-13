#!/usr/bin/env python3
"""Decides which web builds a CI run publishes (BRIEF §5.8). Prints a JSON list.

Each entry: {"dest", "version", "kind", "pr", "main_scene"} where dest is the
folder on GitHub Pages. Inputs come from environment variables set by the
workflow (EVENT_NAME, REF_TYPE, REF_NAME, DEFAULT_BRANCH, PR_NUMBER, BEFORE, SHA).

  pull_request            -> pr-<n>/
  push to default branch  -> latest/ (+ toys changed by the push)
  push to another branch  -> toys changed by the push
  push of tag m<digits>…  -> <tag>/
  workflow_dispatch       -> as a push, but with every toy

A toy is toys/<name>/ holding <name>.tscn, main.tscn or exactly one .tscn, or a
single file toys/<name>.tscn. Names must match [a-z0-9][a-z0-9_-]*.
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAME = re.compile(r"^[a-z0-9][a-z0-9_-]*$")
TAG = re.compile(r"^m[0-9]+[a-z0-9.-]*$")


def all_toys(root: str = ROOT) -> dict:
    """name -> res:// path of the toy's scene."""
    toys = {}
    base = os.path.join(root, "toys")
    if not os.path.isdir(base):
        return toys
    for entry in sorted(os.listdir(base)):
        full = os.path.join(base, entry)
        if os.path.isdir(full):
            scenes = sorted(f for f in os.listdir(full) if f.endswith(".tscn"))
            pick = next((s for s in (entry + ".tscn", "main.tscn") if s in scenes), None)
            if pick is None and len(scenes) == 1:
                pick = scenes[0]
            if pick and NAME.match(entry):
                toys[entry] = "res://toys/{}/{}".format(entry, pick)
        elif entry.endswith(".tscn") and NAME.match(entry[:-5]):
            toys.setdefault(entry[:-5], "res://toys/" + entry)
    return toys


def changed_toys(before: str, sha: str, toys: dict) -> list:
    """Toys touched between two commits; every toy when the range is unknown."""
    if not before or set(before) == {"0"}:
        return sorted(toys)
    try:
        out = subprocess.check_output(
            ["git", "diff", "--name-only", before, sha, "--", "toys"], cwd=ROOT, text=True, stderr=subprocess.DEVNULL
        )
    except (OSError, subprocess.CalledProcessError):
        return sorted(toys)
    names = set()
    for line in out.splitlines():
        parts = line.split("/")
        if len(parts) >= 2:
            names.add(parts[1][:-5] if len(parts) == 2 and parts[1].endswith(".tscn") else parts[1])
    return sorted(n for n in names if n in toys)


def plan(env: dict, toys: dict, changed=None) -> list:
    event = env.get("EVENT_NAME", "")
    ref_type = env.get("REF_TYPE", "branch")
    ref_name = env.get("REF_NAME", "")
    builds = []

    def toy_builds(names):
        return [
            {"dest": "toys/" + n, "version": "toy-" + n, "kind": "toy", "pr": 0, "main_scene": toys[n]} for n in names
        ]

    if event == "pull_request":
        if env.get("PR_ACTION") != "closed" and env.get("PR_SAME_REPO") == "true":
            n = int(env["PR_NUMBER"])
            builds.append({"dest": "pr-%d" % n, "version": "pr-%d" % n, "kind": "pr", "pr": n, "main_scene": ""})
    elif event in ("push", "workflow_dispatch"):
        if ref_type == "tag":
            if TAG.match(ref_name):
                builds.append({"dest": ref_name, "version": ref_name, "kind": "milestone", "pr": 0, "main_scene": ""})
        else:
            if ref_name == env.get("DEFAULT_BRANCH", "main"):
                builds.append({"dest": "latest", "version": "latest", "kind": "latest", "pr": 0, "main_scene": ""})
            if event == "workflow_dispatch":
                names = sorted(toys)
            elif changed is not None:
                names = changed
            else:
                names = changed_toys(env.get("BEFORE", ""), env.get("SHA", ""), toys)
            builds += toy_builds(names)
    return builds


def main() -> int:
    builds = plan(dict(os.environ), all_toys())
    print(json.dumps(builds))
    return 0


if __name__ == "__main__":
    sys.exit(main())
