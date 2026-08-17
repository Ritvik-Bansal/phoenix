import XCTest
@testable import Phoenix

/// Provider selection is what makes "just paste a key" work, so the
/// detection rule and the per-provider defaults are pinned here.
final class LLMProviderTests: XCTestCase {

    func testDetectsOpenAIKeys() {
        XCTAssertEqual(LLMProvider.detect(fromKey: "sk-proj-abc123"), .openAI)
        XCTAssertEqual(LLMProvider.detect(fromKey: "sk-abc123"), .openAI)
    }

    func testDetectsGeminiKeys() {
        // Classic Google API key form and the newer AQ. form.
        XCTAssertEqual(LLMProvider.detect(fromKey: "AIzaSyExampleKeyValue"), .gemini)
        XCTAssertEqual(LLMProvider.detect(fromKey: "AQ.Ab8RNexampleexample"), .gemini)
    }

    func testUnknownKeyShapeFallsBackToGemini() {
        // Anything that is not obviously an OpenAI key is treated as Gemini;
        // an explicit endpoint override still wins over this guess.
        XCTAssertEqual(LLMProvider.detect(fromKey: "some-other-key"), .gemini)
    }

    func testProviderDefaultsAreDistinctAndUsable() {
        XCTAssertEqual(LLMProvider.openAI.defaultModel, "gpt-4o-mini")
        XCTAssertEqual(LLMProvider.gemini.defaultModel, "gemini-flash-latest")
        XCTAssertNotEqual(LLMProvider.openAI.defaultEndpoint, LLMProvider.gemini.defaultEndpoint)

        // Gemini must point at its OpenAI-compatibility endpoint, because the
        // app reuses one SSE parser for both providers.
        let gemini = LLMProvider.gemini.defaultEndpoint.absoluteString
        XCTAssertTrue(gemini.contains("generativelanguage.googleapis.com"), gemini)
        XCTAssertTrue(gemini.hasSuffix("/openai/chat/completions"), gemini)

        XCTAssertEqual(LLMProvider.openAI.defaultEndpoint.absoluteString,
                       "https://api.openai.com/v1/chat/completions")
    }

    func testModelDefaultsAreAliasesNotPinnedVersions() {
        // Google retires dated model ids (gemini-2.5-flash already 404s for
        // new keys), so the default must be a moving alias.
        XCTAssertTrue(LLMProvider.gemini.defaultModel.contains("latest"))
    }

    func testDisplayNames() {
        XCTAssertEqual(LLMProvider.openAI.displayName, "OpenAI")
        XCTAssertEqual(LLMProvider.gemini.displayName, "Gemini")
    }

    func testNoKeyMeansOfflineRegardlessOfProvider() {
        let client = LLMClient(apiKey: nil)
        XCTAssertTrue(client.isOffline)
    }
}
