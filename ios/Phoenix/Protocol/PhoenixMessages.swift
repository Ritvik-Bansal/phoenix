import Foundation

/// Typed builders and parsers over PhoenixFrame — the phone's half of the
/// message set (PROTOCOL.md §5). Builders assign seq at send time.
enum PhoenixMessages {
    // MARK: Builders (phone -> glasses)

    static func assistantText(_ text: String) -> PhoenixFrame {
        PhoenixFrame(type: .assistantText, payload: [UInt8](text.utf8))
    }

    static func assistantChunk(_ text: String, final: Bool) -> PhoenixFrame {
        PhoenixFrame(type: .assistantStreamChunk,
                     flags: final ? PhoenixProto.flagStreamFinal : 0,
                     payload: [UInt8](text.utf8))
    }

    static func navUpdate(_ maneuver: PhoenixProto.Maneuver, meters: UInt16,
                          street: String) -> PhoenixFrame {
        var payload: [UInt8] = [maneuver.rawValue, UInt8(meters & 0xFF), UInt8((meters >> 8) & 0xFF)]
        payload.append(contentsOf: street.utf8)
        return PhoenixFrame(type: .navUpdate, payload: payload)
    }

    static func clear(_ mask: UInt8) -> PhoenixFrame {
        PhoenixFrame(type: .clear, payload: [mask])
    }

    static func setBrightness(_ level: UInt8) -> PhoenixFrame {
        PhoenixFrame(type: .setBrightness, payload: [level])
    }

    static func ping(version: UInt8 = PhoenixProto.protocolVersion) -> PhoenixFrame {
        PhoenixFrame(type: .ping, payload: [version])
    }

    // MARK: Parsers (glasses -> phone)

    struct Ack: Equatable { var ackedSeq: UInt8; var status: UInt8 }
    struct ButtonEvent: Equatable { var button: UInt8; var action: UInt8 }
    struct BatteryStatus: Equatable { var percent: UInt8; var millivolts: UInt16 }

    static func parseAck(_ f: PhoenixFrame) -> Ack? {
        guard f.type == .ack, f.payload.count == 2 else { return nil }
        return Ack(ackedSeq: f.payload[0], status: f.payload[1])
    }

    static func parseButtonEvent(_ f: PhoenixFrame) -> ButtonEvent? {
        guard f.type == .buttonEvent, f.payload.count == 2 else { return nil }
        return ButtonEvent(button: f.payload[0], action: f.payload[1])
    }

    static func parseBatteryStatus(_ f: PhoenixFrame) -> BatteryStatus? {
        guard f.type == .batteryStatus, f.payload.count == 3 else { return nil }
        return BatteryStatus(percent: f.payload[0],
                             millivolts: UInt16(f.payload[1]) | (UInt16(f.payload[2]) << 8))
    }

    /// Human-readable one-liner for the debug console (both directions).
    static func describe(_ f: PhoenixFrame) -> String {
        let flags = f.flags == 0 ? "" :
            " [" + [
                f.flags & PhoenixProto.flagAckReq != 0 ? "ackreq" : nil,
                f.flags & PhoenixProto.flagStreamFinal != 0 ? "final" : nil,
            ].compactMap { $0 }.joined(separator: ",") + "]"
        switch f.type {
        case .assistantText: return "ASSISTANT_TEXT \"\(f.payloadString)\"\(flags)"
        case .assistantStreamChunk: return "CHUNK \"\(f.payloadString)\"\(flags)"
        case .navUpdate:
            guard f.payload.count >= 3 else { return "NAV_UPDATE <malformed>" }
            let m = PhoenixProto.Maneuver(rawValue: f.payload[0]) ?? .straight
            let meters = UInt16(f.payload[1]) | (UInt16(f.payload[2]) << 8)
            let street = String(decoding: f.payload[3...], as: UTF8.self)
            return "NAV_UPDATE \(m) \(meters)m \"\(street)\""
        case .clear: return "CLEAR 0x\(String(f.payload.first ?? 0, radix: 16))"
        case .setBrightness: return "SET_BRIGHTNESS \(f.payload.first ?? 0)"
        case .ping: return "PING v\(f.payload.first ?? 0)"
        case .ack:
            if let a = parseAck(f) { return "ACK seq=\(a.ackedSeq) status=\(a.status)" }
            return "ACK <malformed>"
        case .buttonEvent:
            if let b = parseButtonEvent(f) {
                let name = ["?", "A", "B", "C"]
                let btn = Int(b.button) < name.count ? name[Int(b.button)] : "?"
                return "BUTTON_EVENT \(btn)\(b.action == 1 ? " long" : "")"
            }
            return "BUTTON_EVENT <malformed>"
        case .batteryStatus:
            if let s = parseBatteryStatus(f) { return "BATTERY \(s.percent)% \(s.millivolts)mV" }
            return "BATTERY <malformed>"
        }
    }
}
