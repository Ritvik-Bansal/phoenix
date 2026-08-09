import Foundation
import Combine

/// The seam that makes the whole app runnable in the Simulator. CoreBluetooth
/// does not function there, so everything above this protocol is written
/// against it, and development uses FakeTransport with no device present.
enum TransportState: Equatable {
    case idle
    case scanning
    case connecting
    case connected           // discovered + subscribed, ready for traffic
    case disconnected(reason: String?)

    var isConnected: Bool { if case .connected = self { return true }; return false }

    var label: String {
        switch self {
        case .idle: return "Idle"
        case .scanning: return "Scanning…"
        case .connecting: return "Connecting…"
        case .connected: return "Connected"
        case .disconnected(let reason): return reason.map { "Disconnected: \($0)" } ?? "Disconnected"
        }
    }
}

/// One decoded direction of a frame, tagged for the debug console.
struct FrameLog: Identifiable {
    enum Direction { case tx, rx }
    let id = UUID()
    let date: Date
    let direction: Direction
    let bytes: [UInt8]
    let summary: String
}

protocol PhoenixTransport: AnyObject {
    var statePublisher: AnyPublisher<TransportState, Never> { get }
    /// Whole reassembled frames arriving from the glasses (RX characteristic).
    var incomingFrames: AnyPublisher<PhoenixFrame, Never> { get }
    /// Raw byte-level traffic in both directions, for the debug console.
    var frameLog: AnyPublisher<FrameLog, Never> { get }

    var state: TransportState { get }

    func start()
    func stop()
    /// Serialize + chunk a frame out over TX. Assigns the sequence number.
    func send(_ frame: PhoenixFrame)
}
