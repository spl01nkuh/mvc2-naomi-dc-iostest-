import Combine
import Foundation

/// Sources hold their own masks so releasing one finger never releases another
/// finger's button. The runtime can snapshot this value on its simulation thread.
final class InputState: ObservableObject {
    private let lock = NSLock()
    private var sources: [String: UInt16] = [:]

    var buttons: UInt16 {
        lock.lock()
        defer { lock.unlock() }
        let value = sources.values.reduce(0, |)
        // Opposing directions are neutral, including overlapping custom buttons.
        var result = value
        if value & 0x3000 == 0x3000 { result &= ~UInt16(0x3000) }
        if value & 0x0c00 == 0x0c00 { result &= ~UInt16(0x0c00) }
        return result
    }

    func set(source: String, buttons: UInt16) {
        lock.lock()
        let valid = buttons & NativeButton.validMask
        if valid == 0 { sources.removeValue(forKey: source) }
        else { sources[source] = valid }
        lock.unlock()
    }

    func releaseAll() {
        lock.lock()
        sources.removeAll()
        lock.unlock()
    }
}
