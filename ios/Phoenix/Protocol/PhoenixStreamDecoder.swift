import Foundation

/// Byte-stream reassembler mirroring core's StreamDecoder. Feed arbitrary
/// BLE chunks; poll() yields whole frames. Same resync/CRC/oversize rules as
/// PROTOCOL.md §4 — must never crash or over-read on hostile input (the app
/// side matters less than the firmware, but the phone parses radio bytes too).
final class PhoenixStreamDecoder {
    struct Stats: Equatable {
        var framesOK = 0
        var crcErrors = 0
        var resyncs = 0
        var oversizeLength = 0
        var bytesDropped = 0
    }

    private var buffer = [UInt8]()
    private var ready = [PhoenixFrame]()
    private(set) var stats = Stats()

    private static let bufferCap = 4096

    func feed(_ data: [UInt8]) {
        guard !data.isEmpty else { return }
        var incoming = data
        if incoming.count >= Self.bufferCap {
            stats.bytesDropped += buffer.count + incoming.count - Self.bufferCap
            stats.resyncs += 1
            buffer.removeAll(keepingCapacity: true)
            incoming = Array(incoming.suffix(Self.bufferCap))
        } else if buffer.count + incoming.count > Self.bufferCap {
            let excess = buffer.count + incoming.count - Self.bufferCap
            stats.bytesDropped += excess
            stats.resyncs += 1
            buffer.removeFirst(excess)
        }
        buffer.append(contentsOf: incoming)
        parse()
    }

    func feed(_ data: Data) { feed([UInt8](data)) }

    func poll() -> PhoenixFrame? {
        guard !ready.isEmpty else { return nil }
        return ready.removeFirst()
    }

    private func parse() {
        var pos = 0
        let n = buffer.count
        while pos < n {
            if buffer[pos] != PhoenixProto.startByte {
                pos += 1
                stats.bytesDropped += 1
                continue
            }
            if n - pos < PhoenixProto.headerSize { break }
            let len = Int(buffer[pos + 4]) | (Int(buffer[pos + 5]) << 8)
            if len > PhoenixProto.maxPayload {
                stats.oversizeLength += 1
                stats.resyncs += 1
                pos += 1
                stats.bytesDropped += 1
                continue
            }
            let total = PhoenixProto.headerSize + len + PhoenixProto.crcSize
            if n - pos < total { break }
            let want = UInt16(buffer[pos + total - 2]) | (UInt16(buffer[pos + total - 1]) << 8)
            let got = PhoenixProto.crc16(buffer[(pos + 1)..<(pos + PhoenixProto.headerSize + len)])
            if want != got {
                stats.crcErrors += 1
                stats.resyncs += 1
                pos += 1
                stats.bytesDropped += 1
                continue
            }
            let type = PhoenixProto.MsgType(rawValue: buffer[pos + 1])
            let payload = Array(buffer[(pos + PhoenixProto.headerSize)..<(pos + PhoenixProto.headerSize + len)])
            if let type {
                ready.append(PhoenixFrame(type: type, seq: buffer[pos + 2],
                                          flags: buffer[pos + 3], payload: payload))
                stats.framesOK += 1
            }
            // Unknown types verify structurally then drop, exactly like core.
            pos += total
        }
        if pos > 0 { buffer.removeFirst(pos) }
    }
}
