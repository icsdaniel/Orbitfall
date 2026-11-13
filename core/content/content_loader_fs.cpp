// Directory front end for the content loader, kept apart from the parser so a
// platform without a usable std::filesystem can drop this one file and feed
// load_content_from_strings instead.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include "content/content_loader.h"

namespace orbitfall {

bool load_content(const std::string& dir, ContentDB& out, std::vector<std::string>& errors) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path root(dir);
  if (!fs::is_directory(root, ec)) {
    errors.push_back(dir + ": content directory not found");
    return false;
  }
  std::map<std::string, std::string> files;
  bool ok = true;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || it->path().extension() != ".json") continue;
    std::string rel = it->path().lexically_relative(root).generic_string();
    std::ifstream in(it->path(), std::ios::binary);
    if (!in) {
      errors.push_back(rel + ": cannot open file");
      ok = false;
      continue;
    }
    std::ostringstream text;
    text << in.rdbuf();
    files[rel] = text.str();
  }
  if (ec) {
    errors.push_back(dir + ": cannot list directory: " + ec.message());
    ok = false;
  }
  // std::map keeps paths sorted, so indices do not depend on directory order.
  ContentDB db;
  if (!load_content_from_strings(files, db, errors) || !ok) return false;
  out = std::move(db);
  return true;
}

}  // namespace orbitfall
