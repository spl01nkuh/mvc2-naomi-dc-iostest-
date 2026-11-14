import Combine
import Foundation

/// The development host owns only imported assets and user preferences. It does
/// not start a game session or create frames when the native game is absent.
@MainActor
final class DevelopmentStore: ObservableObject {
    @Published var settings: AppSettings {
        didSet { saveSettings() }
    }
    @Published private(set) var isImporting = false
    @Published private(set) var hasImportedData = false
    @Published private(set) var importMessage: String?
    @Published private(set) var importFailure: String?
    @Published private(set) var storageFailure: String?
    @Published private(set) var settingsFailure: String?

    let input = InputState()
    let frames = NativeFrameStore()
    private let settingsURL: URL?
    private let assetsRoot: URL?

    var canImport: Bool { assetsRoot != nil && !isImporting }
    var storageLocation: String { "Documents/MVC2/GameData" }

    init() {
        let manager = FileManager.default
        var restored = AppSettings()
        var preferences: URL?
        var assets: URL?
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
        settings = restored
        settingsURL = preferences
        assetsRoot = assets
        storageFailure = storageError
        settingsFailure = failure
        if let assets {
            hasImportedData = NativeAssetImporter.existingROM(root: assets) != nil
        }
    }

    func importArchive(_ source: URL) async {
        guard let root = assetsRoot, !isImporting else { return }
        input.releaseAll()
        isImporting = true
        importMessage = nil
        importFailure = nil
        defer { isImporting = false }

        do {
            _ = try await NativeAssetImporter.importROM(from: source, root: root)
            hasImportedData = true
            importMessage = "ZIP imported and validated. Native gameplay is not available in this development build."
        } catch {
            importFailure = error.localizedDescription
            // The importer preserves the previous committed set on failure.
            hasImportedData = NativeAssetImporter.existingROM(root: root) != nil
        }
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
