class_name BuildInfo
extends RefCounted
## Build identity written by CI into `res://build_info.json` (tools/write_build_info.py).
## No autoload: call the static functions. A missing file means a local editor run.

const PATH: String = "res://build_info.json"


## Parsed build info; `{}` when the file is absent or malformed.
static func read() -> Dictionary:
	if not FileAccess.file_exists(PATH):
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(PATH))
	if parsed is Dictionary:
		return parsed as Dictionary
	return {}


## One-line version for an on-screen label, e.g. "pr-12 · 1a2b3c4 · 2026-11-12".
static func label() -> String:
	var info: Dictionary = read()
	if info.is_empty():
		return "dev"
	return "%s · %s · %s" % [
		str(info.get("version", "dev")),
		str(info.get("commit_short", "?")),
		str(info.get("date", "?")).left(10),
	]
