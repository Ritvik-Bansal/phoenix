import XCTest
import Combine
@testable import Phoenix

/// The transport abstraction is what makes this app testable with no hardware:
/// these exercise FakeTransport end to end (wire bytes, chunking, virtual
/// device replies) and the app model driving it.
@MainActor
final class TransportTests: XCTestCase {
    private var cancellables = Set<AnyCancellable>()

    override func tearDown() {
        cancellables.removeAll()
        super.tearDown()
    }

    private func connectedFake(mtu: Int = 185) async throws -> FakeTransport {
        let transport = FakeTransport()
        transport.simulatedMTU = mtu
        let connected = expectation(description: "connected")
        transport.statePublisher
            .sink { if $0.isConnected { connected.fulfill() } }
            .store(in: &cancellables)
        transport.start()
        await fulfillment(of: [connected], timeout: 5)
        return transport
    }

    func testFakeTransportConnectsAndReportsBattery() async throws {
        let transport = try await connectedFake()
        XCTAssertTrue(transport.state.isConnected)

        let gotBattery = expectation(description: "battery frame")
        transport.incomingFrames
            .sink { frame in
                if PhoenixMessages.parseBatteryStatus(frame) != nil { gotBattery.fulfill() }
            }
            .store(in: &cancellables)
        transport.setBattery(percent: 64, millivolts: 3860)
        await fulfillment(of: [gotBattery], timeout: 5)
    }

    func testPingIsAckedByVirtualDevice() async throws {
        let transport = try await connectedFake()
        let acked = expectation(description: "ack")
        transport.incomingFrames
            .sink { frame in
                if let ack = PhoenixMessages.parseAck(frame) {
                    XCTAssertEqual(ack.status, PhoenixProto.ackOK)
                    acked.fulfill()
                }
            }
            .store(in: &cancellables)
        transport.send(PhoenixMessages.ping())
        await fulfillment(of: [acked], timeout: 5)
    }

    func testWrongProtocolVersionIsRejected() async throws {
        let transport = try await connectedFake()
        let acked = expectation(description: "bad-version ack")
        transport.incomingFrames
            .sink { frame in
                if let ack = PhoenixMessages.parseAck(frame), ack.status == PhoenixProto.ackBadVersion {
                    acked.fulfill()
                }
            }
            .store(in: &cancellables)
        transport.send(PhoenixFrame(type: .ping, payload: [99]))
        await fulfillment(of: [acked], timeout: 5)
    }

    func testLargePayloadSurvivesMTUChunking() async throws {
        // 400 bytes over a 23-byte MTU forces ~20 fragments through the real
        // decoder — the reassembly path the radio actually exercises.
        let transport = try await connectedFake(mtu: 23)
        let long = String(repeating: "phoenix ", count: 50)
        transport.send(PhoenixMessages.assistantText(long))

        // The virtual glasses received and applied the whole message.
        guard case .assistant(let text, let streaming) = transport.glasses.screen else {
            return XCTFail("expected assistant screen, got \(transport.glasses.screen)")
        }
        XCTAssertEqual(text, long)
        XCTAssertFalse(streaming)
    }

    func testStreamingChunksAccumulateThenFinalize() async throws {
        let transport = try await connectedFake()
        transport.send(PhoenixMessages.assistantChunk("Sunny and ", final: false))
        if case .assistant(_, let streaming) = transport.glasses.screen {
            XCTAssertTrue(streaming, "should still be streaming mid-reply")
        } else {
            XCTFail("expected assistant screen")
        }
        transport.send(PhoenixMessages.assistantChunk("22C.", final: true))
        guard case .assistant(let text, let streaming) = transport.glasses.screen else {
            return XCTFail("expected assistant screen")
        }
        XCTAssertEqual(text, "Sunny and 22C.")
        XCTAssertFalse(streaming)
    }

    func testClearReturnsToClock() async throws {
        let transport = try await connectedFake()
        transport.send(PhoenixMessages.assistantText("something"))
        transport.send(PhoenixMessages.clear(PhoenixProto.clearAll))
        XCTAssertEqual(transport.glasses.screen, .clock)
    }

    func testNavUpdateRendersDistanceAndStreet() async throws {
        let transport = try await connectedFake()
        transport.send(PhoenixMessages.navUpdate(.left, meters: 1250, street: "Market St"))
        guard case .nav(let maneuver, let meters, let street) = transport.glasses.screen else {
            return XCTFail("expected nav screen")
        }
        XCTAssertEqual(maneuver, .left)
        XCTAssertEqual(meters, 1250)
        XCTAssertEqual(street, "Market St")
        let (big, unit) = GlassesSimulation.distance(meters)
        XCTAssertEqual(big, "1.2")
        XCTAssertEqual(unit, "km")
    }

    func testButtonPressReachesThePhone() async throws {
        let transport = try await connectedFake()
        let pressed = expectation(description: "button event")
        transport.incomingFrames
            .sink { frame in
                if let b = PhoenixMessages.parseButtonEvent(frame) {
                    XCTAssertEqual(b.button, 2)
                    XCTAssertEqual(b.action, 1)
                    pressed.fulfill()
                }
            }
            .store(in: &cancellables)
        transport.pressButton(2, long: true)
        await fulfillment(of: [pressed], timeout: 5)
    }

    func testFrameLogCapturesBothDirections() async throws {
        let transport = try await connectedFake()
        var directions = Set<String>()
        let both = expectation(description: "tx and rx logged")
        both.assertForOverFulfill = false
        transport.frameLog
            .sink { entry in
                directions.insert(entry.direction == .tx ? "tx" : "rx")
                if directions.count == 2 { both.fulfill() }
            }
            .store(in: &cancellables)
        transport.send(PhoenixMessages.ping())   // TX, answered by an RX ack
        await fulfillment(of: [both], timeout: 5)
    }

    func testStoppingReportsDisconnected() async throws {
        let transport = try await connectedFake()
        transport.stop()
        XCTAssertFalse(transport.state.isConnected)
    }

    // MARK: app model over the abstraction

    func testAppModelStreamsReplyToGlasses() async throws {
        let transport = FakeTransport()
        let model = AppModel(transport: transport, llm: StubLLM(reply: ["Sunny ", "and 22C."]))
        let connected = expectation(description: "connected")
        transport.statePublisher
            .sink { if $0.isConnected { connected.fulfill() } }
            .store(in: &cancellables)
        model.start()
        await fulfillment(of: [connected], timeout: 5)

        await model.ask("weather?")
        XCTAssertEqual(model.assistantReply, "Sunny and 22C.")
        XCTAssertFalse(model.isStreaming)
        guard case .assistant(let text, let streaming) = transport.glasses.screen else {
            return XCTFail("expected assistant screen")
        }
        XCTAssertEqual(text, "Sunny and 22C.")
        XCTAssertFalse(streaming, "final chunk should end the stream")
    }

    func testAppModelSurfacesLLMFailureOnGlasses() async throws {
        let transport = FakeTransport()
        let model = AppModel(transport: transport, llm: StubLLM(reply: [], error: LLMError.badResponse("HTTP 500")))
        let connected = expectation(description: "connected")
        transport.statePublisher
            .sink { if $0.isConnected { connected.fulfill() } }
            .store(in: &cancellables)
        model.start()
        await fulfillment(of: [connected], timeout: 5)

        await model.ask("hello")
        XCTAssertNotNil(model.lastError)
        XCTAssertFalse(model.isStreaming)
        // The wearer sees the failure rather than a stuck cursor.
        guard case .assistant(let text, let streaming) = transport.glasses.screen else {
            return XCTFail("expected assistant screen")
        }
        XCTAssertTrue(text.contains("failed"), "got \(text)")
        XCTAssertFalse(streaming)
    }

    func testAppModelIgnoresEmptyPrompt() async throws {
        let transport = FakeTransport()
        let model = AppModel(transport: transport, llm: StubLLM(reply: ["unused"]))
        await model.ask("   ")
        XCTAssertTrue(model.assistantReply.isEmpty)
    }

    func testOfflineEchoModeStillProducesAReply() async throws {
        // No API key -> offline echo; the app must stay usable, not crash.
        let llm = LLMClient(apiKey: nil)
        XCTAssertTrue(llm.isOffline)
        var chunks: [String] = []
        try await llm.stream(prompt: "hello there") { chunks.append($0) }
        let reply = chunks.joined()
        XCTAssertTrue(reply.contains("hello there"), "got \(reply)")
    }
}

/// Deterministic LLM stand-in for the pipeline tests.
private struct StubLLM: LLMClienting {
    let reply: [String]
    var error: Error?
    var isOffline: Bool { false }

    func stream(prompt: String, onChunk: @escaping (String) -> Void) async throws {
        for chunk in reply { onChunk(chunk) }
        if let error { throw error }
    }
}
