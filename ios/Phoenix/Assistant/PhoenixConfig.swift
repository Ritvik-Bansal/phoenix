import Foundation

// LLM configuration. The key comes from the Keychain (preferred) or from the
// build's Info.plist, populated by the gitignored Config/Secrets.xcconfig.
// With neither present the app degrades to offline echo mode.

/// Which API the configured key belongs to. Detected from the key itself so
/// pasting a key is all the user has to do; an explicit endpoint/model in
/// Config/Secrets.xcconfig still overrides everything.
enum LLMProvider: String {
    case openAI
    case gemini

    /// OpenAI keys are `sk-...`; Google's are `AIza...` or `AQ....`.
    static func detect(fromKey key: String) -> LLMProvider {
        key.hasPrefix("sk-") ? .openAI : .gemini
    }

    var displayName: String {
        switch self {
        case .openAI: return "OpenAI"
        case .gemini: return "Gemini"
        }
    }

    /// Both speak the OpenAI streaming chat-completions format, so one client
    /// and one SSE parser cover both. Gemini exposes a compatibility endpoint
    /// for exactly this.
    var defaultEndpoint: URL {
        switch self {
        case .openAI:
            return URL(string: "https://api.openai.com/v1/chat/completions")!
        case .gemini:
            return URL(string: "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions")!
        }
    }

    /// Aliases, not pinned versions: Google retires dated model ids (a pinned
    /// gemini-2.5-flash already 404s), and `-latest` keeps working.
    var defaultModel: String {
        switch self {
        case .openAI: return "gpt-4o-mini"
        case .gemini: return "gemini-flash-latest"
        }
    }
}

enum PhoenixConfig {
    private static let keychainService = "com.phoenix.llm"
    private static let keychainAccount = "api-key"

    static var apiKey: String? {
        if let fromKeychain = Keychain.read(service: keychainService, account: keychainAccount),
           !fromKeychain.isEmpty {
            return fromKeychain
        }
        let fromPlist = Bundle.main.object(forInfoDictionaryKey: "PHOENIX_LLM_API_KEY") as? String
        guard let fromPlist else { return nil }
        let trimmed = fromPlist.trimmingCharacters(in: .whitespacesAndNewlines)
        // Ignore the committed template placeholders so an unedited copy of
        // Secrets.example.xcconfig still lands in offline echo mode.
        let placeholders = ["", "paste-your-key-here", "sk-your-key-here", "sk-your-real-key"]
        guard !placeholders.contains(trimmed) else { return nil }
        return trimmed
    }

    static var provider: LLMProvider {
        guard let apiKey else { return .openAI }
        return LLMProvider.detect(fromKey: apiKey)
    }

    /// Empty in Base.xcconfig means "pick from the provider"; a non-empty
    /// value is an explicit override (self-hosted, proxy, Azure, ...).
    private static func override(_ key: String) -> String? {
        guard let v = Bundle.main.object(forInfoDictionaryKey: key) as? String,
              !v.trimmingCharacters(in: .whitespaces).isEmpty else { return nil }
        return v
    }

    static var endpoint: URL {
        if let s = override("PHOENIX_LLM_ENDPOINT"), let u = URL(string: s) { return u }
        return provider.defaultEndpoint
    }

    static var model: String {
        override("PHOENIX_LLM_MODEL") ?? provider.defaultModel
    }

    static var isConfigured: Bool { apiKey != nil }

    static func storeAPIKey(_ key: String) {
        Keychain.write(key, service: keychainService, account: keychainAccount)
    }

    static func clearAPIKey() {
        Keychain.delete(service: keychainService, account: keychainAccount)
    }
}

/// Tiny Keychain wrapper — generic password items, no third-party dependency.
enum Keychain {
    static func read(service: String, account: String) -> String? {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]
        var item: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &item) == errSecSuccess,
              let data = item as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }

    @discardableResult
    static func write(_ value: String, service: String, account: String) -> Bool {
        delete(service: service, account: account)
        let attrs: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecValueData as String: Data(value.utf8),
            kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlock,
        ]
        return SecItemAdd(attrs as CFDictionary, nil) == errSecSuccess
    }

    @discardableResult
    static func delete(service: String, account: String) -> Bool {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
        ]
        return SecItemDelete(query as CFDictionary) == errSecSuccess
    }
}
