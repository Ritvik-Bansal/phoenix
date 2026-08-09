import XCTest
@testable import Phoenix

final class ReconnectionBackoffTests: XCTestCase {

    /// Deterministic RNG so jitter is reproducible in tests.
    private struct SeededRNG: RandomNumberGenerator {
        var state: UInt64
        init(seed: UInt64) { state = seed == 0 ? 0x9E3779B97F4A7C15 : seed }
        mutating func next() -> UInt64 {
            state ^= state << 13
            state ^= state >> 7
            state ^= state << 17
            return state
        }
    }

    func testCeilingDoublesUntilCap() {
        let backoff = ReconnectionBackoff(base: 0.5, cap: 30)
        XCTAssertEqual(backoff.ceiling(for: 0), 0.5, accuracy: 1e-9)
        XCTAssertEqual(backoff.ceiling(for: 1), 1.0, accuracy: 1e-9)
        XCTAssertEqual(backoff.ceiling(for: 2), 2.0, accuracy: 1e-9)
        XCTAssertEqual(backoff.ceiling(for: 5), 16.0, accuracy: 1e-9)
        XCTAssertEqual(backoff.ceiling(for: 6), 30.0, accuracy: 1e-9)   // capped
        XCTAssertEqual(backoff.ceiling(for: 40), 30.0, accuracy: 1e-9)  // no overflow
    }

    func testDelaysStayWithinJitterWindow() {
        var backoff = ReconnectionBackoff(base: 0.5, cap: 30)
        var rng = SeededRNG(seed: 42)
        for attempt in 0..<12 {
            let ceiling = backoff.ceiling(for: attempt)
            let delay = backoff.nextDelay(using: &rng)
            XCTAssertGreaterThanOrEqual(delay, 0)
            XCTAssertLessThanOrEqual(delay, ceiling)
        }
        XCTAssertEqual(backoff.currentAttempt, 12)
    }

    func testJitterActuallyVaries() {
        // Full jitter must not collapse to a constant; otherwise retries
        // synchronize and hammer the accessory in lockstep.
        var backoff = ReconnectionBackoff(base: 1, cap: 30)
        var rng = SeededRNG(seed: 7)
        _ = backoff.nextDelay(using: &rng)
        _ = backoff.nextDelay(using: &rng)
        _ = backoff.nextDelay(using: &rng)  // ceiling 4s: room to differ
        var samples = Set<String>()
        for _ in 0..<20 {
            var probe = backoff
            samples.insert(String(format: "%.4f", probe.nextDelay(using: &rng)))
        }
        XCTAssertGreaterThan(samples.count, 1)
    }

    func testResetReturnsToFastRetry() {
        var backoff = ReconnectionBackoff(base: 0.5, cap: 30)
        var rng = SeededRNG(seed: 99)
        for _ in 0..<8 { _ = backoff.nextDelay(using: &rng) }
        XCTAssertEqual(backoff.ceiling(for: backoff.currentAttempt), 30.0, accuracy: 1e-9)
        backoff.reset()
        XCTAssertEqual(backoff.currentAttempt, 0)
        XCTAssertEqual(backoff.ceiling(for: backoff.currentAttempt), 0.5, accuracy: 1e-9)
        let delay = backoff.nextDelay(using: &rng)
        XCTAssertLessThanOrEqual(delay, 0.5)
    }
}
