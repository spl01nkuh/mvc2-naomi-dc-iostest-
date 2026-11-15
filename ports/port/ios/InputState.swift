import Combine
import Foundation

/// Sources hold their own masks so releasing one finger never releases another
/// finger's button. The runtime can snapshot this value on its simulation thread.
final class InputState: ObservableObject {
    private let lock = NSLock()
    private var sources: [String: UInt16] = [:]
    private var pendingCoins: UInt32 = 0

    var buttons: UInt16 {
        lock.lock()
        defer { lock.unlock() }
        return normalizedButtons()
    }

    private func normalizedButtons() -> UInt16 {
        let value = sources.values.reduce(0, |)
        // Opposing directions are neutral, including overlapping custom buttons.
        var result = value
        if value & 0x3000 == 0x3000 { result &= ~UInt16(0x3000) }
        if value & 0x0c00 == 0x0c00 { result &= ~UInt16(0x0c00) }
        return result
    }

    /// A simulation frame consumes its player mask and one credit event under
    /// the same lock, so UI changes cannot split a native input snapshot.
    func snapshotForFrame() -> (buttons: UInt16, coin: Bool) {
        lock.lock()
        defer { lock.unlock() }
        let coin = pendingCoins > 0
        if coin { pendingCoins -= 1 }
        return (normalizedButtons(), coin)
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
        pendingCoins = 0
        lock.unlock()
    }

    /// The arcade credit backend expects a frame event, not a player mask.
    @discardableResult
    func insertCoin() -> Bool {
        lock.lock()
        defer { lock.unlock() }
        guard pendingCoins < UInt32.max else { return false }
        pendingCoins += 1
        return true
    }

    /// Call once per simulated game frame when filling HOST_INPUT.coin.
    func takeCoin() -> Bool {
        lock.lock()
        defer { lock.unlock() }
        guard pendingCoins > 0 else { return false }
        pendingCoins -= 1
        return true
    }
}
