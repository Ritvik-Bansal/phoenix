import Foundation

/// LLM configuration. The key is read from the build's Info.plist (populated
/// by the gitignored Config/Secrets.xcconfig) or, preferentially, from the
/// Keychain so it never has to touch the repo at all. With neither present the
/// app degrades to offline echo mode instead of crashing.
enum PhoenixConfig {
    private static let keychainService = "com.phoenix.llm"
    private static let keychainAccount = "api-key"

    static var apiKey: String? {
        if let fromKeychain = Keychain.read(service: keychainService, account: keychainAccount),
           !fromKeychain.isEmpty {
            return fromKeychain
        }
        let fromPlist = Bundle.main.object(forInfoDictionaryKey: "PHOENIX_LLM_API_KEY") as? String
        guard let fromPlist, !fromPlist.isEmpty, !fromPlist.hasPrefix("sk-your-key") else { return nil }
        return fromPlist
    }

    static var endpoint: URL {
        let s = Bundle.main.object(forInfoDictionaryKey: "PHOENIX_LLM_ENDPOINT") as? String
        return URL(string: s ?? "") ?? URL(string: "https://api.openai.com/v1/chat/completions")!
    }

    static var model: String {
        let s = Bundle.main.object(forInfoDictionaryKey: "PHOENIX_LLM_MODEL") as? String
        return (s?.isEmpty == false) ? s! : "gpt-4o-mini"
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
