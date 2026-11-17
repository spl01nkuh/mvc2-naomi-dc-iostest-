import Combine
import Foundation
import AVFoundation
import UIKit

/// Owns validated assets, preferences and the experimental native game session.
@MainActor
final class DevelopmentStore: ObservableObject {
    @Published var settings: AppSettings {
        didSet {
            runtime?.setWidescreen(settings.nativeWidescreen)
            saveSettings()
        }
    }
    @Published private(set) var isImporting = false
    @Published private(set) var hasImportedData = false
    @Published private(set) var importMessage: String?
    @Published private(set) var importFailure: String?
    @Published private(set) var storageFailure: String?
    @Published private(set) var settingsFailure: String?
    @Published private(set) var gameStatus: NativeGameplayStatus = .stopped
    @Published private(set) var sessionHasFrame = false
    @Published private(set) var audioWarning: String?
    @Published private(set) var isStopping = false
    @Published private(set) var isUserPaused = false
    @Published private(set) var isAudioInterrupted = false

    let input = InputState()
    let frames = NativeFrameStore()
    private let settingsURL: URL?
    private let assetsRoot: URL?
    private let savesRoot: URL?
    private var runtime: NativeGameplaySession!
    private var hostSuspended = true
    private var applicationInactive = true
    private var interruptions: AnyCancellable?
    private var applicationObservers: [AnyCancellable] = []

    var canImport: Bool { assetsRoot != nil && !isImporting && !isStopping }
    var canStart: Bool {
        hasImportedData && savesRoot != nil && !isImporting && !isStopping && !gameStatus.isOpen
    }
    var canPause: Bool {
        gameStatus == .running || gameStatus == .paused
    }
    var gameFailure: String? {
        if case .failed(let message) = gameStatus { return "Native engine failed: \(message)" }
        return nil
    }
    var storageLocation: String { "Documents/MVC2/GameData" }

    init() {
        let manager = FileManager.default
        var restored = AppSettings()
        var preferences: URL?
        var assets: URL?
        var saves: URL?
        var failure: String?
        var storageError: String?

        do {
            let documents = try manager.url(for: .documentDirectory, in: .userDomainMask,
                                            appropriateFor: nil, create: true)
            let root = documents.appendingPathComponent("MVC2", isDirectory: true)
            try manager.createDirectory(at: root, withIntermediateDirectories: true)
            let dataRoot = root.appendingPathComponent("GameData", isDirectory: true)
            try manager.createDirectory(at: dataRoot, withIntermediateDirectories: true)
            assets = dataRoot
            let saveRoot = root.appendingPathComponent("SaveData", isDirectory: true)
            try manager.createDirectory(at: saveRoot, withIntermediateDirectories: true)
            saves = saveRoot
            preferences = root.appendingPathComponent("settings.json")
            if let preferences, manager.fileExists(atPath: preferences.path) {
                do {
                    restored = try JSONDecoder().decode(AppSettings.self,
                                                        from: Data(contentsOf: preferences))
                } catch {
                    failure = "Saved settings could not be read. Default settings are in use."
                }
            }
        } catch {
            storageError = "The app could not open its Documents folder: \(error.localizedDescription)"
        }

        restored.sanitize()
        #if targetEnvironment(simulator)
        // CI screenshots exercise the actual UIKit artwork without changing
        // saved preferences or adding a preview route to the device app.
        let previewPrefix = "--mvc2-controls-preview="
        if let argument = ProcessInfo.processInfo.arguments.first(where: { $0.hasPrefix(previewPrefix) }),
           let style = DirectionStyle(rawValue: String(argument.dropFirst(previewPrefix.count))) {
            restored.directionStyle = style
        }
        let widescreenPrefix = "--mvc2-widescreen-preview="
        if let argument = ProcessInfo.processInfo.arguments.first(where: { $0.hasPrefix(widescreenPrefix) }) {
            restored.nativeWidescreen = argument == widescreenPrefix + "on"
        }
        #endif
        settings = restored
        settingsURL = preferences
        assetsRoot = assets
        savesRoot = saves
        storageFailure = storageError
        settingsFailure = failure
        if let assets {
            hasImportedData = NativeAssetImporter.existingROM(root: assets) != nil
        }
        runtime = NativeGameplaySession(input: input, frames: frames) { [weak self] event in
            self?.receive(event)
        }
        runtime.setWidescreen(settings.nativeWidescreen)
        applicationInactive = UIApplication.shared.applicationState != .active
        interruptions = NotificationCenter.default.publisher(for: AVAudioSession.interruptionNotification)
            .receive(on: DispatchQueue.main)
            .sink { [weak self] notification in self?.handleInterruption(notification) }
        applicationObservers = [
            NotificationCenter.default.publisher(for: UIApplication.willResignActiveNotification)
                .sink { [weak self] _ in self?.setApplicationActive(false) },
            NotificationCenter.default.publisher(for: UIApplication.didBecomeActiveNotification)
                .sink { [weak self] _ in self?.setApplicationActive(true) }
        ]
    }

    func importArchive(_ source: URL) async {
        guard let root = assetsRoot, !isImporting, !isStopping else { return }
        input.releaseAll()
        isImporting = true
        importMessage = nil
        importFailure = nil
        defer { isImporting = false }

        // The importer may remove the previous committed set. Wait until the
        // permanent native owner has released all of its asset handles first.
        await runtime.stop()
        do {
            _ = try await NativeAssetImporter.importROM(from: source, root: root)
            hasImportedData = true
            importMessage = "ZIP imported and validated. Start Game launches the experimental native engine."
        } catch {
            importFailure = error.localizedDescription
            // The importer preserves the previous committed set on failure.
            hasImportedData = NativeAssetImporter.existingROM(root: root) != nil
        }
    }

    func startGame() {
        guard canStart, let root = assetsRoot, let saves = savesRoot else { return }
        guard let rom = NativeAssetImporter.existingROM(root: root) else {
            hasImportedData = false
            importFailure = "The validated game files are missing or changed. Import the complete ZIP again."
            return
        }
        input.releaseAll()
        isUserPaused = false
        audioWarning = nil
        sessionHasFrame = false
        gameStatus = .starting
        synchronizeSuspension()
        runtime.start(rom: rom, saves: saves)
    }

    func stopGame() async {
        guard !isStopping else { return }
        isStopping = true
        input.releaseAll()
        await runtime.stop()
        gameStatus = .stopped
        sessionHasFrame = false
        isUserPaused = false
        isStopping = false
    }

    func togglePause() {
        guard canPause else { return }
        isUserPaused.toggle()
        input.releaseAll()
        synchronizeSuspension()
    }

    func setHostSuspended(_ suspended: Bool) {
        hostSuspended = suspended
        if suspended { input.releaseAll() }
        synchronizeSuspension()
    }

    private func synchronizeSuspension() {
        runtime.setSuspended(hostSuspended || applicationInactive || isUserPaused ||
                             isAudioInterrupted || isImporting)
    }

    private func setApplicationActive(_ active: Bool) {
        applicationInactive = !active
        if !active {
            input.releaseAll()
            saveSettings()
        }
        synchronizeSuspension()
    }

    private func receive(_ event: NativeGameplayEvent) {
        switch event {
        case .status(let status):
            gameStatus = status
            switch status {
            case .stopped, .starting, .failed: sessionHasFrame = false
            case .running, .paused: break
            }
        case .firstFrame: sessionHasFrame = true
        case .audioWarning(let message): audioWarning = message
        }
    }

    private func handleInterruption(_ notification: Notification) {
        guard let raw = notification.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt,
              let type = AVAudioSession.InterruptionType(rawValue: raw) else { return }
        if type == .began {
            isAudioInterrupted = true
        } else {
            let rawOptions = notification.userInfo?[AVAudioSessionInterruptionOptionKey] as? UInt ?? 0
            let options = AVAudioSession.InterruptionOptions(rawValue: rawOptions)
            isAudioInterrupted = false
            if gameStatus.isOpen && !options.contains(.shouldResume) { isUserPaused = true }
        }
        input.releaseAll()
        synchronizeSuspension()
    }

    func reportPickerFailure(_ error: Error) {
        // Closing the Files picker without choosing a file is not an error.
        let nsError = error as NSError
        guard !(nsError.domain == NSCocoaErrorDomain && nsError.code == NSUserCancelledError) else { return }
        importFailure = error.localizedDescription
    }

    func saveSettings() {
        guard let settingsURL else { return }
        var validSettings = settings
        validSettings.sanitize()
        do {
            try JSONEncoder().encode(validSettings).write(to: settingsURL, options: .atomic)
            settingsFailure = nil
        } catch {
            settingsFailure = "Settings could not be saved: \(error.localizedDescription)"
        }
    }
}
