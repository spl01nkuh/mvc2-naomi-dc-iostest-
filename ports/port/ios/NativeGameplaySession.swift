import Foundation

enum NativeGameplayStatus: Equatable {
    case stopped
    case starting
    case running
    case paused
    case failed(String, report: URL?)

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
        case start(rom: URL, saves: URL, arcade: ArcadeSettings)
        case stop(CheckedContinuation<Void, Never>)
    }

    private let input: InputState
    private let frames: NativeFrameStore
    private let checkpoint: URL?
    private let checkpointQueue = DispatchQueue(label: "MVC2 session reports", qos: .utility)
    private let periodicCheckpoint = DispatchSemaphore(value: 1)
    private let receive: @MainActor (NativeGameplayEvent) -> Void
    private let condition = NSCondition()
    private var commands: [Command] = []
    private var suspensionRequested = true
    private var widescreenRequested = false
    private var owner: Thread?

    init(input: InputState, frames: NativeFrameStore, checkpoint: URL?,
         receive: @escaping @MainActor (NativeGameplayEvent) -> Void) {
        self.input = input
        self.frames = frames
        self.checkpoint = checkpoint
        self.receive = receive
        let thread = Thread { [weak self] in self?.run() }
        thread.name = "MVC2 native gameplay owner"
        thread.qualityOfService = .userInteractive
        owner = thread
        thread.start()
    }

    func start(rom: URL, saves: URL, arcade: ArcadeSettings) {
        condition.lock()
        commands.append(.start(rom: rom, saves: saves, arcade: arcade))
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
        // Prepare the system audio graph only once a game is ready to run.
        var sound: NativeAudioOutput?
        var audioActivationAttempted = false
        var nextAudioRestart = 0.0
        let period = 858.0 * 525.0 / 27_000_000.0
        var active = false
        var paused = true
        var deliveredFrame = false
        var frameNumber = 0
        var lastScene = (task: Int32(-2), step: Int32(0), scene: Int32(0), routine: Int32(0))
        var nextCheckpoint = 0.0
        var nextFrame = ProcessInfo.processInfo.systemUptime
        var error = [CChar](repeating: 0, count: 2048)
        let errorCapacity = error.count

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
                case .start(let rom, let saves, let arcade):
                    sound?.suspend()
                    if active { mvc2_gameplay_stop() }
                    active = false
                    paused = true
                    audioActivationAttempted = false
                    deliveredFrame = false
                    frameNumber = 0
                    nextCheckpoint = 0
                    input.releaseAll()
                    frames.clear()
                    send(.audioWarning(nil))
                    send(.status(.starting))
                    recordCheckpoint("Starting native engine")
                    var nativeArcade = MVC2_ARCADE_SETTINGS(
                        difficulty: Int32(arcade.difficulty), damage: Int32(arcade.damage),
                        timer_speed: Int32(arcade.timerSpeed), game_speed: Int32(arcade.gameSpeed),
                        free_play: arcade.freePlay ? 1 : 0, demo_sound: arcade.demoSound ? 1 : 0,
                        continues: arcade.continues ? 1 : 0)
                    let result = rom.path.withCString { romPath in
                        saves.path.withCString { savePath in
                            mvc2_gameplay_start(romPath, savePath, &nativeArcade, &error, errorCapacity)
                        }
                    }
                    if result == 0 {
                        let message = Self.message(error, fallback: "The native engine could not start this game data.")
                        let report = saveFailure("Starting native engine", message: message)
                        mvc2_gameplay_stop()
                        clearCheckpoint()
                        send(.status(.failed(message, report: report)))
                    } else {
                        active = true
                        // The next loop observes commands/suspension before it
                        // resumes audio or advances the first game frame.
                        if shouldPause {
                            clearCheckpoint()
                            send(.status(.paused))
                        }
                    }
                case .stop(let continuation):
                    sound?.suspend()
                    if active { mvc2_gameplay_stop() }
                    clearCheckpoint()
                    active = false
                    paused = true
                    audioActivationAttempted = false
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
                    sound?.suspend()
                    audioActivationAttempted = false
                    input.releaseAll()
                    paused = true
                    clearCheckpoint()
                    send(.status(.paused))
                }
                continue
            }
            if paused {
                paused = false
                nextFrame = ProcessInfo.processInfo.systemUptime
                lastScene.task = -2
                nextCheckpoint = 0
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
                let progress = mvc2_gameplay_progress()
                let scene = (progress.task, progress.step, progress.scene, progress.routine)
                let time = ProcessInfo.processInfo.systemUptime
                frameNumber += 1
                let sceneChanged = scene != lastScene
                if sceneChanged || time >= nextCheckpoint {
                    recordCheckpoint("Advancing game frame \(frameNumber)",
                                     progress: progress, widescreen: widescreen,
                                     immediate: false, sceneChanged: sceneChanged)
                    lastScene = scene
                    nextCheckpoint = time + 1
                }
                if mvc2_gameplay_step(&controls, &error, errorCapacity) == 0 {
                    let message = Self.message(error, fallback: "The native engine stopped while advancing a game frame.")
                    let report = saveFailure("Advancing game frame \(frameNumber)", message: message,
                                             progress: progress, widescreen: widescreen)
                    sound?.suspend()
                    mvc2_gameplay_stop()
                    clearCheckpoint()
                    active = false
                    paused = true
                    input.releaseAll()
                    frames.clear()
                    send(.status(.failed(message, report: report)))
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
                // A successful native frame precedes audio activation. A
                // route can fail independently of starting the game engine.
                let restartAudio = sound?.needsRestart == true && time >= nextAudioRestart
                if !audioActivationAttempted || restartAudio {
                    audioActivationAttempted = true
                    nextAudioRestart = time + 1
                    recordCheckpoint("\(restartAudio ? "Restarting" : "Activating") audio after frame \(frameNumber)",
                                     progress: progress, widescreen: widescreen)
                    let audio = sound ?? NativeAudioOutput()
                    sound = audio
                    do {
                        try audio.resume()
                        send(.audioWarning(nil))
                    } catch {
                        send(.audioWarning(error.localizedDescription))
                    }
                    nextCheckpoint = 0
                }
                var samples: UnsafePointer<Int16>?
                let sampleFrames = mvc2_gameplay_audio(&samples)
                if sampleFrames > 0, let samples {
                    sound?.enqueue(interleaved: samples, frames: Int(sampleFrames))
                }
            }

            nextFrame += period
            let now = ProcessInfo.processInfo.systemUptime
            // Drop accumulated debt without adding an idle frame after a stall.
            if now - nextFrame > period * 2 { nextFrame = now }
        }
    }

    private func recordCheckpoint(_ action: String, progress: MVC2_GAMEPLAY_PROGRESS? = nil,
                                  widescreen: Bool? = nil, immediate: Bool = true,
                                  sceneChanged: Bool = false) {
        guard let checkpoint else { return }
        let data = reportData(action, progress: progress, widescreen: widescreen)
        if immediate {
            checkpointQueue.sync { _ = try? data.write(to: checkpoint, options: .atomic) }
        } else if sceneChanged {
            checkpointQueue.async { _ = try? data.write(to: checkpoint, options: .atomic) }
        } else if periodicCheckpoint.wait(timeout: .now()) == .success {
            checkpointQueue.async { [periodicCheckpoint] in
                defer { periodicCheckpoint.signal() }
                try? data.write(to: checkpoint, options: .atomic)
            }
        }
    }

    private func saveFailure(_ action: String, message: String, progress: MVC2_GAMEPLAY_PROGRESS? = nil,
                             widescreen: Bool? = nil) -> URL? {
        guard let checkpoint else { return nil }
        let report = checkpoint.deletingLastPathComponent().appendingPathComponent("LastEngineFailure.txt")
        do {
            try reportData("\(action)\nEngine error: \(message)",
                           progress: progress, widescreen: widescreen).write(to: report, options: .atomic)
            return report
        } catch {
            return nil
        }
    }

    private func reportData(_ action: String, progress: MVC2_GAMEPLAY_PROGRESS?,
                            widescreen: Bool?) -> Data {
        let build = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "unknown"
        var report = "MVC2 build \(build)\n\(Date().ISO8601Format())\n\(action)\n"
        if let progress {
            report += "Task \(progress.task), step \(progress.step), scene \(progress.scene), "
            report += "routine \(progress.routine), timer \(progress.timer)\n"
        }
        if let widescreen { report += "Native widescreen \(widescreen ? "on" : "off")\n" }
        return Data(report.utf8)
    }

    private func clearCheckpoint() {
        guard let checkpoint else { return }
        // Order deletion after any pending periodic write.
        checkpointQueue.sync { _ = try? FileManager.default.removeItem(at: checkpoint) }
    }

    private static func message(_ error: [CChar], fallback: String) -> String {
        let message = String(cString: error)
        return message.isEmpty ? fallback : message
    }
}
