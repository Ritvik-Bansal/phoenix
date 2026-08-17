import Foundation

/// Streaming client for an OpenAI-compatible chat endpoint. Streaming matters
/// here: chunks go to the glasses as ASSISTANT_STREAM_CHUNK frames, so text
/// starts appearing on the display before the model finishes.
///
/// With no API key configured the client runs in offline echo mode — the app
/// stays fully usable (and demoable) without network or secrets.
protocol LLMClienting {
    /// Streams reply fragments in order. Throws on transport failure.
    func stream(prompt: String, onChunk: @escaping (String) -> Void) async throws
    var isOffline: Bool { get }
}

final class LLMClient: LLMClienting {
    private let session: URLSession
    /// Resolved per request, not captured at init, so a key saved into the
    /// Keychain while the app is running takes effect on the next question
    /// instead of requiring a relaunch.
    private let apiKeyProvider: () -> String?
    private let endpointProvider: () -> URL
    private let modelProvider: () -> String

    init(session: URLSession = .shared,
         apiKeyProvider: @escaping () -> String? = { PhoenixConfig.apiKey },
         endpointProvider: @escaping () -> URL = { PhoenixConfig.endpoint },
         modelProvider: @escaping () -> String = { PhoenixConfig.model }) {
        self.session = session
        self.apiKeyProvider = apiKeyProvider
        self.endpointProvider = endpointProvider
        self.modelProvider = modelProvider
    }

    /// Fixed-configuration convenience (tests, previews).
    convenience init(session: URLSession = .shared, apiKey: String?,
                     endpoint: URL = PhoenixConfig.endpoint,
                     model: String = PhoenixConfig.model) {
        self.init(session: session,
                  apiKeyProvider: { apiKey },
                  endpointProvider: { endpoint },
                  modelProvider: { model })
    }

    var isOffline: Bool { apiKeyProvider() == nil }

    /// Human-readable provider for error messages.
    private var providerName: String {
        guard let key = apiKeyProvider() else { return "LLM" }
        return LLMProvider.detect(fromKey: key).displayName
    }

    func stream(prompt: String, onChunk: @escaping (String) -> Void) async throws {
        let endpoint = endpointProvider()
        let model = modelProvider()
        guard let apiKey = apiKeyProvider() else {
            await echo(prompt: prompt, onChunk: onChunk)
            return
        }

        var request = URLRequest(url: endpoint)
        request.httpMethod = "POST"
        request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.timeoutInterval = 30

        let body: [String: Any] = [
            "model": model,
            "stream": true,
            "messages": [
                // The display is 12 characters by 4 lines: brevity is a
                // hardware constraint, not a style preference.
                ["role": "system",
                 "content": "You answer for a tiny heads-up display. Reply in at most 2 short sentences, plain ASCII, no markdown, no emoji."],
                ["role": "user", "content": prompt],
            ],
        ]
        request.httpBody = try JSONSerialization.data(withJSONObject: body)

        let (bytes, response) = try await session.bytes(for: request)
        guard let http = response as? HTTPURLResponse else {
            throw LLMError.badResponse("no HTTP response")
        }
        guard (200..<300).contains(http.statusCode) else {
            throw LLMError.http(status: http.statusCode, provider: providerName)
        }

        // Server-sent events: `data: {json}` lines, terminated by `data: [DONE]`.
        for try await line in bytes.lines {
            guard line.hasPrefix("data:") else { continue }
            let payload = line.dropFirst(5).trimmingCharacters(in: .whitespaces)
            if payload == "[DONE]" { break }
            guard let data = payload.data(using: .utf8),
                  let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let choices = json["choices"] as? [[String: Any]],
                  let delta = choices.first?["delta"] as? [String: Any],
                  let content = delta["content"] as? String,
                  !content.isEmpty
            else { continue }
            onChunk(content)
        }
    }

    /// Offline echo mode: emits a short canned reply in chunks so the whole
    /// streaming path (and the glasses render) still exercises end to end.
    private func echo(prompt: String, onChunk: @escaping (String) -> Void) async {
        let trimmed = prompt.trimmingCharacters(in: .whitespacesAndNewlines)
        let reply = trimmed.isEmpty
            ? "Offline mode: no API key configured."
            : "Offline echo: \(trimmed)"
        for word in reply.split(separator: " ", omittingEmptySubsequences: false) {
            onChunk(word + " ")
            try? await Task.sleep(nanoseconds: 90_000_000)
        }
    }
}

enum LLMError: LocalizedError {
    case badResponse(String)
    case http(status: Int, provider: String)

    var errorDescription: String? {
        switch self {
        case .badResponse(let detail):
            return "LLM request failed: \(detail)"
        case .http(let status, let provider):
            // Say what to do, not just what broke. 401 in particular is
            // almost always a rotated or revoked key rather than a bug.
            switch status {
            case 401, 403:
                return "\(provider) rejected the API key (HTTP \(status)). "
                     + "If you rotated or deleted it, paste the new key in Debug → Save."
            case 404:
                return "\(provider) does not recognise the model (HTTP 404). "
                     + "It may have been retired — set PHOENIX_LLM_MODEL to a current one."
            case 429:
                return "\(provider) rate-limited or out of quota (HTTP 429). Try again shortly."
            case 500...599:
                return "\(provider) server error (HTTP \(status)). Try again shortly."
            default:
                return "\(provider) request failed (HTTP \(status))."
            }
        }
    }
}
