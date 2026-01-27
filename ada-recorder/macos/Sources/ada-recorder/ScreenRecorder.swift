import AVFoundation
import CoreMedia
import Foundation
import ScreenCaptureKit

/// Records screen content using ScreenCaptureKit and encodes to MP4.
/// Available on macOS 12.3+
@available(macOS 12.3, *)
final class ScreenRecorder: NSObject {
    private var stream: SCStream?
    private var assetWriter: AVAssetWriter?
    private var videoInput: AVAssetWriterInput?
    private var adaptor: AVAssetWriterInputPixelBufferAdaptor?
    private let outputURL: URL
    private var isRecording = false
    private var startTime: CMTime?
    private var frameCount: Int = 0
    private let recordingQueue = DispatchQueue(label: "com.ada.screenrecorder", qos: .userInitiated)

    init(outputDir: URL) {
        self.outputURL = outputDir.appendingPathComponent("screen.mp4")
        super.init()
    }

    /// Request screen recording permission
    func requestAccess() async throws {
        // ScreenCaptureKit requires explicit permission via SCShareableContent
        do {
            _ = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
        } catch {
            throw ScreenRecorderError.permissionDenied
        }
    }

    /// Start recording the screen to screen.mp4
    func start() async throws {
        guard !isRecording else {
            throw ScreenRecorderError.alreadyRecording
        }

        // Get available content
        let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)

        guard let display = content.displays.first else {
            throw ScreenRecorderError.noDisplayFound
        }

        // Configure stream
        let config = SCStreamConfiguration()
        config.width = Int(display.width)
        config.height = Int(display.height)
        config.minimumFrameInterval = CMTime(value: 1, timescale: 30) // 30 fps
        config.pixelFormat = kCVPixelFormatType_32BGRA
        config.showsCursor = true

        // Create filter for the display
        let filter = SCContentFilter(display: display, excludingWindows: [])

        // Setup AVAssetWriter for MP4 output
        try setupAssetWriter(width: config.width, height: config.height)

        // Create and start stream
        let stream = SCStream(filter: filter, configuration: config, delegate: self)
        try stream.addStreamOutput(self, type: .screen, sampleHandlerQueue: recordingQueue)

        self.stream = stream
        isRecording = true

        try await stream.startCapture()
        fputs("Screen recording started: \(outputURL.path)\n", stderr)
    }

    private func setupAssetWriter(width: Int, height: Int) throws {
        // Remove existing file if present
        if FileManager.default.fileExists(atPath: outputURL.path) {
            try FileManager.default.removeItem(at: outputURL)
        }

        assetWriter = try AVAssetWriter(url: outputURL, fileType: .mp4)

        // Video settings for H.264 encoding
        let videoSettings: [String: Any] = [
            AVVideoCodecKey: AVVideoCodecType.h264,
            AVVideoWidthKey: width,
            AVVideoHeightKey: height,
            AVVideoCompressionPropertiesKey: [
                AVVideoAverageBitRateKey: 8_000_000, // 8 Mbps
                AVVideoExpectedSourceFrameRateKey: 30,
                AVVideoMaxKeyFrameIntervalKey: 60,
                AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
            ] as [String: Any]
        ]

        videoInput = AVAssetWriterInput(mediaType: .video, outputSettings: videoSettings)
        videoInput?.expectsMediaDataInRealTime = true

        let sourcePixelBufferAttributes: [String: Any] = [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
            kCVPixelBufferWidthKey as String: width,
            kCVPixelBufferHeightKey as String: height,
        ]

        adaptor = AVAssetWriterInputPixelBufferAdaptor(
            assetWriterInput: videoInput!,
            sourcePixelBufferAttributes: sourcePixelBufferAttributes
        )

        if assetWriter!.canAdd(videoInput!) {
            assetWriter!.add(videoInput!)
        }

        guard assetWriter!.startWriting() else {
            throw ScreenRecorderError.assetWriterFailed(assetWriter!.error?.localizedDescription ?? "Unknown error")
        }

        assetWriter!.startSession(atSourceTime: .zero)
    }

    /// Stop recording and finalize the file
    func stop() async {
        guard isRecording, let stream = stream else { return }

        isRecording = false

        do {
            try await stream.stopCapture()
        } catch {
            fputs("Warning: Error stopping capture: \(error)\n", stderr)
        }

        self.stream = nil

        // Finalize video file
        await finalizeRecording()
    }

    private func finalizeRecording() async {
        guard let assetWriter = assetWriter else { return }

        videoInput?.markAsFinished()

        await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
            assetWriter.finishWriting {
                continuation.resume()
            }
        }

        self.assetWriter = nil
        self.videoInput = nil
        self.adaptor = nil

        // Verify file was written
        if FileManager.default.fileExists(atPath: outputURL.path) {
            if let attrs = try? FileManager.default.attributesOfItem(atPath: outputURL.path),
               let size = attrs[.size] as? Int64 {
                fputs("Screen recording saved: \(outputURL.path) (\(size) bytes, \(frameCount) frames)\n", stderr)
            }
        } else {
            fputs("Warning: Screen file not found after recording\n", stderr)
        }
    }

    /// Get the output file path
    var outputPath: String {
        outputURL.path
    }
}

@available(macOS 12.3, *)
extension ScreenRecorder: SCStreamDelegate {
    func stream(_ stream: SCStream, didStopWithError error: Error) {
        fputs("Screen capture stopped with error: \(error)\n", stderr)
        isRecording = false
    }
}

@available(macOS 12.3, *)
extension ScreenRecorder: SCStreamOutput {
    func stream(_ stream: SCStream, didOutputSampleBuffer sampleBuffer: CMSampleBuffer, of type: SCStreamOutputType) {
        guard type == .screen, isRecording else { return }
        guard let videoInput = videoInput, videoInput.isReadyForMoreMediaData else { return }

        // Get pixel buffer from sample buffer
        guard let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }

        // Get presentation timestamp
        let presentationTime = CMSampleBufferGetPresentationTimeStamp(sampleBuffer)

        // Track start time for relative timestamps
        if startTime == nil {
            startTime = presentationTime
        }

        // Calculate relative time from start
        let relativeTime = CMTimeSubtract(presentationTime, startTime!)

        // Append to asset writer
        if let adaptor = adaptor, adaptor.assetWriterInput.isReadyForMoreMediaData {
            if adaptor.append(pixelBuffer, withPresentationTime: relativeTime) {
                frameCount += 1
            }
        }
    }
}

enum ScreenRecorderError: Error, CustomStringConvertible {
    case permissionDenied
    case noDisplayFound
    case alreadyRecording
    case assetWriterFailed(String)

    var description: String {
        switch self {
        case .permissionDenied:
            return "Screen recording permission denied. Grant access in System Settings > Privacy & Security > Screen Recording"
        case .noDisplayFound:
            return "No display found for screen recording"
        case .alreadyRecording:
            return "Screen recording already in progress"
        case .assetWriterFailed(let reason):
            return "Failed to setup video writer: \(reason)"
        }
    }
}
