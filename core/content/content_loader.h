#pragma once
// Content loading and validation (BRIEF §5.4). No Godot includes.
#include <map>
#include <string>
#include <vector>

#include "content/content.h"

namespace orbitfall {

// Loads every *.json under `dir` (the content/ folder: rules.json plus
// planets/ sun/ buildings/ enemies/ waves/), converts degrees to radians and
// validates. Returns true and fills `out` only when there are no errors;
// otherwise `out` is left untouched and `errors` gets one line per problem
// ("enemies/dart.json: 'speed': must be > 0").
bool load_content(const std::string& dir, ContentDB& out, std::vector<std::string>& errors);

// Same, from in-memory files: path -> JSON text. The category is the file's
// parent folder name, so keys may be relative ("enemies/dart.json") or full
// ("res://content/enemies/dart.json"). Used by the GDExtension, which reads
// files through Godot FileAccess on web and Android.
bool load_content_from_strings(const std::map<std::string, std::string>& files, ContentDB& out,
                               std::vector<std::string>& errors);

}  // namespace orbitfall
