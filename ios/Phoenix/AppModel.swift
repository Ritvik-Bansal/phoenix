import Foundation
import Combine
import SwiftUI

/// App-wide state: owns the transport, the assistant pipeline, and the debug
/// log. Written against PhoenixTransport so the Simulator runs the whole app
/// on FakeTransport with no hardware.
@MainActor
final class AppModel: ObservableObject {
    @Published private(set) var transportState: TransportState = .idle
    @Published private(set) var frames: [FrameLog] = []
    @Published private(set) var assistantReply = ""
    @Published private(set) var isStreaming = false
    @Published private(set) var lastError: String?
    @Published private(set) var battery: PhoenixMessages.BatteryStatus?
    @Published private(set) var lastButton: String?
    @Published var usingFakeTransport: Bool
    /// Bumped whenever the virtual glasses redraw, to refresh the view.
    @Published private(set) var glassesRevision = 0

    let transport: PhoenixTransport
    let speech = SpeechRecognizer()
    private let llm: LLMClienting
    private var cancellables = Set<AnyCancellable>()

    /// Backing simulation for the virtual glasses view. With FakeTransport
    /// this IS the virtual device; with BleTransport it mirrors what we sent
    /// so the on-screen preview still tracks the real display.
    let glasses: GlassesSimulation
    private var mirrorTimer: Timer?

    var isOfflineLLM: Bool { llm.isOffline }

    init(transport: PhoenixTransport? = nil, llm: LLMClienting = LLMClient()) {
        // CoreBluetooth cannot work in the Simulator; default to the fake
        // transport there so the app is fully usable during development.
        #if targetEnvironment(simulator)
        let useFake = true
        #else
        let useFake = false
        #endif
        let chosen = transport ?? (useFake ? FakeTransport() : BleTransport())
        self.transport = chosen
        self.usingFakeTransport = chosen is FakeTransport
        self.llm = llm
        self.glasses = (chosen as? FakeTransport)?.glasses ?? GlassesSimulation()

        chosen.statePublisher
            .receive(on: DispatchQueue.main)
            .sink { [weak self] in self?.transportState = $0 }
            .store(in: &cancellables)

        chosen.frameLog
            .receive(on: DispatchQueue.main)
            .sink { [weak self] log in
                guard let self else { return }
                self.frames.insert(log, at: 0)
                if self.frames.count > 300 { self.frames.removeLast(self.frames.count - 300) }
            }
            .store(in: &cancellables)

        chosen.incomingFrames
            .receive(on: DispatchQueue.main)
            .sink { [weak self] frame in self?.handleIncoming(frame) }
            .store(in: &cancellables)

        if let fake = chosen as? FakeTransport {
            fake.glassesDidChange
                .receive(on: DispatchQueue.main)
                .sink { [weak self] in self?.glassesRevision &+= 1 }
                .store(in: &cancellables)
        } else {
            // Real transport: drive the preview's animation clock locally.
            mirrorTimer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
                Task { @MainActor in
                    guard let self else { return }
                    self.glasses.advance()
                    self.glassesRevision &+= 1
                }
            }
        }
    }

    func start() { transport.start() }
    func stop() { transport.stop() }

    private func handleIncoming(_ frame: PhoenixFrame) {
        if let b = PhoenixMessages.parseBatteryStatus(frame) { battery = b }
        if let btn = PhoenixMessages.parseButtonEvent(frame) {
            let names = ["?", "A", "B", "C"]
            let name = Int(btn.button) < names.count ? names[Int(btn.button)] : "?"
            lastButton = "\(name)\(btn.action == 1 ? " (long)" : "")"
        }
    }

    /// Sends a frame and mirrors it into the local preview when the real
    /// transport is in use (FakeTransport already renders it itself).
    private func dispatch(_ frame: PhoenixFrame) {
        transport.send(frame)
        if !usingFakeTransport {
            glasses.apply(frame)
            glassesRevision &+= 1
        }
    }

    // MARK: assistant pipeline

    /// Full path: transcript -> LLM (streaming) -> chunk frames -> glasses.
    func ask(_ prompt: String) async {
        guard !prompt.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return }
        isStreaming = true
        assistantReply = ""
        lastError = nil
        var sentAny = false

        do {
            try await llm.stream(prompt: prompt) { [weak self] chunk in
                Task { @MainActor in
                    guard let self else { return }
                    self.assistantReply += chunk
                    self.dispatch(PhoenixMessages.assistantChunk(chunk, final: false))
                    sentAny = true
                }
            }
        } catch {
            lastError = error.localizedDescription
        }

        // Always close the stream so the display stops showing a cursor and
        // starts paging, even if the request failed mid-flight.
        if !sentAny && assistantReply.isEmpty {
            let message = lastError ?? "No reply."
            assistantReply = message
            dispatch(PhoenixMessages.assistantChunk(message, final: false))
        }
        dispatch(PhoenixMessages.assistantChunk("", final: true))
        isStreaming = false
    }

    func startListening() async {
        guard await speech.requestAuthorization() else { return }
        do {
            try speech.start()
        } catch {
            lastError = error.localizedDescription
        }
    }

    func stopListeningAndAsk() async {
        let text = speech.stop()
        guard !text.isEmpty else { return }
        await ask(text)
    }

    // MARK: direct controls

    func sendClear() { dispatch(PhoenixMessages.clear(PhoenixProto.clearAll)); assistantReply = "" }
    func sendPing() { dispatch(PhoenixMessages.ping()) }
    func setBrightness(_ level: UInt8) { dispatch(PhoenixMessages.setBrightness(level)) }

    func sendDemoNav() {
        dispatch(PhoenixMessages.navUpdate(.left, meters: 250, street: "Market St"))
    }

    func pressVirtualButton(_ index: UInt8, long: Bool = false) {
        (transport as? FakeTransport)?.pressButton(index, long: long)
    }

    func clearLog() { frames.removeAll() }
}
