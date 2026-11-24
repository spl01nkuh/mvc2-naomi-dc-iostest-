import AVFoundation
import Foundation

/// Owned by the permanent gameplay thread. Only the completion handler runs
/// outside that thread; it returns a preallocated buffer through scalar state.
final class NativeAudioOutput {
    private struct AudioError: LocalizedError {
        let message: String
        var errorDescription: String? { message }
    }

    private static let sampleRate = 44_100.0
    private static let bufferCount = 6
    private static let bufferCapacity: AVAudioFrameCount = 4_096
    private static let maximumQueuedFrames = 6_615 // At most 150 ms of sound.
    private static let allBuffersAvailable: UInt8 = 0x3f

    private let engine = AVAudioEngine()
    private let player = AVAudioPlayerNode()
    private var buffers: [AVAudioPCMBuffer] = []
    private var graphReady = false
    private var running = false
    private var sessionActive = false

    // No graph work, PCM conversion, allocation or unbounded queue runs in the
    // audio completion. This lock protects only these three scalar values.
    private let completionLock = NSLock()
    private var generation: UInt64 = 0
    private var availableBuffers = NativeAudioOutput.allBuffersAvailable
    private var queuedFrames = 0

    func resume() throws {
        guard !running else { return }
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .default)
            try session.setPreferredSampleRate(Self.sampleRate)
            try session.setPreferredIOBufferDuration(512 / Self.sampleRate)
            try session.setActive(true)
            sessionActive = true
            try prepareGraph()
            try engine.start()
            player.play()
            running = true
        } catch {
            suspend()
            throw AudioError(message: "Game audio could not start: \(error.localizedDescription)")
        }
    }

    /// Stop pending playback before another game frame can reuse its buffers.
    /// Do not hold completionLock across stop(): it may invoke completions.
    func suspend() {
        running = false
        completionLock.lock()
        generation &+= 1
        availableBuffers = Self.allBuffersAvailable
        queuedFrames = 0
        completionLock.unlock()
        if graphReady {
            player.stop()
            engine.pause()
        }
        if sessionActive {
            try? AVAudioSession.sharedInstance().setActive(false,
                                                          options: .notifyOthersOnDeactivation)
            sessionActive = false
        }
    }

    /// Copies native 44.1 kHz interleaved stereo samples before the next native
    /// step invalidates them. Excess sound is dropped instead of growing latency.
    func enqueue(interleaved samples: UnsafePointer<Int16>, frames: Int) {
        guard running, engine.isRunning, frames > 0,
              frames <= Int(Self.bufferCapacity) else { return }

        completionLock.lock()
        guard queuedFrames + frames <= Self.maximumQueuedFrames,
              availableBuffers != 0 else {
            completionLock.unlock()
            return
        }
        let index = availableBuffers.trailingZeroBitCount
        let slot = UInt8(1) << index
        availableBuffers &= ~slot
        queuedFrames += frames
        let scheduledGeneration = generation
        completionLock.unlock()

        let buffer = buffers[index]
        buffer.frameLength = AVAudioFrameCount(frames)
        // prepareGraph creates only standard noninterleaved Float32 buffers.
        guard let channels = buffer.floatChannelData else {
            completionLock.lock()
            availableBuffers |= slot
            queuedFrames -= frames
            completionLock.unlock()
            return
        }
        for frame in 0..<frames {
            channels[0][frame] = Float(samples[frame * 2]) / 32_768
            channels[1][frame] = Float(samples[frame * 2 + 1]) / 32_768
        }

        player.scheduleBuffer(buffer, completionCallbackType: .dataPlayedBack) { [weak self] _ in
            guard let self else { return }
            self.completionLock.lock()
            if self.generation == scheduledGeneration {
                self.availableBuffers |= slot
                self.queuedFrames -= frames
            }
            self.completionLock.unlock()
        }
    }

    private func prepareGraph() throws {
        guard !graphReady else { return }
        guard let format = AVAudioFormat(standardFormatWithSampleRate: Self.sampleRate,
                                         channels: 2) else {
            throw AudioError(message: "The stereo game audio format is unavailable.")
        }
        var prepared: [AVAudioPCMBuffer] = []
        for _ in 0..<Self.bufferCount {
            guard let buffer = AVAudioPCMBuffer(pcmFormat: format,
                                                frameCapacity: Self.bufferCapacity) else {
                throw AudioError(message: "The game audio buffers could not be allocated.")
            }
            prepared.append(buffer)
        }
        buffers = prepared
        engine.attach(player)
        engine.connect(player, to: engine.mainMixerNode, format: format)
        engine.prepare()
        graphReady = true
    }
}
