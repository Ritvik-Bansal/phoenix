import Foundation

/// Interprets the Phoenix frame stream into a 72x40 render, the way the real
/// glasses' screen manager would — but only the pieces the app can drive
/// (assistant reply, nav, brightness, clear). It backs both the virtual
/// glasses view and FakeTransport's loopback. Tick-advance paging keeps it
/// lively without pulling in the full C++ screen stack.
final class GlassesSimulation {
    enum Screen: Equatable {
        case clock
        case assistant(text: String, streaming: Bool)
        case nav(maneuver: PhoenixProto.Maneuver, meters: UInt16, street: String)
    }

    private(set) var screen: Screen = .clock
    private(set) var brightness: UInt8 = 255
    private var assistantBuffer = ""
    private var assistantStreaming = false
    private var tick: UInt32 = 0

    /// Fold one inbound frame into display state.
    func apply(_ frame: PhoenixFrame) {
        switch frame.type {
        case .assistantText:
            assistantStreaming = false
            assistantBuffer = frame.payloadString
            screen = assistantBuffer.isEmpty ? .clock
                : .assistant(text: assistantBuffer, streaming: false)
            tick = 0
        case .assistantStreamChunk:
            if !assistantStreaming { assistantBuffer = ""; assistantStreaming = true }
            assistantBuffer += frame.payloadString
            if frame.flags & PhoenixProto.flagStreamFinal != 0 { assistantStreaming = false; tick = 0 }
            screen = .assistant(text: assistantBuffer, streaming: assistantStreaming)
        case .navUpdate:
            guard frame.payload.count >= 3 else { return }
            let m = PhoenixProto.Maneuver(rawValue: frame.payload[0]) ?? .straight
            let meters = UInt16(frame.payload[1]) | (UInt16(frame.payload[2]) << 8)
            let street = String(decoding: frame.payload[3...], as: UTF8.self)
            screen = .nav(maneuver: m, meters: meters, street: street)
        case .clear:
            let mask = frame.payload.first ?? 0
            if mask & PhoenixProto.clearAssistant != 0, case .assistant = screen { screen = .clock }
            if mask & PhoenixProto.clearNav != 0, case .nav = screen { screen = .clock }
            if mask == PhoenixProto.clearAll { screen = .clock }
        case .setBrightness:
            brightness = frame.payload.first ?? 255
        default:
            break
        }
    }

    func advance() { tick &+= 1 }

    /// Renders the current screen into a fresh framebuffer.
    func render(clock: String = "09:41", date: String = "Fri Aug 7",
                battery: Int = 92, connected: Bool = true) -> GlassesFrameBuffer {
        var fb = GlassesFrameBuffer()
        switch screen {
        case .clock:
            renderClock(&fb, clock: clock, date: date, battery: battery, connected: connected)
        case .assistant(let text, let streaming):
            renderAssistant(&fb, text: text, streaming: streaming)
        case .nav(let maneuver, let meters, let street):
            renderNav(&fb, maneuver: maneuver, meters: meters, street: street)
        }
        return fb
    }

    private func centered(_ font: PhoenixFont, _ s: String) -> Int {
        let w = PhoenixText.measure(font, s)
        return w >= GlassesFrameBuffer.width ? 0 : (GlassesFrameBuffer.width - w) / 2
    }

    private func renderClock(_ fb: inout GlassesFrameBuffer, clock: String, date: String,
                             battery: Int, connected: Bool) {
        var t = clock
        if (tick / 5) % 2 == 1, t.count == 5 {
            t = String(Array(t).enumerated().map { $0.offset == 2 ? " " : $0.element })
        }
        PhoenixText.draw(&fb, PhoenixFontData.clock, 12, 3, t)
        PhoenixText.draw(&fb, PhoenixFontData.body, centered(PhoenixFontData.body, date), 22, date)
        let pct = "\(battery)%"
        PhoenixText.draw(&fb, PhoenixFontData.body,
                         58 - PhoenixText.measure(PhoenixFontData.body, pct), 32, pct)
        drawBattery(&fb, 60, 32, battery)
    }

    private func renderAssistant(_ fb: inout GlassesFrameBuffer, text: String, streaming: Bool) {
        let lines = PhoenixText.wrap(PhoenixFontData.body, text, width: GlassesFrameBuffer.width)
        let perPage = 4
        let pageCount = max(1, (lines.count + perPage - 1) / perPage)
        let page = streaming ? pageCount - 1 : Int(tick / 30) % pageCount
        let first = page * perPage
        var lastW = 0, lastY = 1
        for i in 0..<perPage {
            let idx = first + i
            guard idx < lines.count else { break }
            lastY = 1 + i * 8
            lastW = PhoenixText.draw(&fb, PhoenixFontData.body, 0, lastY, lines[idx])
        }
        if let spark = PhoenixFontData.sprites["ICON_SPARK"] { blit(&fb, spark, 0, 32) }
        if streaming {
            if (tick / 3) % 2 == 0 {
                let cx = min(lastW + 2, 68)
                for dy in 0..<6 { for dx in 0..<3 { fb.set(cx + dx, lastY + dy) } }
            }
        } else if pageCount > 1 {
            let ind = "\(page + 1)/\(pageCount)"
            PhoenixText.draw(&fb, PhoenixFontData.body,
                             GlassesFrameBuffer.width - PhoenixText.measure(PhoenixFontData.body, ind),
                             33, ind)
        }
    }

    private func renderNav(_ fb: inout GlassesFrameBuffer, maneuver: PhoenixProto.Maneuver,
                           meters: UInt16, street: String) {
        let arrow = ["ARROW_STRAIGHT", "ARROW_LEFT", "ARROW_RIGHT", "ARROW_SLIGHT_LEFT",
                     "ARROW_SLIGHT_RIGHT", "ARROW_SHARP_LEFT", "ARROW_SHARP_RIGHT",
                     "ARROW_UTURN", "ARROW_ARRIVE"][Int(maneuver.rawValue)]
        if let s = PhoenixFontData.sprites[arrow] { blit(&fb, s, 0, 4) }
        if maneuver == .arrive {
            PhoenixText.draw(&fb, PhoenixFontData.body, 22, 8, "Arrived")
        } else if meters != PhoenixProto.distanceUnknown {
            let (big, unit) = Self.distance(meters)
            let w = PhoenixText.draw(&fb, PhoenixFontData.clock, 20, 2, big)
            PhoenixText.draw(&fb, PhoenixFontData.body, 20 + w + 1, 11, unit)
        }
        // Street: truncate to a single line here (marquee lives on-device).
        PhoenixText.draw(&fb, PhoenixFontData.body, 0, 28, street)
    }

    static func distance(_ meters: UInt16) -> (String, String) {
        if meters == PhoenixProto.distanceUnknown { return ("", "") }
        if meters < 1000 { return ("\(meters)", "m") }
        if meters < 10000 { return ("\(meters / 1000).\((meters % 1000) / 100)", "km") }
        return ("\(meters / 1000)", "km")
    }

    private func drawBattery(_ fb: inout GlassesFrameBuffer, _ x: Int, _ y: Int, _ percent: Int) {
        for i in 0..<10 { fb.set(x + i, y); fb.set(x + i, y + 5) }
        for j in 0..<6 { fb.set(x, y + j); fb.set(x + 9, y + j) }
        fb.set(x + 10, y + 2); fb.set(x + 10, y + 3)
        let fill = (max(0, min(100, percent)) * 8 + 50) / 100
        for i in 0..<fill { for j in 0..<4 { fb.set(x + 1 + i, y + 1 + j) } }
    }

    private func blit(_ fb: inout GlassesFrameBuffer, _ sprite: PhoenixSprite, _ x: Int, _ y: Int) {
        let bytesPerRow = (sprite.width + 7) / 8
        for row in 0..<sprite.height {
            for col in 0..<sprite.width {
                let byte = sprite.data[row * bytesPerRow + col / 8]
                if byte & (0x80 >> (col & 7)) != 0 { fb.set(x + col, y + row) }
            }
        }
    }
}
