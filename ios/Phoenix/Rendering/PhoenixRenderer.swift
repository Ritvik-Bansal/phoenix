import Foundation

/// Minimal Swift port of the core's framebuffer + font drawing + word wrap —
/// just enough for the virtual glasses view to render what the app sends
/// (assistant replies, nav, clock). The font data itself is the SAME bytes
/// the firmware uses, emitted by tools/gen_fonts.py into FontData.swift, so
/// glyph shapes match the device exactly. Full screen logic (priority queue,
/// marquee timing) lives in the C++ core and is exercised by the desktop
/// simulator; here we only need the glanceable result.
struct GlassesFrameBuffer {
    static let width = 72
    static let height = 40
    private(set) var pixels: [Bool]

    init() { pixels = [Bool](repeating: false, count: Self.width * Self.height) }

    mutating func clear() {
        for i in pixels.indices { pixels[i] = false }
    }

    mutating func set(_ x: Int, _ y: Int, _ on: Bool = true) {
        guard x >= 0, x < Self.width, y >= 0, y < Self.height else { return }
        pixels[y * Self.width + x] = on
    }

    func get(_ x: Int, _ y: Int) -> Bool {
        guard x >= 0, x < Self.width, y >= 0, y < Self.height else { return false }
        return pixels[y * Self.width + x]
    }
}

/// Text rendering + measurement against a PhoenixFont from FontData.swift.
enum PhoenixText {
    static func hasGlyph(_ font: PhoenixFont, _ byte: UInt8) -> Bool {
        byte >= font.firstChar && byte <= font.lastChar
            && font.glyphs[Int(byte - font.firstChar)].width > 0
    }

    static func glyph(_ font: PhoenixFont, _ byte: UInt8) -> PhoenixGlyph {
        let b = hasGlyph(font, byte) ? byte : font.fallbackChar
        return font.glyphs[Int(b - font.firstChar)]
    }

    static func advance(_ font: PhoenixFont, _ byte: UInt8) -> Int {
        glyph(font, byte).width + font.tracking
    }

    /// Exact ink+tracking width in px (no trailing tracking).
    static func measure(_ font: PhoenixFont, _ text: [UInt8]) -> Int {
        var w = 0
        for b in text { w += advance(font, b) }
        return text.isEmpty ? 0 : w - font.tracking
    }

    static func measure(_ font: PhoenixFont, _ text: String) -> Int {
        measure(font, sanitize(text))
    }

    @discardableResult
    static func draw(_ fb: inout GlassesFrameBuffer, _ font: PhoenixFont,
                     _ x: Int, _ y: Int, _ bytes: [UInt8]) -> Int {
        var cx = x
        for b in bytes {
            if cx >= GlassesFrameBuffer.width { break }
            let g = glyph(font, b)
            let bytesPerRow = (g.width + 7) / 8
            for row in 0..<font.height {
                for col in 0..<g.width {
                    let byte = font.bitmap[g.offset + row * bytesPerRow + col / 8]
                    if byte & (0x80 >> (col & 7)) != 0 {
                        fb.set(cx + col, y + row)
                    }
                }
            }
            cx += g.width + font.tracking
        }
        return cx - x
    }

    @discardableResult
    static func draw(_ fb: inout GlassesFrameBuffer, _ font: PhoenixFont,
                     _ x: Int, _ y: Int, _ text: String) -> Int {
        draw(&fb, font, x, y, sanitize(text))
    }

    /// Word wrap mirroring core layout.cpp: word boundaries, hard char break
    /// for over-long words, explicit newlines, collapsed spaces.
    static func wrap(_ font: PhoenixFont, _ text: String, width: Int) -> [[UInt8]] {
        var lines: [[UInt8]] = []
        let bytes = sanitize(text)
        if bytes.isEmpty { return lines }

        func widthOf(_ slice: ArraySlice<UInt8>) -> Int {
            var w = 0
            for b in slice { w += advance(font, b) }
            return slice.isEmpty ? 0 : w - font.tracking
        }

        var paragraph: [UInt8] = []
        func flushParagraph() {
            var cur: [UInt8] = []
            var emitted = false
            var i = 0
            while i < paragraph.count {
                if paragraph[i] == 0x20 { i += 1; continue }
                var j = i
                while j < paragraph.count && paragraph[j] != 0x20 { j += 1 }
                var word = Array(paragraph[i..<j])
                i = j
                while !word.isEmpty {
                    if cur.isEmpty {
                        if widthOf(word[...]) <= width {
                            cur = word; word = []
                        } else {
                            // hard break: longest prefix that fits, at least 1
                            var cut = 1, w = glyph(font, word[0]).width
                            while cut < word.count {
                                let nw = w + font.tracking + glyph(font, word[cut]).width
                                if nw > width { break }
                                w = nw; cut += 1
                            }
                            lines.append(Array(word[0..<cut]))
                            emitted = true
                            word.removeFirst(cut)
                        }
                    } else {
                        let candidate = cur + [0x20] + word
                        if widthOf(candidate[...]) <= width {
                            cur = candidate; word = []
                        } else {
                            lines.append(cur); emitted = true; cur = []
                        }
                    }
                }
            }
            if !cur.isEmpty || !emitted { lines.append(cur) }
            paragraph = []
        }

        for b in bytes {
            if b == 0x0A { flushParagraph() } else { paragraph.append(b) }
        }
        flushParagraph()
        return lines
    }

    /// UTF-8 -> display bytes, matching PROTOCOL.md §6 for the common cases the
    /// app produces (ASCII, smart quotes, dashes, accents, emoji -> box).
    static func sanitize(_ text: String) -> [UInt8] {
        var out: [UInt8] = []
        let fallback: UInt8 = 0x7F
        for scalar in text.unicodeScalars {
            let cp = scalar.value
            switch cp {
            case 0x0A: out.append(0x0A)
            case 0x09, 0xA0: out.append(0x20)
            case 0x20...0x7E: out.append(UInt8(cp))
            case 0x2018, 0x2019, 0x201A, 0x2032: out.append(0x27)          // ' quotes
            case 0x201C, 0x201D, 0x201E, 0x2033, 0xAB, 0xBB: out.append(0x22) // " quotes
            case 0x2010...0x2015, 0x2212, 0x2022, 0xB7: out.append(0x2D)    // dashes/bullet
            case 0x2026: out.append(contentsOf: [0x2E, 0x2E, 0x2E])         // …
            case 0xD7: out.append(0x78)                                     // ×
            case 0xF7: out.append(0x2F)                                     // ÷
            case 0xB0: out.append(0x2A)                                     // °
            case 0xC0...0xFF: out.append(contentsOf: latin1(cp))
            case 0x300...0x36F, 0x200B...0x200F, 0xFE00...0xFE0F, 0xFEFF: break // zero-width
            case 0x1F3FB...0x1F3FF: break                                   // skin-tone
            default:
                if cp < 0x20 { break } else { out.append(fallback) }
            }
        }
        return out
    }

    private static func latin1(_ cp: UInt32) -> [UInt8] {
        let table: [String] = [
            "A","A","A","A","A","A","AE","C","E","E","E","E","I","I","I","I",
            "D","N","O","O","O","O","O","x","O","U","U","U","U","Y","Th","ss",
            "a","a","a","a","a","a","ae","c","e","e","e","e","i","i","i","i",
            "d","n","o","o","o","o","o","/","o","u","u","u","u","y","th","y",
        ]
        let idx = Int(cp - 0xC0)
        return idx >= 0 && idx < table.count ? [UInt8](table[idx].utf8) : [0x7F]
    }
}
