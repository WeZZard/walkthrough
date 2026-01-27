import AVFoundation
import Foundation

/// Records voice/microphone audio using AVAudioRecorder.
/// Extracted from CaptureViewModel.swift for CLI use.
final class VoiceRecorder {
    private var audioRecorder: AVAudioRecorder?
    private let outputURL: URL
    private var isRecording = false

    init(outputDir: URL) {
        self.outputURL = outputDir.appendingPathComponent("voice.wav")
    }

    /// Request microphone access (must be called before start)
    func requestAccess() async throws {
        let status = AVCaptureDevice.authorizationStatus(for: .audio)
        switch status {
        case .authorized:
            return
        case .notDetermined:
            let granted = await withCheckedContinuation { continuation in
                AVCaptureDevice.requestAccess(for: .audio) { allowed in
                    continuation.resume(returning: allowed)
                }
            }
            if !granted {
                throw VoiceRecorderError.permissionDenied
            }
        case .denied, .restricted:
            throw VoiceRecorderError.permissionDenied
        @unknown default:
            throw VoiceRecorderError.permissionDenied
        }
    }

    /// Start recording voice to voice.wav
    func start() throws {
        guard !isRecording else {
            throw VoiceRecorderError.alreadyRecording
        }

        // Settings matching GUI app for consistency
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: 48_000,
            AVNumberOfChannelsKey: 1,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false,
        ]

        let recorder = try AVAudioRecorder(url: outputURL, settings: settings)
        recorder.isMeteringEnabled = false
        recorder.prepareToRecord()

        guard recorder.record() else {
            throw VoiceRecorderError.startFailed
        }

        audioRecorder = recorder
        isRecording = true
        fputs("Voice recording started: \(outputURL.path)\n", stderr)
    }

    /// Stop recording and finalize the file
    func stop() {
        guard isRecording, let recorder = audioRecorder else { return }

        recorder.stop()
        audioRecorder = nil
        isRecording = false

        // Verify file was written
        if FileManager.default.fileExists(atPath: outputURL.path) {
            if let attrs = try? FileManager.default.attributesOfItem(atPath: outputURL.path),
               let size = attrs[.size] as? Int64 {
                fputs("Voice recording saved: \(outputURL.path) (\(size) bytes)\n", stderr)
            }
        } else {
            fputs("Warning: Voice file not found after recording\n", stderr)
        }
    }

    /// Get the output file path
    var outputPath: String {
        outputURL.path
    }
}

enum VoiceRecorderError: Error, CustomStringConvertible {
    case permissionDenied
    case startFailed
    case alreadyRecording

    var description: String {
        switch self {
        case .permissionDenied:
            return "Microphone permission denied. Grant access in System Settings > Privacy & Security > Microphone"
        case .startFailed:
            return "Failed to start microphone recording"
        case .alreadyRecording:
            return "Voice recording already in progress"
        }
    }
}
