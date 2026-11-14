import Foundation

/// Imports cartridge bytes as assets. This path never loads or executes SH-4
/// code. Only the supported complete NAOMI mvsc2 set is accepted.
enum NativeAssetImporter {
    private struct ActiveSet: Codable {
        let version: Int
        let directory: String
    }

    private struct ValidatedSet: Codable {
        let version: Int
        let fileCount: Int
    }

    struct ImportError: LocalizedError {
        let message: String
        var errorDescription: String? { message }
    }

    /// Lightweight restore: CRC validation is performed during import. On
    /// launch, check the committed manifest and regular file sizes, not 124 MiB
    /// of checksums on the main thread. Asset reads still enforce their bounds.
    static func existingROM(root: URL) -> URL? {
        let decoder = JSONDecoder()
        guard let data = try? Data(contentsOf: root.appendingPathComponent("active.json")),
              let active = try? decoder.decode(ActiveSet.self, from: data),
              active.version == 1, UUID(uuidString: active.directory) != nil else { return nil }
        let directory = root.appendingPathComponent("sets", isDirectory: true)
            .appendingPathComponent(active.directory, isDirectory: true)
        guard let marker = try? Data(contentsOf: directory.appendingPathComponent("validated.json")),
              let validated = try? decoder.decode(ValidatedSet.self, from: marker),
              validated.version == 1 else { return nil }
        var count = 0
        guard let manifest = mvc2_rom_manifest(&count), count == validated.fileCount else { return nil }
        for i in 0..<count {
            let entry = manifest[i]
            let file = directory.appendingPathComponent(String(cString: entry.name))
            guard let values = try? file.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey]),
                  values.isRegularFile == true, values.isSymbolicLink != true,
                  values.fileSize == Int(entry.size) else { return nil }
        }
        return directory
    }

    static func importROM(from source: URL, root: URL) async throws -> URL {
        try await Task.detached(priority: .userInitiated) {
            try importOnWorker(from: source, root: root)
        }.value
    }

    private static func importOnWorker(from source: URL, root: URL) throws -> URL {
        let manager = FileManager.default
        let access = source.startAccessingSecurityScopedResource()
        defer { if access { source.stopAccessingSecurityScopedResource() } }
        let sets = root.appendingPathComponent("sets", isDirectory: true)
        try manager.createDirectory(at: sets, withIntermediateDirectories: true)
        let identifier = UUID().uuidString
        let staging = sets.appendingPathComponent(".import-\(identifier)", isDirectory: true)
        let committed = sets.appendingPathComponent(identifier, isDirectory: true)
        try manager.createDirectory(at: staging, withIntermediateDirectories: false)
        var completed = false
        defer {
            if !completed {
                try? manager.removeItem(at: staging)
                try? manager.removeItem(at: committed)
            }
        }

        var coordinationError: NSError?
        var importError: Error?
        NSFileCoordinator().coordinate(readingItemAt: source, options: .withoutChanges,
                                       error: &coordinationError) { coordinatedURL in
            var error = [CChar](repeating: 0, count: 1024)
            let errorCapacity = error.count
            let ok = coordinatedURL.path.withCString { zipPath in
                staging.path.withCString { destination in
                    mvc2_rom_import_zip(zipPath, destination, &error, errorCapacity)
                }
            }
            if ok == 0 {
                importError = ImportError(message: String(cString: error))
            }
        }
        if let error = coordinationError { throw error }
        if let error = importError { throw error }

        var count = 0
        _ = mvc2_rom_manifest(&count)
        let encoder = JSONEncoder()
        try encoder.encode(ValidatedSet(version: 1, fileCount: count))
            .write(to: staging.appendingPathComponent("validated.json"), options: .atomic)
        try manager.moveItem(at: staging, to: committed)
        var backupExcluded = URLResourceValues()
        backupExcluded.isExcludedFromBackup = true
        var backupURL = committed
        try? backupURL.setResourceValues(backupExcluded)
        // This small atomic pointer switch leaves the previous import intact
        // if copying, CRC validation, or the commit fails (including low disk).
        let previous = existingROM(root: root)
        try encoder.encode(ActiveSet(version: 1, directory: identifier))
            .write(to: root.appendingPathComponent("active.json"), options: .atomic)
        completed = true
        if let previous, previous != committed { try? manager.removeItem(at: previous) }
        return committed
    }
}
