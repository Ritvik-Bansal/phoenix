import Foundation
import AVFoundation
import Speech

/// Push-to-talk speech capture. Voice comes from whatever the user already
/// wears — the audio session is configured to allow Bluetooth headset input,
/// so paired earbuds are the microphone. The glasses have no microphone and
/// never see audio; only the final text goes over BLE.
@MainActor
final class SpeechRecognizer: ObservableObject {
    @Published private(set) var transcript = ""
    @Published private(set) var isRecording = false
    @Published private(set) var lastError: String?
    /// True when Apple can transcribe without leaving the device.
    @Published private(set) var onDevice = false

    private let recognizer = SFSpeechRecognizer(locale: Locale(identifier: "en-US"))
    private var request: SFSpeechAudioBufferRecognitionRequest?
    private var task: SFSpeechRecognitionTask?
    private let engine = AVAudioEngine()

    var isAvailable: Bool { recognizer?.isAvailable ?? false }

    func requestAuthorization() async -> Bool {
        let speechOK = await withCheckedContinuation { (c: CheckedContinuation<Bool, Never>) in
            SFSpeechRecognizer.requestAuthorization { status in
                c.resume(returning: status == .authorized)
            }
        }
        guard speechOK else {
            lastError = "Speech recognition not authorized"
            return false
        }
        let micOK = await withCheckedContinuation { (c: CheckedContinuation<Bool, Never>) in
            AVAudioApplication.requestRecordPermission { granted in c.resume(returning: granted) }
        }
        if !micOK { lastError = "Microphone not authorized" }
        return micOK
    }

    func start() throws {
        guard !isRecording else { return }
        guard let recognizer, recognizer.isAvailable else {
            throw SpeechError.unavailable
        }
        transcript = ""
        lastError = nil

        // allowBluetoothHFP routes input from a paired headset (the earbuds
        // the wearer already has in); .duckOthers keeps the reply audible.
        let session = AVAudioSession.sharedInstance()
        try session.setCategory(.playAndRecord, mode: .voiceChat,
                                options: [.allowBluetoothHFP, .allowBluetoothA2DP, .duckOthers,
                                          .defaultToSpeaker])
        try session.setActive(true, options: .notifyOthersOnDeactivation)

        let req = SFSpeechAudioBufferRecognitionRequest()
        req.shouldReportPartialResults = true
        // Prefer on-device recognition: no audio leaves the phone, and it
        // works without a network.
        if recognizer.supportsOnDeviceRecognition {
            req.requiresOnDeviceRecognition = true
            onDevice = true
        } else {
            onDevice = false
        }
        request = req

        let input = engine.inputNode
        let format = input.outputFormat(forBus: 0)
        input.removeTap(onBus: 0)
        input.installTap(onBus: 0, bufferSize: 1024, format: format) { buffer, _ in
            req.append(buffer)
        }
        engine.prepare()
        try engine.start()
        isRecording = true

        task = recognizer.recognitionTask(with: req) { [weak self] result, error in
            guard let self else { return }
            Task { @MainActor in
                if let result {
                    self.transcript = result.bestTranscription.formattedString
                }
                if error != nil || (result?.isFinal ?? false) {
                    if let error, !(result?.isFinal ?? false) {
                        self.lastError = error.localizedDescription
                    }
                    self.stop()
                }
            }
        }
    }

    /// Stops capture and returns the final transcript.
    @discardableResult
    func stop() -> String {
        guard isRecording else { return transcript }
        engine.inputNode.removeTap(onBus: 0)
        engine.stop()
        request?.endAudio()
        task?.finish()
        task = nil
        request = nil
        isRecording = false
        try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
        return transcript
    }
}

enum SpeechError: LocalizedError {
    case unavailable

    var errorDescription: String? {
        switch self {
        case .unavailable: return "Speech recognition is unavailable on this device."
        }
    }
}
