// phoenix_sim — runs scenario scripts through the portable core and writes a
// single self-contained HTML page of the results.
//
//   phoenix_sim --all                      run every sim/scenarios/*.txt
//   phoenix_sim <file.txt> [more...]       run specific scenarios
//   options: --scenarios-dir <dir>  (default sim/scenarios)
//            --out <file>           (default sim/out/index.html)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "html_writer.h"
#include "runner.h"
#include "scenario.h"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  std::string scenariosDir = "sim/scenarios";
  std::string outPath = "sim/out/index.html";
  bool all = false;
  std::vector<std::string> files;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--all") {
      all = true;
    } else if (arg == "--scenarios-dir" && i + 1 < argc) {
      scenariosDir = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      outPath = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      std::printf(
          "usage: phoenix_sim --all | <scenario.txt>... "
          "[--scenarios-dir dir] [--out file]\n");
      return 0;
    } else if (!arg.empty() && arg[0] == '-') {
      std::fprintf(stderr, "unknown option %s (try --help)\n", arg.c_str());
      return 2;
    } else {
      files.push_back(arg);
    }
  }

  if (all) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(scenariosDir, ec)) {
      if (entry.path().extension() == ".txt") {
        files.push_back(entry.path().string());
      }
    }
    if (ec) {
      std::fprintf(stderr, "cannot list %s: %s\n", scenariosDir.c_str(),
                   ec.message().c_str());
      return 1;
    }
    std::sort(files.begin(), files.end());
  }
  if (files.empty()) {
    std::fprintf(stderr,
                 "no scenarios: pass --all or scenario files (try --help)\n");
    return 2;
  }

  std::vector<sim::ScenarioResult> results;
  for (const std::string& f : files) {
    sim::Scenario sc;
    if (!sim::parseScenarioFile(f, sc)) return 1;
    sim::ScenarioResult r = sim::runScenario(sc);
    std::printf("%-28s %4u ticks  %4zu frames\n", sc.fileStem.c_str(),
                r.totalTicks, r.frames.size());
    results.push_back(std::move(r));
  }

  std::error_code ec;
  fs::create_directories(fs::path(outPath).parent_path(), ec);
  if (!sim::writeHtml(results, outPath)) return 1;

  std::printf("wrote %s (%ju bytes, %zu scenarios)\n", outPath.c_str(),
              static_cast<uintmax_t>(fs::file_size(outPath, ec)),
              results.size());
  return 0;
}
