#include "html_writer.h"

#include <cstdio>
#include <fstream>

#include "phoenix/version.h"

namespace sim {
namespace {

// ASCII frame dump -> SVG path of 1px-high horizontal runs.
std::string asciiToPath(const std::string& ascii) {
  std::string d;
  int x = 0, y = 0, runStart = -1;
  auto flush = [&](int endX) {
    if (runStart < 0) return;
    d += "M" + std::to_string(runStart) + " " + std::to_string(y) + ".5h" +
         std::to_string(endX - runStart);
    runStart = -1;
  };
  for (char c : ascii) {
    if (c == '\n') {
      flush(x);
      x = 0;
      ++y;
      continue;
    }
    if (c == '#') {
      if (runStart < 0) runStart = x;
    } else {
      flush(x);
    }
    ++x;
  }
  flush(x);
  return d;
}

std::string escapeJs(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '<': out += "\\u003C"; break;  // no accidental </script>
      case '>': out += "\\u003E"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) break;  // drop controls
        out.push_back(c);
    }
  }
  return out;
}

std::string escapeHtml(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

const char* kCss = R"PHX(
:root {
  --bg: #07090d;
  --panel: #0d1117;
  --edge: #1b2431;
  --ink: #9db0bd;
  --ink-dim: #5c6b77;
  --phos: #7dffb0;
  --phos-dim: rgba(125, 255, 176, 0.16);
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  background: var(--bg);
  color: var(--ink);
  font: 14px/1.5 ui-monospace, "SF Mono", Menlo, Consolas, monospace;
  padding: 28px 32px 80px;
}
header { margin-bottom: 10px; }
header h1 { color: #e8f2ec; font-size: 21px; letter-spacing: 2px; }
header .sub { color: var(--ink-dim); margin-top: 4px; }
header .legend { color: var(--ink-dim); font-size: 12px; margin-top: 10px; }
header .legend b { color: var(--ink); font-weight: normal; }
.scenario { border-top: 1px solid var(--edge); margin-top: 34px; padding-top: 22px; }
.scenario h2 { color: #dfe9e3; font-size: 16px; font-weight: 600; }
.scenario .desc { color: var(--ink-dim); margin: 4px 0 16px; }
.playrow { display: flex; gap: 26px; flex-wrap: wrap; align-items: flex-start; }
.bezel {
  background: #000;
  border: 1px solid #161d26;
  border-radius: 16px;
  padding: 22px 26px;
  box-shadow: inset 0 0 34px rgba(0,0,0,0.9), 0 1px 0 rgba(255,255,255,0.03);
}
svg.frame {
  width: 576px; height: 320px; display: block;
  color: var(--phos);
  filter: drop-shadow(0 0 3px rgba(125,255,176,0.45));
}
svg.frame path, svg.mini path {
  stroke: currentColor; stroke-width: 1; fill: none;
  shape-rendering: crispEdges;
}
.controls { min-width: 240px; }
.buttons { display: flex; gap: 8px; margin-bottom: 14px; }
.buttons button {
  background: var(--panel); color: var(--ink);
  border: 1px solid var(--edge); border-radius: 7px;
  font: inherit; padding: 6px 13px; cursor: pointer; min-width: 44px;
}
.buttons button:hover { border-color: #33465c; color: #d7e3dd; }
.readout { color: var(--ink-dim); font-size: 13px; }
.readout .tick { color: var(--ink); }
.readout .label {
  color: var(--phos); min-height: 3em; margin-top: 8px;
  max-width: 300px; overflow-wrap: break-word; font-size: 13px;
}
.meta { color: var(--ink-dim); font-size: 12px; margin-top: 14px; }
.strip {
  display: flex; gap: 12px; overflow-x: auto;
  margin-top: 20px; padding-bottom: 10px;
}
.thumb { flex: 0 0 auto; width: 150px; cursor: pointer; }
.thumb .shell {
  background: #000; border: 1px solid #131a22; border-radius: 7px;
  padding: 6px 8px;
}
.thumb.current .shell { border-color: var(--phos); box-shadow: 0 0 9px var(--phos-dim); }
svg.mini { width: 132px; height: 74px; display: block; color: var(--phos); }
.thumb .cap {
  color: var(--ink-dim); font-size: 11px; margin-top: 5px;
  white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
}
.thumb .cap b { color: var(--ink); font-weight: normal; }
footer { border-top: 1px solid var(--edge); margin-top: 44px; padding-top: 14px;
         color: var(--ink-dim); font-size: 12px; }
noscript { display: block; color: #ffb84d; margin: 18px 0; }
)PHX";

const char* kJs = R"PHX(
function frameIndexAt(frames, t) {
  let lo = 0, hi = frames.length - 1, ans = 0;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (frames[mid].t <= t) { ans = mid; lo = mid + 1; } else { hi = mid - 1; }
  }
  return ans;
}

function svgEl(tag) { return document.createElementNS('http://www.w3.org/2000/svg', tag); }

function initScenario(sc, i) {
  const root = document.getElementById('sc' + i);
  const path = root.querySelector('svg.frame path');
  const bezel = root.querySelector('.bezel');
  const tickEl = root.querySelector('.tick');
  const labelEl = root.querySelector('.label');
  const strip = root.querySelector('.strip');

  // Sticky labels: an animation frame keeps showing the event that caused it.
  let sticky = '';
  sc.frames.forEach(f => { f.s = f.l || sticky; if (f.l) sticky = f.l; });

  // Build the strip from key (event) frames.
  const thumbs = [];
  sc.frames.forEach((f, fi) => {
    if (!f.k) return;
    const t = document.createElement('div');
    t.className = 'thumb';
    const shell = document.createElement('div');
    shell.className = 'shell';
    const svg = svgEl('svg');
    svg.setAttribute('viewBox', '0 0 72 40');
    svg.setAttribute('class', 'mini');
    const p = svgEl('path');
    p.setAttribute('d', f.d);
    svg.appendChild(p);
    shell.appendChild(svg);
    t.appendChild(shell);
    const cap = document.createElement('div');
    cap.className = 'cap';
    const tickB = document.createElement('b');
    tickB.textContent = 't' + f.t;
    cap.appendChild(tickB);
    cap.appendChild(document.createTextNode(f.l ? ' ' + f.l : ''));
    cap.title = 't' + f.t + (f.l ? ' — ' + f.l : '');
    t.appendChild(cap);
    t.onclick = () => { vt = f.t; renderNow(true); };
    strip.appendChild(t);
    thumbs.push({ el: t, fi: fi });
  });

  let vt = 0, playing = true, speed = 1, lastTs = null, curIdx = -1;
  let visible = false, shownTick = -1;

  // Offscreen players idle completely \u2014 ten filtered SVGs animating at once
  // would swamp the renderer.
  new IntersectionObserver(entries => {
    visible = entries[0].isIntersecting;
    if (visible) lastTs = null;
  }, { rootMargin: '100px' }).observe(root);

  function renderNow(force) {
    const idx = frameIndexAt(sc.frames, vt);
    if (idx !== curIdx || force) {
      curIdx = idx;
      const f = sc.frames[idx];
      path.setAttribute('d', f.d);
      bezel.style.opacity = (0.25 + 0.75 * (f.b / 255)).toFixed(3);
      labelEl.textContent = f.s || '\u00A0';
      let cur = -1;
      for (let k = 0; k < thumbs.length; k++) {
        if (thumbs[k].fi <= idx) cur = k;
        thumbs[k].el.classList.remove('current');
      }
      if (cur >= 0) thumbs[cur].el.classList.add('current');
    }
    const t = Math.floor(vt);
    if (t !== shownTick || force) {
      shownTick = t;
      tickEl.textContent = 't ' + t + ' / ' + sc.total +
                           '  (' + (t / 10).toFixed(1) + 's)';
    }
  }

  const playBtn = root.querySelector('.play');
  root.querySelector('.restart').onclick = () => { vt = 0; renderNow(true); };
  root.querySelector('.back').onclick = () => {
    playing = false; playBtn.textContent = '\u25B6';
    const idx = frameIndexAt(sc.frames, vt);
    vt = sc.frames[Math.max(0, idx - 1)].t;
    renderNow(true);
  };
  root.querySelector('.fwd').onclick = () => {
    playing = false; playBtn.textContent = '\u25B6';
    const idx = frameIndexAt(sc.frames, vt);
    vt = sc.frames[Math.min(sc.frames.length - 1, idx + 1)].t;
    renderNow(true);
  };
  playBtn.onclick = () => {
    playing = !playing;
    playBtn.textContent = playing ? '\u275A\u275A' : '\u25B6';
  };
  const speedBtn = root.querySelector('.speed');
  const speeds = [1, 2, 4, 0.5];
  let si = 0;
  speedBtn.onclick = () => {
    si = (si + 1) % speeds.length;
    speed = speeds[si];
    speedBtn.textContent = speeds[si] + 'x';
  };

  function loop(ts) {
    if (visible) {
      if (lastTs === null) lastTs = ts;
      if (playing) {
        vt += (ts - lastTs) / 100 * speed;  // 1 tick = 100 ms
        if (vt > sc.total + 12) vt = 0;     // brief hold, then loop
      }
      lastTs = ts;
      renderNow(false);
    }
    requestAnimationFrame(loop);
  }
  renderNow(true);
  requestAnimationFrame(loop);
}

SCENARIOS.forEach(initScenario);
)PHX";

}  // namespace

bool writeHtml(const std::vector<ScenarioResult>& results,
               const std::string& outPath) {
  std::string html;
  html.reserve(1 << 20);

  html += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  html += "<title>Project Phoenix — HUD Simulator</title>\n<style>";
  html += kCss;
  html += "</style>\n</head>\n<body>\n";

  html += "<header>\n<h1>PROJECT PHOENIX &middot; HUD SIMULATOR</h1>\n";
  html += "<div class=\"sub\">72&times;40 monocular OLED &middot; rendered by the same "
          "portable core the firmware runs &middot; fw v";
  html += escapeHtml(phoenix::kFirmwareVersion);
  html += "</div>\n";
  html += "<div class=\"legend\"><b>ANCS</b> events arrive straight from the iPhone "
          "over BLE (no app involved) &middot; <b>TX</b> frames are the Phoenix "
          "assistant service byte stream, fed to the decoder in 20-byte radio "
          "chunks &middot; <b>RX</b> is glasses&rarr;phone traffic &middot; "
          "1 tick = 100 ms</div>\n";
  html += "</header>\n";
  html += "<noscript>JavaScript is disabled: playback and film strips are built "
          "by inline script. The data is embedded below regardless.</noscript>\n";

  for (size_t i = 0; i < results.size(); ++i) {
    const ScenarioResult& r = results[i];
    html += "<section class=\"scenario\" id=\"sc" + std::to_string(i) + "\">\n";
    html += "<h2>" + escapeHtml(r.name) + "</h2>\n";
    if (!r.desc.empty()) {
      html += "<p class=\"desc\">" + escapeHtml(r.desc) + "</p>\n";
    }
    html += "<div class=\"playrow\">\n";
    html += "<div class=\"bezel\"><svg class=\"frame\" viewBox=\"0 0 72 40\">"
            "<path d=\"\"/></svg></div>\n";
    html += "<div class=\"controls\">\n<div class=\"buttons\">"
            "<button class=\"restart\" title=\"restart\">&#10226;</button>"
            "<button class=\"back\" title=\"step back\">&#9664;</button>"
            "<button class=\"play\" title=\"play/pause\">&#10074;&#10074;</button>"
            "<button class=\"fwd\" title=\"step forward\">&#9654;</button>"
            "<button class=\"speed\" title=\"speed\">1x</button></div>\n";
    html += "<div class=\"readout\"><span class=\"tick\"></span>"
            "<div class=\"label\"></div></div>\n";
    html += "<div class=\"meta\">" + std::to_string(r.frames.size()) +
            " distinct frames &middot; " + std::to_string(r.totalTicks) +
            " ticks (" + std::to_string(r.totalTicks / 10) + "s) &middot; "
            "click a strip frame to seek</div>\n";
    html += "</div>\n</div>\n";
    html += "<div class=\"strip\"></div>\n</section>\n";
  }

  html += "<footer>Generated by <code>phoenix_sim</code> from "
          "<code>sim/scenarios/*.txt</code>. Every pixel came through "
          "<code>phoenix::Device</code> &mdash; the firmware pushes the same "
          "buffer to the real panel.</footer>\n";

  // Frame data + player.
  html += "<script>\nconst SCENARIOS = [\n";
  for (const ScenarioResult& r : results) {
    html += "{name:\"" + escapeJs(r.name) + "\",total:" +
            std::to_string(r.totalTicks) + ",frames:[\n";
    for (const CapturedFrame& f : r.frames) {
      html += "{t:" + std::to_string(f.tick) + ",b:" +
              std::to_string(static_cast<int>(f.brightness)) + ",k:" +
              (f.key ? "1" : "0") + ",l:\"" + escapeJs(f.label) + "\",d:\"" +
              asciiToPath(f.ascii) + "\"},\n";
    }
    html += "]},\n";
  }
  html += "];\n";
  html += kJs;
  html += "</script>\n</body>\n</html>\n";

  std::ofstream out(outPath, std::ios::binary);
  if (!out) {
    std::fprintf(stderr, "cannot write %s\n", outPath.c_str());
    return false;
  }
  out << html;
  return true;
}

}  // namespace sim
