import Foundation
import Combine

/// Development transport: virtual glasses in-process. CoreBluetooth is dead in
/// the iOS Simulator, so this is what makes the app runnable end to end with no
/// hardware — it reassembles the same wire bytes through the same decoder,
/// drives a GlassesSimulation, and answers like the firmware would (ACKs on
/// PING, battery reports, button events on demand).
final class FakeTransport: PhoenixTransport {
    private let stateSubject = CurrentValueSubject<TransportState, Never>(.idle)
    private let framesSubject = PassthroughSubject<PhoenixFrame, Never>()
    private let logSubject = PassthroughSubject<FrameLog, Never>()

    var statePublisher: AnyPublisher<TransportState, Never> { stateSubject.eraseToAnyPublisher() }
    var incomingFrames: AnyPublisher<PhoenixFrame, Never> { framesSubject.eraseToAnyPublisher() }
    var frameLog: AnyPublisher<FrameLog, Never> { logSubject.eraseToAnyPublisher() }
    var state: TransportState { stateSubject.value }

    /// The virtual device's display state, published for the glasses view.
    let glasses = GlassesSimulation()
    private let glassesSubject = PassthroughSubject<Void, Never>()
    var glassesDidChange: AnyPublisher<Void, Never> { glassesSubject.eraseToAnyPublisher() }

    private let decoder = PhoenixStreamDecoder()
    private var seqOut: UInt8 = 0
    private var deviceSeq: UInt8 = 0
    private var batteryPercent: UInt8 = 92
    private var batteryMillivolts: UInt16 = 4010
    private var timer: Timer?

    /// Simulated MTU: iOS typically lands at 185 for a modern iPhone, so the
    /// app's chunking path gets exercised rather than bypassed.
    var simulatedMTU = 185

    func start() {
        stateSubject.send(.scanning)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { [weak self] in
            guard let self, !self.state.isConnected else { return }
            self.stateSubject.send(.connecting)
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.9) { [weak self] in
            guard let self else { return }
            self.stateSubject.send(.connected)
            self.deviceEmit(PhoenixFrame(type: .batteryStatus,
                                         payload: [self.batteryPercent,
                                                   UInt8(self.batteryMillivolts & 0xFF),
                                                   UInt8(self.batteryMillivolts >> 8)]))
            self.startTicking()
        }
    }

    func stop() {
        timer?.invalidate()
        timer = nil
        stateSubject.send(.disconnected(reason: "stopped"))
    }

    func send(_ frame: PhoenixFrame) {
        var f = frame
        f.seq = seqOut
        seqOut &+= 1
        guard let bytes = f.encoded() else { return }
        logSubject.send(FrameLog(date: Date(), direction: .tx, bytes: bytes,
                                 summary: PhoenixMessages.describe(f)))
        guard state.isConnected else { return }

        // Deliver in MTU-sized chunks through the real decoder, so split
        // frames and reassembly are genuinely exercised.
        let chunk = max(1, simulatedMTU - 3)
        var i = 0
        while i < bytes.count {
            let n = min(chunk, bytes.count - i)
            decoder.feed(Array(bytes[i..<(i + n)]))
            i += n
        }
        while let received = decoder.poll() { deviceHandle(received) }
    }

    /// Simulates a physical button press on the glasses.
    func pressButton(_ button: UInt8, long: Bool = false) {
        guard state.isConnected else { return }
        deviceEmit(PhoenixFrame(type: .buttonEvent, payload: [button, long ? 1 : 0]))
        if button == 1 {  // A dismisses whatever is showing
            glasses.apply(PhoenixFrame(type: .clear, payload: [PhoenixProto.clearAll]))
            glassesSubject.send(())
        }
    }

    /// Simulates the pack draining, so battery UI and thresholds can be seen.
    func setBattery(percent: UInt8, millivolts: UInt16) {
        batteryPercent = percent
        batteryMillivolts = millivolts
        guard state.isConnected else { return }
        deviceEmit(PhoenixFrame(type: .batteryStatus,
                                payload: [percent, UInt8(millivolts & 0xFF), UInt8(millivolts >> 8)]))
    }

    // MARK: virtual device side

    private func deviceHandle(_ frame: PhoenixFrame) {
        glasses.apply(frame)
        glassesSubject.send(())

        if frame.type == .ping {
            let ok = frame.payload.first == PhoenixProto.protocolVersion
            deviceEmit(PhoenixFrame(type: .ack,
                                    payload: [frame.seq, ok ? PhoenixProto.ackOK : PhoenixProto.ackBadVersion]))
        } else if frame.flags & PhoenixProto.flagAckReq != 0 {
            deviceEmit(PhoenixFrame(type: .ack, payload: [frame.seq, PhoenixProto.ackOK]))
        }
    }

    private func deviceEmit(_ frame: PhoenixFrame) {
        var f = frame
        f.seq = deviceSeq
        deviceSeq &+= 1
        let bytes = f.encoded() ?? []
        logSubject.send(FrameLog(date: Date(), direction: .rx, bytes: bytes,
                                 summary: PhoenixMessages.describe(f)))
        framesSubject.send(f)
    }

    private func startTicking() {
        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            guard let self else { return }
            self.glasses.advance()
            self.glassesSubject.send(())
        }
    }
}
