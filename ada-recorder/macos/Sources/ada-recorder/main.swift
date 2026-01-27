import Foundation

/// ada-recorder: Native macOS screen and voice recorder for ADA capture
///
/// Usage:
///   ada-recorder start --output-dir <path> [--no-screen] [--no-voice]
///
/// Records until SIGINT (Ctrl+C) or SIGTERM is received.
/// Outputs: screen.mp4 (if screen enabled), voice.wav (if voice enabled)

// MARK: - Argument Parsing

struct Arguments {
    var command: String = ""
    var outputDir: String = ""
    var noScreen: Bool = false
    var noVoice: Bool = false

    static func parse() -> Arguments {
        var args = Arguments()
        let argv = CommandLine.arguments

        var i = 1
        while i < argv.count {
            let arg = argv[i]
            switch arg {
            case "start":
                args.command = "start"
            case "--output-dir":
                i += 1
                if i < argv.count {
                    args.outputDir = argv[i]
                }
            case "--no-screen":
                args.noScreen = true
            case "--no-voice":
                args.noVoice = true
            case "--help", "-h":
                Arguments.printUsage()
                exit(0)
            default:
                break
            }
            i += 1
        }

        return args
    }

    static func printUsage() {
        let usage = """
        ada-recorder - Native macOS screen and voice recorder for ADA capture

        Usage:
          ada-recorder start --output-dir <path> [--no-screen] [--no-voice]

        Commands:
          start     Start recording (runs until SIGINT/SIGTERM)

        Options:
          --output-dir <path>  Directory to save recordings (required)
          --no-screen          Disable screen recording
          --no-voice           Disable voice recording
          --help, -h           Show this help message

        Output Files:
          screen.mp4           Screen recording (if enabled)
          voice.wav            Voice recording (if enabled)

        Signals:
          SIGINT (Ctrl+C)      Stop recording gracefully
          SIGTERM              Stop recording gracefully
        """
        fputs("\(usage)\n", stderr)
    }
}

// MARK: - Signal Handling

final class SignalHandler {
    static let shared = SignalHandler()
    var shouldStop = false

    private init() {}

    func setup() {
        signal(SIGINT) { _ in
            fputs("\nReceived SIGINT, stopping...\n", stderr)
            SignalHandler.shared.shouldStop = true
        }
        signal(SIGTERM) { _ in
            fputs("\nReceived SIGTERM, stopping...\n", stderr)
            SignalHandler.shared.shouldStop = true
        }
    }
}

// MARK: - Recording Session

@available(macOS 12.3, *)
final class RecordingSession {
    let outputDir: URL
    let enableScreen: Bool
    let enableVoice: Bool

    private var screenRecorder: ScreenRecorder?
    private var voiceRecorder: VoiceRecorder?

    init(outputDir: URL, enableScreen: Bool, enableVoice: Bool) {
        self.outputDir = outputDir
        self.enableScreen = enableScreen
        self.enableVoice = enableVoice
    }

    func run() async throws {
        // Create output directory if needed
        try FileManager.default.createDirectory(at: outputDir, withIntermediateDirectories: true)

        // Initialize recorders
        if enableScreen {
            screenRecorder = ScreenRecorder(outputDir: outputDir)
            try await screenRecorder!.requestAccess()
        }

        if enableVoice {
            voiceRecorder = VoiceRecorder(outputDir: outputDir)
            try await voiceRecorder!.requestAccess()
        }

        // Start recording
        if let screen = screenRecorder {
            try await screen.start()
        }

        if let voice = voiceRecorder {
            try voice.start()
        }

        fputs("Recording started. Press Ctrl+C to stop.\n", stderr)

        // Wait for signal
        while !SignalHandler.shared.shouldStop {
            try await Task.sleep(nanoseconds: 100_000_000) // 100ms
        }

        fputs("Stopping recording...\n", stderr)

        // Stop recording in order (voice first as it's simpler)
        if let voice = voiceRecorder {
            voice.stop()
        }

        if let screen = screenRecorder {
            await screen.stop()
        }

        fputs("Recording complete.\n", stderr)
    }
}

// Fallback for older macOS
final class LegacyRecordingSession {
    let outputDir: URL
    let enableScreen: Bool
    let enableVoice: Bool

    private var voiceRecorder: VoiceRecorder?

    init(outputDir: URL, enableScreen: Bool, enableVoice: Bool) {
        self.outputDir = outputDir
        self.enableScreen = enableScreen
        self.enableVoice = enableVoice
    }

    func run() async throws {
        // Create output directory if needed
        try FileManager.default.createDirectory(at: outputDir, withIntermediateDirectories: true)

        if enableScreen {
            fputs("Warning: Screen recording requires macOS 12.3 or later. Skipping screen capture.\n", stderr)
        }

        if enableVoice {
            voiceRecorder = VoiceRecorder(outputDir: outputDir)
            try await voiceRecorder!.requestAccess()
            try voiceRecorder!.start()
        }

        fputs("Recording started. Press Ctrl+C to stop.\n", stderr)

        // Wait for signal
        while !SignalHandler.shared.shouldStop {
            try await Task.sleep(nanoseconds: 100_000_000) // 100ms
        }

        fputs("Stopping recording...\n", stderr)

        if let voice = voiceRecorder {
            voice.stop()
        }

        fputs("Recording complete.\n", stderr)
    }
}

// MARK: - Main

@main
struct AdaRecorder {
    static func main() async {
        SignalHandler.shared.setup()

        let args = Arguments.parse()

        guard args.command == "start" else {
            fputs("Error: Unknown or missing command. Use 'start' to begin recording.\n", stderr)
            Arguments.printUsage()
            exit(1)
        }

        guard !args.outputDir.isEmpty else {
            fputs("Error: --output-dir is required\n", stderr)
            Arguments.printUsage()
            exit(1)
        }

        let outputDir = URL(fileURLWithPath: args.outputDir)
        let enableScreen = !args.noScreen
        let enableVoice = !args.noVoice

        if !enableScreen && !enableVoice {
            fputs("Error: Both screen and voice recording disabled. Nothing to do.\n", stderr)
            exit(1)
        }

        do {
            if #available(macOS 12.3, *) {
                let session = RecordingSession(
                    outputDir: outputDir,
                    enableScreen: enableScreen,
                    enableVoice: enableVoice
                )
                try await session.run()
            } else {
                let session = LegacyRecordingSession(
                    outputDir: outputDir,
                    enableScreen: enableScreen,
                    enableVoice: enableVoice
                )
                try await session.run()
            }
            exit(0)
        } catch {
            fputs("Error: \(error)\n", stderr)
            exit(1)
        }
    }
}
