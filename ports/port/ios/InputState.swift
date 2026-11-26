import Combine
import Foundation

/// Sources hold their own masks so releasing one finger never releases another
/// finger's button. The runtime can snapshot this value on its simulation thread.
final class InputState: ObservableObject {
    private let lock = NSLock()
    private var sources: [String: UInt16] = [:]
    private var unsampledTouches: [String: UInt16] = [:]
    private var releasedTaps: UInt16 = 0
    private var pendingCoins: UInt32 = 0

    var buttons: UInt16 {
        lock.lock()
        defer { lock.unlock() }
        return normalizedButtons()
    }

    private func normalizedButtons(including pending: UInt16 = 0) -> UInt16 {
        let value = sources.values.reduce(pending, |)
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
        let buttons = normalizedButtons(including: releasedTaps)
        unsampledTouches.removeAll(keepingCapacity: true)
        releasedTaps = 0
        return (buttons, coin)
    }

    /// Remember action buttons until the game samples them. For a custom
    /// button, keep its complete chord, including any bound direction.
    func set(source: String, buttons: UInt16, retainTap: Bool = false) {
        lock.lock()
        defer { lock.unlock() }
        let valid = buttons & NativeButton.validMask
        let previous = sources[source] ?? 0
        if valid == 0 { sources.removeValue(forKey: source) }
        else { sources[source] = valid }
        if previous != valid {
            if retainTap && valid & ~NativeButton.directions != 0 {
                unsampledTouches[source] = valid
            } else {
                unsampledTouches.removeValue(forKey: source)
            }
        }
    }

    /// A touch that lifts between frames still gets one frame of action.
    /// UIKit cancellation, by contrast, drops the unplayed tap.
    func endTouch(source: String, cancelled: Bool = false) {
        lock.lock()
        sources.removeValue(forKey: source)
        let unsampled = unsampledTouches.removeValue(forKey: source) ?? 0
        if !cancelled { releasedTaps |= unsampled }
        lock.unlock()
    }

    func cancelTouches(sources touchSources: [String]) {
        lock.lock()
        for source in touchSources { sources.removeValue(forKey: source) }
        unsampledTouches.removeAll(keepingCapacity: true)
        releasedTaps = 0
        lock.unlock()
    }

    func releaseAll() {
        lock.lock()
        sources.removeAll()
        unsampledTouches.removeAll(keepingCapacity: true)
        releasedTaps = 0
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
