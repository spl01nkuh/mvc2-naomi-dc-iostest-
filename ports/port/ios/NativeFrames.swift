import Foundation

/// An immutable frame produced by the native renderer. No default or generated
/// picture is supplied: a producer must publish actual frame pixels.
struct NativeCanvasFrame {
    let width: Int
    let height: Int
    let rgba: [UInt8]

    init?(width: Int, height: Int, rgba: [UInt8]) {
        guard width > 0, height > 0, width <= 4096, height <= 4096,
              rgba.count == width * height * 4 else { return nil }
        self.width = width
        self.height = height
        self.rgba = rgba
    }

    /// `rd_end_frame` writes numeric 0xAARRGGBB words. Convert their components
    /// explicitly so presentation never depends on the CPU's byte order.
    init?(width: Int, height: Int, argb: [UInt32]) {
        guard let frame = argb.withUnsafeBufferPointer({
            NativeCanvasFrame(width: width, height: height, argb: $0)
        }) else { return nil }
        self = frame
    }

    /// Copy the native renderer's borrowed pixels before its next step.
    init?(width: Int, height: Int, argb: UnsafeBufferPointer<UInt32>) {
        guard width > 0, height > 0, width <= 4096, height <= 4096,
              argb.count == width * height else { return nil }
        var pixels = [UInt8](repeating: 0, count: argb.count * 4)
        for (index, word) in argb.enumerated() {
            let offset = index * 4
            pixels[offset] = UInt8(truncatingIfNeeded: word >> 16)
            pixels[offset + 1] = UInt8(truncatingIfNeeded: word >> 8)
            pixels[offset + 2] = UInt8(truncatingIfNeeded: word)
            pixels[offset + 3] = UInt8(truncatingIfNeeded: word >> 24)
        }
        self.width = width
        self.height = height
        self.rgba = pixels
    }
}

extension Notification.Name {
    static let mvc2NativeFrame = Notification.Name("MVC2NativeFrame")
}

/// Owned by the native runtime session. A new session starts empty; clearing it
/// removes the last frame when that session stops. Notifications contain no
/// frame bytes and are delivered on the main queue for presentation views.
final class NativeFrameStore {
    private let lock = NSLock()
    private var frame: NativeCanvasFrame?
    private var generation: UInt64 = 0
    private var notificationPending = false

    func publish(frame: NativeCanvasFrame) {
        lock.lock()
        self.frame = frame
        generation &+= 1
        lock.unlock()
        notifyPresentation()
    }

    func clear() {
        lock.lock()
        frame = nil
        generation &+= 1
        lock.unlock()
        notifyPresentation()
    }

    func snapshot() -> (NativeCanvasFrame?, UInt64) {
        lock.lock()
        defer { lock.unlock() }
        return (frame, generation)
    }

    private func notifyPresentation() {
        lock.lock()
        guard !notificationPending else { lock.unlock(); return }
        notificationPending = true
        lock.unlock()
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.lock.lock()
            self.notificationPending = false
            self.lock.unlock()
            NotificationCenter.default.post(name: .mvc2NativeFrame, object: self)
        }
    }
}
