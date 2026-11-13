// Determinism guard (CLAUDE.md hard rule 4): scans the core/ sources and fails
// on unseeded randomness or clocks. Comments are ignored. core/third_party is
// skipped (not ours) and so is core/bench (the bench harness has to time itself
// and never feeds the sim).
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "doctest/doctest.h"

namespace {

struct Banned {
  std::string token;
  bool isCall;  // must be followed by '(' and not be part of a longer identifier or a member
};

// Tokens are split so this file does not trip its own scan.
const std::vector<Banned>& bannedList() {
  static const std::vector<Banned> list = {
      {std::string("ra") + "nd", true},
      {std::string("sra") + "nd", true},
      {std::string("ti") + "me", true},
      {std::string("clo") + "ck", true},
      {std::string("system_") + "clock", false},
      {std::string("steady_") + "clock", false},
      {std::string("high_resolution_") + "clock", false},
      {std::string("random_") + "device", false},
  };
  return list;
}

bool isIdent(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// Replaces // and /* */ comments with spaces, keeping newlines for line numbers.
std::string stripComments(const std::string& src) {
  std::string out = src;
  const size_t n = src.size();
  size_t i = 0;
  while (i < n) {
    if (src[i] == '/' && i + 1 < n && src[i + 1] == '/') {
      while (i < n && src[i] != '\n') out[i++] = ' ';
    } else if (src[i] == '/' && i + 1 < n && src[i + 1] == '*') {
      while (i < n && !(src[i] == '*' && i + 1 < n && src[i + 1] == '/')) {
        if (src[i] != '\n') out[i] = ' ';
        ++i;
      }
      if (i < n) out[i++] = ' ';
      if (i < n) out[i++] = ' ';
    } else if (src[i] == '"') {
      ++i;  // skip string literals so "//" inside one is not a comment
      while (i < n && src[i] != '"' && src[i] != '\n') i += (src[i] == '\\') ? 2 : 1;
      if (i < n) ++i;
    } else {
      ++i;
    }
  }
  return out;
}

// Returns "line N: token" for each banned use in `src`.
std::vector<std::string> findBanned(const std::string& src) {
  std::vector<std::string> hits;
  const std::string text = stripComments(src);
  for (const Banned& b : bannedList()) {
    size_t pos = 0;
    while ((pos = text.find(b.token, pos)) != std::string::npos) {
      const size_t end = pos + b.token.size();
      bool hit = true;
      if (pos > 0 && isIdent(text[pos - 1])) hit = false;
      if (end < text.size() && isIdent(text[end])) hit = false;
      if (hit && b.isCall) {
        // A member call such as world.time( is not the C library function.
        if (pos > 0 && text[pos - 1] == '.') hit = false;
        if (pos > 1 && text[pos - 1] == '>' && text[pos - 2] == '-') hit = false;
        size_t p = end;
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
        if (p >= text.size() || text[p] != '(') hit = false;
      }
      if (hit) {
        size_t line = 1;
        for (size_t k = 0; k < pos; ++k) line += text[k] == '\n';
        hits.push_back("line " + std::to_string(line) + ": " + b.token);
      }
      pos = end;
    }
  }
  return hits;
}

std::string join(const std::string& a, const std::string& b) { return a + b; }

}  // namespace

TEST_CASE("banned calls: the scanner catches each banned pattern") {
  const std::string r = join("ra", "nd"), t = join("ti", "me"), c = join("clo", "ck");
  CHECK(findBanned("int x = " + r + "();").size() == 1);
  CHECK(findBanned("int x = std::" + r + " ();").size() == 1);
  CHECK(findBanned("s" + r + "(42);").size() == 1);
  CHECK(findBanned("auto now = " + t + "(nullptr);").size() == 1);
  CHECK(findBanned("auto now = std::" + t + "(0);").size() == 1);
  CHECK(findBanned("auto c = " + c + "();").size() == 1);
  CHECK(findBanned("std::chrono::system_" + c + "::now()").size() == 1);
  CHECK(findBanned("std::chrono::steady_" + c + "::now()").size() == 1);
  CHECK(findBanned("std::chrono::high_resolution_" + c + "::now()").size() == 1);
  CHECK(findBanned("std::random_" + join("dev", "ice") + " rd;").size() == 1);
  CHECK(findBanned("a\nb\n" + r + "();").front() == "line 3: " + r);
}

TEST_CASE("banned calls: the scanner ignores lookalikes and comments") {
  const std::string r = join("ra", "nd"), t = join("ti", "me");
  CHECK(findBanned("float simTime(World&); double x = w.simTime;").empty());
  CHECK(findBanned("uint32_t operand(int); void strand(); int " + r + "om = 1;").empty());
  CHECK(findBanned("world." + t + "(); world->" + t + "();").empty());
  CHECK(findBanned("// no " + r + "() or " + t + "() here\nint x;").empty());
  CHECK(findBanned("/* " + r + "()\n " + t + "() */ int x;").empty());
  CHECK(findBanned("float " + t + " = 0; runtime(3); lifetime (4);").empty());
  CHECK(findBanned("const char* url = \"http://x\"; " + r + "();").size() == 1);
}

TEST_CASE("banned calls: core/ has no unseeded randomness or clocks") {
  namespace fs = std::filesystem;
  const fs::path root = fs::path(ORBITFALL_SOURCE_DIR);
  REQUIRE(fs::is_directory(root));

  int scanned = 0;
  for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
    const std::string name = it->path().filename().string();
    if (it->is_directory()) {
      if (name == "third_party" || name == "bench" || name == "build") it.disable_recursion_pending();
      continue;
    }
    const std::string ext = it->path().extension().string();
    if (ext != ".h" && ext != ".hpp" && ext != ".cpp" && ext != ".cc" && ext != ".inl") continue;
    std::ifstream in(it->path(), std::ios::binary);
    REQUIRE(in.good());
    std::stringstream buffer;
    buffer << in.rdbuf();
    ++scanned;
    for (const std::string& hit : findBanned(buffer.str())) {
      FAIL_CHECK("banned in core/: " << fs::relative(it->path(), root).string() << " " << hit);
    }
  }
  CHECK(scanned >= 10);  // the scan really walked core/
}
