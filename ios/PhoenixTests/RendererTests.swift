import XCTest
@testable import Phoenix

/// The Swift renderer shares generated font data with the firmware, so these
/// pin the shared behavior: glyph metrics, wrapping, and the UTF-8 fallback
/// policy from PROTOCOL.md §6.
final class RendererTests: XCTestCase {

    func testGeneratedFontMatchesCoreMetrics() {
        let body = PhoenixFontData.body
        XCTAssertEqual(body.height, 7)
        XCTAssertEqual(body.tracking, 1)
        XCTAssertEqual(body.fallbackChar, 0x7F)
        // Same widths the C++ tests assert (proportional 5x7).
        XCTAssertEqual(PhoenixText.measure(body, "A"), 5)
        XCTAssertEqual(PhoenixText.measure(body, "AB"), 11)
        XCTAssertEqual(PhoenixText.measure(body, "!"), 1)
        XCTAssertEqual(PhoenixText.measure(body, "il"), 7)
        XCTAssertEqual(PhoenixText.measure(body, ""), 0)

        let clock = PhoenixFontData.clock
        XCTAssertEqual(clock.height, 16)
        XCTAssertEqual(PhoenixText.measure(clock, "09:41"), 48)
    }

    func testSanitizeMatchesProtocolPolicy() {
        func s(_ text: String) -> String {
            String(decoding: PhoenixText.sanitize(text), as: UTF8.self)
        }
        XCTAssertEqual(s("plain ASCII 123!"), "plain ASCII 123!")
        XCTAssertEqual(s("caf\u{E9}"), "cafe")
        XCTAssertEqual(s("\u{2019}tis"), "'tis")
        XCTAssertEqual(s("\u{201C}q\u{201D}"), "\"q\"")
        XCTAssertEqual(s("a\u{2014}b"), "a-b")
        XCTAssertEqual(s("wait\u{2026}"), "wait...")
        XCTAssertEqual(s("stra\u{DF}e"), "strasse")
        XCTAssertEqual(s("72\u{B0}"), "72*")
        XCTAssertEqual(s("e\u{301}"), "e")            // combining mark dropped
        XCTAssertEqual(s("a\u{200D}b"), "ab")         // zero-width joiner
        XCTAssertEqual(s("x\u{7}y"), "xy")            // control dropped
        XCTAssertEqual(s("a\tb"), "a b")
        XCTAssertEqual(s("a\nb"), "a\nb")             // newline preserved
    }

    func testEmojiBecomesFallbackGlyphNotACrash() {
        let box = String(UnicodeScalar(0x7F)!)
        func s(_ text: String) -> String {
            String(decoding: PhoenixText.sanitize(text), as: UTF8.self)
        }
        XCTAssertEqual(s("\u{1F525}"), box)                    // 🔥
        XCTAssertEqual(s("ok \u{1F44D} done"), "ok \(box) done")
        XCTAssertEqual(s("\u{1F44D}\u{1F3FB}"), box)           // skin tone vanishes
        // And it renders without trapping.
        var fb = GlassesFrameBuffer()
        PhoenixText.draw(&fb, PhoenixFontData.body, 0, 0, "\u{1F525}\u{4E2D}\u{6587}")
        XCTAssertTrue(fb.pixels.contains(true))
    }

    func testWrapRespectsWidthAndPreservesText() {
        let body = PhoenixFontData.body
        let text = "Meet me at the coffee shop on Market Street at noon"
        for width in [20, 30, 40, 72] {
            let lines = PhoenixText.wrap(body, text, width: width)
            var rejoined = ""
            for line in lines {
                XCTAssertLessThanOrEqual(PhoenixText.measure(body, line), width)
                rejoined += String(decoding: line, as: UTF8.self).replacingOccurrences(of: " ", with: "")
            }
            XCTAssertEqual(rejoined, text.replacingOccurrences(of: " ", with: ""))
        }
    }

    func testWrapHardBreaksOverlongWords() {
        let body = PhoenixFontData.body
        let word = "Pneumonoultramicroscopicsilicovolcanoconiosis"
        let lines = PhoenixText.wrap(body, word, width: 30)
        XCTAssertGreaterThan(lines.count, 1)
        var joined = ""
        for line in lines {
            XCTAssertFalse(line.isEmpty)
            XCTAssertLessThanOrEqual(PhoenixText.measure(body, line), 30)
            joined += String(decoding: line, as: UTF8.self)
        }
        XCTAssertEqual(joined, word)
    }

    func testWrapHandlesNewlinesAndCollapsesSpaces() {
        let body = PhoenixFontData.body
        let lines = PhoenixText.wrap(body, "a\n\nb", width: 72).map {
            String(decoding: $0, as: UTF8.self)
        }
        XCTAssertEqual(lines, ["a", "", "b"])
        let collapsed = PhoenixText.wrap(body, "a    b", width: 72).map {
            String(decoding: $0, as: UTF8.self)
        }
        XCTAssertEqual(collapsed, ["a b"])
        XCTAssertTrue(PhoenixText.wrap(body, "", width: 72).isEmpty)
    }

    func testFrameBufferClipsOutOfBounds() {
        var fb = GlassesFrameBuffer()
        fb.set(-1, 0)
        fb.set(0, -1)
        fb.set(GlassesFrameBuffer.width, 0)
        fb.set(0, GlassesFrameBuffer.height)
        XCTAssertFalse(fb.pixels.contains(true))
        fb.set(0, 0)
        XCTAssertTrue(fb.get(0, 0))
        XCTAssertFalse(fb.get(-5, -5))
    }

    func testSimulationRendersEachScreen() {
        let sim = GlassesSimulation()
        XCTAssertTrue(sim.render().pixels.contains(true), "clock should draw")

        sim.apply(PhoenixMessages.assistantText("Sunny with a high of 22C today."))
        XCTAssertTrue(sim.render().pixels.contains(true), "assistant should draw")

        sim.apply(PhoenixMessages.navUpdate(.slightRight, meters: 400, street: "Folsom St"))
        XCTAssertTrue(sim.render().pixels.contains(true), "nav should draw")

        sim.apply(PhoenixMessages.setBrightness(40))
        XCTAssertEqual(sim.brightness, 40)
    }

    func testNavDistanceFormatting() {
        XCTAssertEqual(GlassesSimulation.distance(850).0, "850")
        XCTAssertEqual(GlassesSimulation.distance(850).1, "m")
        XCTAssertEqual(GlassesSimulation.distance(1250).0, "1.2")
        XCTAssertEqual(GlassesSimulation.distance(12600).0, "12")
        XCTAssertEqual(GlassesSimulation.distance(PhoenixProto.distanceUnknown).0, "")
    }
}
