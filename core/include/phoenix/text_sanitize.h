#pragma once

#include <cstddef>
#include <string>

namespace phoenix {

// The renderer's fallback glyph slot (a hollow box in the body font). Every
// unsupported codepoint sanitizes to this byte.
inline constexpr char kFallbackChar = '\x7F';

// Converts arbitrary (possibly hostile) UTF-8 into the display byte set:
// printable ASCII 0x20-0x7E, '\n', and kFallbackChar. Implements the policy
// documented in PROTOCOL.md §6:
//   - invalid bytes -> fallback glyph, consume one byte, keep going
//   - common typography transliterated ('smart' quotes, dashes, ellipsis, ...)
//   - Latin-1 letters stripped to their base letter (é -> e, ß -> ss, ...)
//   - zero-width characters, combining marks, variation selectors dropped
//   - everything else (emoji, CJK, symbols) -> one fallback glyph
//   - '\n' preserved, '\t' -> space, other control characters dropped
//
// keepIncompleteTail: when true, a truncated UTF-8 sequence at the very end of
// the input is left unconsumed instead of emitted as fallback glyphs, and its
// byte count is reported via *pendingBytes. Assistant streaming re-sanitizes
// its growing buffer with this set so a codepoint split across BLE chunks
// never flashes as garbage.
std::string sanitizeForDisplay(const std::string& utf8,
                               bool keepIncompleteTail = false,
                               size_t* pendingBytes = nullptr);

}  // namespace phoenix
