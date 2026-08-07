#include "phoenix/text_sanitize.h"

#include <cstdint>

namespace phoenix {
namespace {

struct Decoded {
  uint32_t cp;
  size_t len;       // bytes consumed
  bool valid;
  bool incomplete;  // sequence truncated by end of input
};

// Strict UTF-8 decode of one codepoint. Never reads past p[n-1].
Decoded decodeOne(const uint8_t* p, size_t n) {
  const uint8_t b0 = p[0];
  if (b0 < 0x80) return {b0, 1, true, false};
  int cont;
  uint32_t cp;
  uint32_t minCp;
  if (b0 >= 0xC2 && b0 <= 0xDF) {
    cont = 1; cp = b0 & 0x1Fu; minCp = 0x80;
  } else if (b0 >= 0xE0 && b0 <= 0xEF) {
    cont = 2; cp = b0 & 0x0Fu; minCp = 0x800;
  } else if (b0 >= 0xF0 && b0 <= 0xF4) {
    cont = 3; cp = b0 & 0x07u; minCp = 0x10000;
  } else {
    // 0x80-0xC1 (stray continuation / overlong lead) or 0xF5-0xFF.
    return {0, 1, false, false};
  }
  if (static_cast<size_t>(cont) + 1 > n) return {0, n, false, true};
  for (int i = 1; i <= cont; ++i) {
    if ((p[i] & 0xC0u) != 0x80u) return {0, 1, false, false};  // reprocess tail
    cp = (cp << 6) | (p[i] & 0x3Fu);
  }
  if (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    return {0, static_cast<size_t>(cont) + 1, false, false};
  }
  return {cp, static_cast<size_t>(cont) + 1, true, false};
}

// Latin-1 letters 0xC0-0xFF stripped to base ASCII. 0xD7/0xF7 are handled by
// the typography map before this table is consulted.
const char* const kLatin1[0x40] = {
    "A", "A", "A", "A", "A", "A", "AE", "C",   // À Á Â Ã Ä Å Æ Ç
    "E", "E", "E", "E", "I", "I", "I",  "I",   // È É Ê Ë Ì Í Î Ï
    "D", "N", "O", "O", "O", "O", "O",  "x",   // Ð Ñ Ò Ó Ô Õ Ö ×
    "O", "U", "U", "U", "U", "Y", "Th", "ss",  // Ø Ù Ú Û Ü Ý Þ ß
    "a", "a", "a", "a", "a", "a", "ae", "c",   // à á â ã ä å æ ç
    "e", "e", "e", "e", "i", "i", "i",  "i",   // è é ê ë ì í î ï
    "d", "n", "o", "o", "o", "o", "o",  "/",   // ð ñ ò ó ô õ ö ÷
    "o", "u", "u", "u", "u", "y", "th", "y",   // ø ù ú û ü ý þ ÿ
};

void appendMapped(std::string& out, uint32_t cp) {
  if (cp == '\n') { out.push_back('\n'); return; }
  if (cp == '\t') { out.push_back(' '); return; }
  if (cp < 0x20 || cp == 0x7F) return;  // other controls: drop
  if (cp <= 0x7E) { out.push_back(static_cast<char>(cp)); return; }

  switch (cp) {
    case 0x00A0: out.push_back(' '); return;                      // NBSP
    case 0x00AB: case 0x00BB:                                     // « »
    case 0x201C: case 0x201D: case 0x201E: case 0x2033:           // “ ” „ ″
      out.push_back('"'); return;
    case 0x2018: case 0x2019: case 0x201A: case 0x2032:           // ‘ ’ ‚ ′
      out.push_back('\''); return;
    case 0x2010: case 0x2011: case 0x2012: case 0x2013:           // hyphens
    case 0x2014: case 0x2015: case 0x2212: case 0x2022:           // — ― − •
    case 0x00B7:                                                  // ·
      out.push_back('-'); return;
    case 0x2026: out.append("..."); return;                       // …
    case 0x00D7: out.push_back('x'); return;                      // ×
    case 0x00F7: out.push_back('/'); return;                      // ÷
    case 0x00B0: out.push_back('*'); return;                      // °
    default: break;
  }
  if (cp >= 0x00C0 && cp <= 0x00FF) {
    out.append(kLatin1[cp - 0x00C0]);
    return;
  }
  // Width-zero characters vanish: combining marks, zero-width spaces/joiners,
  // BiDi marks, variation selectors, BOM, emoji skin-tone modifiers.
  if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x200B && cp <= 0x200F) ||
      (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF ||
      (cp >= 0x1F3FB && cp <= 0x1F3FF)) {
    return;
  }
  out.push_back(kFallbackChar);  // emoji, CJK, everything else: one box
}

}  // namespace

std::string sanitizeForDisplay(const std::string& utf8, bool keepIncompleteTail,
                               size_t* pendingBytes) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(utf8.data());
  const size_t n = utf8.size();
  std::string out;
  out.reserve(n);
  if (pendingBytes) *pendingBytes = 0;
  size_t i = 0;
  while (i < n) {
    const Decoded d = decodeOne(p + i, n - i);
    if (d.incomplete) {
      if (keepIncompleteTail) {
        if (pendingBytes) *pendingBytes = n - i;
      } else {
        out.push_back(kFallbackChar);  // truncated trailing sequence
      }
      break;
    }
    if (!d.valid) {
      out.push_back(kFallbackChar);
      i += d.len;
      continue;
    }
    appendMapped(out, d.cp);
    i += d.len;
  }
  return out;
}

}  // namespace phoenix
