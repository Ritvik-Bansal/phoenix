import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        TabView {
            AssistantView()
                .tabItem { Label("Assistant", systemImage: "waveform") }
            DebugView()
                .tabItem { Label("Debug", systemImage: "list.bullet.rectangle") }
        }
    }
}

struct AssistantView: View {
    @EnvironmentObject private var model: AppModel
    @State private var typed = ""
    @State private var brightness: Double = 255
    @FocusState private var inputFocused: Bool

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 18) {
                    GlassesView(frameBuffer: model.glasses.render(),
                                brightness: model.glasses.brightness)
                        .id(model.glassesRevision)   // redraw on every device tick
                        .padding(.top, 8)

                    statusRow

                    if model.speech.isRecording {
                        Text(model.speech.transcript.isEmpty
                             ? "Listening…" : model.speech.transcript)
                            .font(.callout)
                            .foregroundStyle(.secondary)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(.horizontal)
                    }

                    micButton

                    HStack {
                        TextField("Ask something…", text: $typed)
                            .textFieldStyle(.roundedBorder)
                            .focused($inputFocused)
                            .submitLabel(.send)
                            .onSubmit(sendTyped)
                        Button("Send", action: sendTyped)
                            .buttonStyle(.borderedProminent)
                            .disabled(typed.trimmingCharacters(in: .whitespaces).isEmpty
                                      || model.isStreaming)
                    }
                    .padding(.horizontal)

                    if !model.assistantReply.isEmpty {
                        VStack(alignment: .leading, spacing: 4) {
                            Text(model.isStreaming ? "Streaming to glasses…" : "Reply")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                            Text(model.assistantReply)
                                .font(.body)
                        }
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(.horizontal)
                    }

                    if let error = model.lastError {
                        Text(error)
                            .font(.caption)
                            .foregroundStyle(.red)
                            .padding(.horizontal)
                    }

                    controls
                }
                .padding(.bottom, 24)
            }
            .navigationTitle("Phoenix")
            .background(Color(.systemGroupedBackground))
        }
    }

    private var statusRow: some View {
        VStack(spacing: 6) {
            HStack(spacing: 8) {
                Circle()
                    .fill(model.transportState.isConnected ? Color.green : Color.orange)
                    .frame(width: 8, height: 8)
                Text(model.transportState.label)
                    .font(.subheadline)
                if model.usingFakeTransport {
                    Text("· virtual device")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            HStack(spacing: 14) {
                if let b = model.battery {
                    Label("\(b.percent)% · \(b.millivolts)mV", systemImage: "battery.75")
                }
                if let btn = model.lastButton {
                    Label("Button \(btn)", systemImage: "hand.tap")
                }
                if model.isOfflineLLM {
                    Label("Offline echo", systemImage: "wifi.slash")
                }
            }
            .font(.caption)
            .foregroundStyle(.secondary)
        }
    }

    private var micButton: some View {
        Button {
            Task {
                if model.speech.isRecording {
                    await model.stopListeningAndAsk()
                } else {
                    await model.startListening()
                }
            }
        } label: {
            Label(model.speech.isRecording ? "Stop & Send" : "Hold to Ask",
                  systemImage: model.speech.isRecording ? "stop.circle.fill" : "mic.circle.fill")
                .font(.title3)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 10)
        }
        .buttonStyle(.borderedProminent)
        .tint(model.speech.isRecording ? .red : .accentColor)
        .disabled(model.isStreaming)
        .padding(.horizontal)
    }

    private var controls: some View {
        VStack(spacing: 14) {
            HStack(spacing: 10) {
                Button("Ping") { model.sendPing() }
                Button("Nav demo") { model.sendDemoNav() }
                Button("Clear") { model.sendClear() }
            }
            .buttonStyle(.bordered)

            VStack(alignment: .leading, spacing: 2) {
                Text("Brightness \(Int(brightness))")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Slider(value: $brightness, in: 0...255, step: 5) { editing in
                    if !editing { model.setBrightness(UInt8(brightness)) }
                }
            }
            .padding(.horizontal)

            if model.usingFakeTransport {
                VStack(spacing: 6) {
                    Text("Glasses buttons")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    HStack(spacing: 10) {
                        Button("A · dismiss") { model.pressVirtualButton(1) }
                        Button("B · next") { model.pressVirtualButton(2) }
                        Button("C · status") { model.pressVirtualButton(3) }
                    }
                    .buttonStyle(.bordered)
                    .font(.caption)
                }
            }
        }
    }

    private func sendTyped() {
        let prompt = typed
        typed = ""
        inputFocused = false
        Task { await model.ask(prompt) }
    }
}

#Preview {
    ContentView().environmentObject(AppModel(transport: FakeTransport()))
}
