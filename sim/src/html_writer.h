#pragma once

// Emits one fully self-contained HTML file: no external assets, no CDNs, no
// image files. Frames are inline SVG paths (one horizontal run per path
// segment); playback is a small block of vanilla JS.

#include <string>
#include <vector>

#include "runner.h"

namespace sim {

// Returns false (with a message on stderr) if the file cannot be written.
bool writeHtml(const std::vector<ScenarioResult>& results,
               const std::string& outPath);

}  // namespace sim
