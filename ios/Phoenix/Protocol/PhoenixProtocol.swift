import Foundation

/// Swift mirror of the Phoenix wire format (PROTOCOL.md / core/protocol.h).
/// Kept byte-for-byte compatible with the C++ core; cross-checked against
/// C++-generated fixtures in PhoenixProtocolTests.
enum PhoenixProto {
    static let startByte: UInt8 = 0xA5
    static let maxPayload = 512
    static let headerSize = 6
    static let crcSize = 2
    static let protocolVersion: UInt8 = 1

    // GATT identity (PROTOCOL.md §1).
    static let serviceUUID = "CD310001-0101-4B2F-9456-982A27ED3560"
    static let txUUID = "CD310002-0101-4B2F-9456-982A27ED3560"
    static let rxUUID = "CD310003-0101-4B2F-9456-982A27ED3560"

    enum MsgType: UInt8 {
        case assistantText = 0x01
        case assistantStreamChunk = 0x02
        case navUpdate = 0x03
        case clear = 0x04
        case setBrightness = 0x05
        case ping = 0x06
        case ack = 0x40
        case buttonEvent = 0x41
        case batteryStatus = 0x42
    }

    static let flagAckReq: UInt8 = 0x01
    static let flagStreamFinal: UInt8 = 0x02

    static let clearAssistant: UInt8 = 0x01
    static let clearNav: UInt8 = 0x02
    static let clearNotification: UInt8 = 0x04
    static let clearAll: UInt8 = 0xFF

    static let ackOK: UInt8 = 0
    static let ackBadVersion: UInt8 = 1
    static let ackBadType: UInt8 = 2

    enum Maneuver: UInt8 {
        case straight = 0, left = 1, right = 2, slightLeft = 3, slightRight = 4
        case sharpLeft = 5, sharpRight = 6, uturn = 7, arrive = 8
    }
    static let distanceUnknown: UInt16 = 0xFFFF

    /// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR.
    static func crc16<S: Sequence>(_ bytes: S, initial: UInt16 = 0xFFFF) -> UInt16
    where S.Element == UInt8 {
        var crc = initial
        for b in bytes {
            crc ^= UInt16(b) << 8
            for _ in 0..<8 {
                if crc & 0x8000 != 0 {
                    crc = (crc << 1) ^ 0x1021
                } else {
                    crc <<= 1
                }
            }
        }
        return crc
    }
}

struct PhoenixFrame: Equatable {
    var type: PhoenixProto.MsgType
    var seq: UInt8
    var flags: UInt8
    var payload: [UInt8]

    init(type: PhoenixProto.MsgType, seq: UInt8 = 0, flags: UInt8 = 0, payload: [UInt8] = []) {
        self.type = type
        self.seq = seq
        self.flags = flags
        self.payload = payload
    }

    /// Serializes start byte through CRC. Returns nil for an over-max payload.
    func encoded() -> [UInt8]? {
        guard payload.count <= PhoenixProto.maxPayload else { return nil }
        var out = [UInt8]()
        out.reserveCapacity(PhoenixProto.headerSize + payload.count + PhoenixProto.crcSize)
        out.append(PhoenixProto.startByte)
        out.append(type.rawValue)
        out.append(seq)
        out.append(flags)
        out.append(UInt8(payload.count & 0xFF))
        out.append(UInt8((payload.count >> 8) & 0xFF))
        out.append(contentsOf: payload)
        let crc = PhoenixProto.crc16(out[1...])
        out.append(UInt8(crc & 0xFF))
        out.append(UInt8((crc >> 8) & 0xFF))
        return out
    }

    var payloadString: String {
        String(decoding: payload, as: UTF8.self)
    }
}
