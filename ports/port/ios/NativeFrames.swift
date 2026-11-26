import Foundation

/// An immutable frame produced by the native renderer. No default or generated
/// picture is supplied: a producer must publish actual frame pixels.
struct NativeCanvasFrame {
    let width: Int
    let height: Int
    let bgra: Data

    init?(width: Int, height: Int, argb: [UInt32]) {
        guard let frame = argb.withUnsafeBufferPointer({
            NativeCanvasFrame(width: width, height: height, argb: $0)
        }) else { return nil }
        self = frame
    }

    /// Copy the renderer's borrowed 0xAARRGGBB words before the next step.
    /// On Apple CPUs their byte order already matches the Metal BGRA texture.
    init?(width: Int, height: Int, argb: UnsafeBufferPointer<UInt32>) {
        guard width > 0, height > 0, width <= 4096, height <= 4096,
              argb.count == width * height else { return nil }
        self.width = width
        self.height = height
        #if _endian(little)
        bgra = Data(buffer: argb)
        #else
        bgra = argb.map(\.littleEndian).withUnsafeBufferPointer { Data(buffer: $0) }
        #endif
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
