import Foundation

/// Exponential backoff with full jitter for BLE reconnection. Pure and
/// injectable-random so it is unit-testable (PhoenixReconnectionTests).
///
/// delay(attempt) picks a uniform value in [0, min(cap, base * 2^attempt)]
/// — "full jitter" (AWS architecture blog), which avoids reconnection
/// thundering-herd and, for a single device, simply spreads retries out.
struct ReconnectionBackoff {
    let base: TimeInterval
    let cap: TimeInterval
    private var attempt = 0

    init(base: TimeInterval = 0.5, cap: TimeInterval = 30) {
        self.base = base
        self.cap = cap
    }

    /// Ceiling for the current attempt before jitter (deterministic).
    func ceiling(for attempt: Int) -> TimeInterval {
        let exp = pow(2.0, Double(min(attempt, 30)))
        return min(cap, base * exp)
    }

    /// Next delay, applying full jitter via the supplied RNG. Advances the
    /// attempt counter.
    mutating func nextDelay<R: RandomNumberGenerator>(using rng: inout R) -> TimeInterval {
        let ceil = ceiling(for: attempt)
        attempt += 1
        return Double.random(in: 0...ceil, using: &rng)
    }

    mutating func nextDelay() -> TimeInterval {
        var rng = SystemRandomNumberGenerator()
        return nextDelay(using: &rng)
    }

    /// Call after a successful connection so the next drop retries fast.
    mutating func reset() { attempt = 0 }

    var currentAttempt: Int { attempt }
}
