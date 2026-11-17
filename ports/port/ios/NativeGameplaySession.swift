import Foundation

enum NativeGameplayStatus: Equatable {
    case stopped
    case starting
    case running
    case paused
    case failed(String)

    var isOpen: Bool {
        switch self {
        case .starting, .running, .paused: return true
        case .stopped, .failed: return false
        }
    }
}

enum NativeGameplayEvent {
    case status(NativeGameplayStatus)
    case firstFrame
    case audioWarning(String?)
}

/// The board and its game coroutines belong to one permanent OS thread. A
/// serial dispatch queue is insufficient: successive blocks can change threads.
/// Stop, restart and pause are commands handled only between native steps.
final class NativeGameplaySession {
    private enum Command {
        case start(rom: URL, saves: URL)
        case stop(CheckedContinuation<Void, Never>)
    }

    private let input: InputState
    private let frames: NativeFrameStore
    private let receive: @MainActor (NativeGameplayEvent) -> Void
    private let condition = NSCondition()
    private var commands: [Command] = []
    private var suspensionRequested = true
    private var widescreenRequested = false
    private var owner: Thread?

    init(input: InputState, frames: NativeFrameStore,
         receive: @escaping @MainActor (NativeGameplayEvent) -> Void) {
        self.input = input
        self.frames = frames
        self.receive = receive
        let thread = Thread { [weak self] in self?.run() }
        thread.name = "MVC2 native gameplay owner"
        thread.qualityOfService = .userInteractive
        owner = thread
        thread.start()
    }

    func start(rom: URL, saves: URL) {
        condition.lock()
        commands.append(.start(rom: rom, saves: saves))
        condition.signal()
        condition.unlock()
    }

    /// Await this before replacing imported files. The continuation is resumed
    /// after the native owner has finished any step and released the old ROM.
    func stop() async {
        await withCheckedContinuation { continuation in
            condition.lock()
            commands.append(.stop(continuation))
            condition.signal()
            condition.unlock()
        }
    }

    func setSuspended(_ suspended: Bool) {
        condition.lock()
        suspensionRequested = suspended
        condition.signal()
        condition.unlock()
    }

    func setWidescreen(_ enabled: Bool) {
        condition.lock()
        widescreenRequested = enabled
        condition.signal()
        condition.unlock()
    }

    private func send(_ event: NativeGameplayEvent) {
        let receive = self.receive
        // Preserve owner-thread event order when reporting to the UI.
        DispatchQueue.main.async { receive(event) }
    }

    private func run() {
        let audio = NativeAudioOutput()
        let period = 858.0 * 525.0 / 27_000_000.0
        var active = false
        var paused = true
        var deliveredFrame = false
        var nextFrame = ProcessInfo.processInfo.systemUptime

        // This thread intentionally survives stopped sessions: native coroutine
        // ownership must be preserved when the user starts another session.
        while true {
            condition.lock()
            while commands.isEmpty && (!active || (suspensionRequested && paused)) {
                condition.wait()
            }
            let command = commands.isEmpty ? nil : commands.removeFirst()
            let shouldPause = suspensionRequested
            let widescreen = widescreenRequested
            condition.unlock()

            if let command {
                switch command {
                case .start(let rom, let saves):
                    audio.suspend()
                    if active { mvc2_gameplay_stop() }
                    active = false
                    paused = true
                    deliveredFrame = false
                    input.releaseAll()
                    frames.clear()
                    send(.audioWarning(nil))
                    send(.status(.starting))
                    var error = [CChar](repeating: 0, count: 2048)
                    let capacity = error.count
                    let result = rom.path.withCString { romPath in
                        saves.path.withCString { savePath in
                            mvc2_gameplay_start(romPath, savePath, &error, capacity)
                        }
                    }
                    if result == 0 {
                        mvc2_gameplay_stop()
                        send(.status(.failed(Self.message(error, fallback:
                            "The native engine could not start this game data."))))
                    } else {
                        active = true
                        // The next loop observes commands/suspension before it
                        // resumes audio or advances the first game frame.
                        if shouldPause { send(.status(.paused)) }
                    }
                case .stop(let continuation):
                    audio.suspend()
                    if active { mvc2_gameplay_stop() }
                    active = false
                    paused = true
                    input.releaseAll()
                    frames.clear()
                    send(.audioWarning(nil))
                    send(.status(.stopped))
                    continuation.resume()
                }
                continue
            }

            if shouldPause {
                if !paused {
                    audio.suspend()
                    input.releaseAll()
                    paused = true
                    send(.status(.paused))
                }
                continue
            }
            if paused {
                do {
                    try audio.resume()
                    send(.audioWarning(nil))
                } catch {
                    // A route/session failure leaves the real native renderer
                    // usable and is reported separately from an engine failure.
                    send(.audioWarning(error.localizedDescription))
                }
                paused = false
                nextFrame = ProcessInfo.processInfo.systemUptime
                send(.status(.running))
            }

            let remaining = nextFrame - ProcessInfo.processInfo.systemUptime
            if remaining > 0 {
                condition.lock()
                if commands.isEmpty && !suspensionRequested {
                    _ = condition.wait(until: Date(timeIntervalSinceNow: remaining))
                }
                condition.unlock()
                continue
            }

            autoreleasepool {
                // Rendering options cross the native boundary on the same
                // permanent thread as startup, frame steps and shutdown.
                mvc2_gameplay_set_widescreen(widescreen ? 1 : 0)
                let snapshot = input.snapshotForFrame()
                var controls = HOST_INPUT()
                controls.player = (UInt32(snapshot.buttons), 0)
                controls.coin = snapshot.coin ? 1 : 0
                controls.test = 0
                var error = [CChar](repeating: 0, count: 2048)
                let capacity = error.count
                if mvc2_gameplay_step(&controls, &error, capacity) == 0 {
                    audio.suspend()
                    mvc2_gameplay_stop()
                    active = false
                    paused = true
                    input.releaseAll()
                    frames.clear()
                    send(.status(.failed(Self.message(error, fallback:
                        "The native engine stopped while advancing a game frame."))))
                    return
                }

                let width = Int(mvc2_gameplay_frame_width())
                let height = Int(mvc2_gameplay_frame_height())
                if width > 0, height > 0, width <= 4096, height <= 4096,
                   let pixels = mvc2_gameplay_frame(),
                   let frame = NativeCanvasFrame(width: width, height: height,
                       argb: UnsafeBufferPointer(start: pixels, count: width * height)) {
                    frames.publish(frame: frame)
                    if !deliveredFrame {
                        deliveredFrame = true
                        send(.firstFrame)
                    }
                }
                var samples: UnsafePointer<Int16>?
                let sampleFrames = mvc2_gameplay_audio(&samples)
                if sampleFrames > 0, let samples {
                    audio.enqueue(interleaved: samples, frames: Int(sampleFrames))
                }
            }

            nextFrame += period
            let now = ProcessInfo.processInfo.systemUptime
            // Slow frames do not cause an unbounded burst of catch-up frames.
            if now - nextFrame > period * 2 { nextFrame = now + period }
        }
    }

    private static func message(_ error: [CChar], fallback: String) -> String {
        let message = String(cString: error)
        return message.isEmpty ? fallback : message
    }
}
