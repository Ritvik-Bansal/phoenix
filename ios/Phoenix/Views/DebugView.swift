import SwiftUI

/// Raw protocol console: every frame both directions, hex plus decoded.
struct DebugView: View {
    @EnvironmentObject private var model: AppModel
    @State private var keyEntry = ""
    @State private var keyStatus: String?

    var body: some View {
        NavigationStack {
            List {
                Section {
                    LabeledContent("Transport", value: model.usingFakeTransport
                                   ? "FakeTransport (virtual)" : "BleTransport")
                    LabeledContent("State", value: model.transportState.label)
                    LabeledContent("LLM", value: model.isOfflineLLM
                                   ? "offline echo (no key)" : PhoenixConfig.model)
                    LabeledContent("Service", value: String(PhoenixProto.serviceUUID.prefix(8)) + "…")
                } header: {
                    Text("Link")
                }

                Section {
                    // Runtime alternative to Config/Secrets.xcconfig: the key
                    // goes to the Keychain, takes precedence over the build
                    // setting, and never touches the repo.
                    SecureField("sk-…", text: $keyEntry)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    HStack {
                        Button("Save to Keychain") {
                            let trimmed = keyEntry.trimmingCharacters(in: .whitespacesAndNewlines)
                            guard !trimmed.isEmpty else { return }
                            PhoenixConfig.storeAPIKey(trimmed)
                            keyEntry = ""
                            keyStatus = "Saved. New questions use the live LLM."
                            model.refreshLLMStatus()
                        }
                        .disabled(keyEntry.trimmingCharacters(in: .whitespaces).isEmpty)
                        Spacer()
                        Button("Clear", role: .destructive) {
                            PhoenixConfig.clearAPIKey()
                            keyEntry = ""
                            keyStatus = "Cleared. Back to offline echo."
                            model.refreshLLMStatus()
                        }
                    }
                    .font(.callout)
                    if let keyStatus {
                        Text(keyStatus).font(.caption).foregroundStyle(.secondary)
                    }
                } header: {
                    Text("LLM API key")
                } footer: {
                    Text("Stored in the Keychain, not in the project. The other option is ios/Config/Secrets.xcconfig, which is gitignored.")
                }

                Section {
                    if model.frames.isEmpty {
                        Text("No traffic yet").foregroundStyle(.secondary)
                    }
                    ForEach(model.frames) { entry in
                        VStack(alignment: .leading, spacing: 3) {
                            HStack(spacing: 6) {
                                Text(entry.direction == .tx ? "TX" : "RX")
                                    .font(.caption2.bold())
                                    .padding(.horizontal, 5)
                                    .padding(.vertical, 1)
                                    .background(entry.direction == .tx ? Color.blue.opacity(0.2)
                                                                       : Color.green.opacity(0.2))
                                    .clipShape(Capsule())
                                Text(entry.summary)
                                    .font(.caption)
                                Spacer()
                                Text(entry.date, format: .dateTime.hour().minute().second())
                                    .font(.caption2)
                                    .foregroundStyle(.secondary)
                            }
                            Text(hex(entry.bytes))
                                .font(.system(.caption2, design: .monospaced))
                                .foregroundStyle(.secondary)
                                .lineLimit(3)
                        }
                        .padding(.vertical, 2)
                    }
                } header: {
                    HStack {
                        Text("Frames (\(model.frames.count))")
                        Spacer()
                        Button("Clear") { model.clearLog() }
                            .font(.caption)
                    }
                }
            }
            .navigationTitle("Debug")
        }
    }

    private func hex(_ bytes: [UInt8]) -> String {
        bytes.map { String(format: "%02X", $0) }.joined(separator: " ")
    }
}

#Preview {
    DebugView().environmentObject(AppModel(transport: FakeTransport()))
}
