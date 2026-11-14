// macOS host check for persisted iOS settings and concurrent touch-source holds.
// Build with AppSettings.swift, InputState.swift and NativeFrames.swift.
import Foundation
import Dispatch

private struct TestFailure: Error, CustomStringConvertible {
    let description: String
}

private func check(_ condition: @autoclosure () -> Bool, _ message: String) throws {
    if !condition() { throw TestFailure(description: message) }
}

@main
struct IOSSettingsTests {
    static func main() throws {
        try persistence()
        try heldSources()
        try concurrentSources()
        try coinEvents()
        try frames()
        #if MVC2_TEST_NATIVE_BRIDGE
        try nativeStick()
        print("Native stick bridge checks passed")
        #endif
        print("iOS settings persistence and input ownership checks passed")
    }

    static func persistence() throws {
        var options = AppSettings()
        options.filter = .geom
        options.filterStrength = .nan
        options.screenScale = -.infinity
        options.screenWidth = 100
        options.screenHeight = -3
        options.screenOffsetX = -.infinity
        options.screenOffsetY = 40
        options.touchOpacity = -1
        options.stickSensitivity = 100
        options.stickDeadZone = 1
        options.controls[0].x = .nan
        options.controls[0].y = 4
        options.controls[0].size = -.infinity
        options.controls[1].id = options.controls[0].id
        options.controls[1].buttons = .max
        options.controls[1].label = String(repeating: "🙂", count: 20)
        options.sanitize()

        try check(options.filterStrength == 0.75, "Nonfinite filter strength must recover its default")
        try check(options.screenScale == 1 && options.screenWidth == 2 && options.screenHeight == 0.25, "Corrupt screen geometry must stay usable")
        try check(options.screenOffsetX == 0 && options.screenOffsetY == 1, "Invalid positions must recover or clamp")
        try check(options.touchOpacity == 0.1 && options.stickSensitivity == 1 && options.stickDeadZone == 0.35, "Input options must respect supported ranges")
        try check(options.controls[0].x == 0.5 && options.controls[0].y == 1 && options.controls[0].size == 1, "Malformed saved controls must remain reachable")
        try check(Set(options.controls.map(\.id)).count == options.controls.count, "Duplicate restored control identifiers must be repaired")
        try check(options.controls[1].label.count == 12, "Labels must truncate by complete characters")
        try check(options.controls[1].buttons == NativeButton.validMask, "Reserved player bits must be removed")

        let encoded = try JSONEncoder().encode(options)
        let restored = try JSONDecoder().decode(AppSettings.self, from: encoded)
        try check(restored.filter == .geom && restored.controls == options.controls, "Saved filter, custom bindings and identifiers must survive a round trip")
        try check(NativeButton.allCases.reduce(UInt16(0)) { $0 | $1.rawValue } == NativeButton.validMask, "UI bindings must match MVC2's complete player switch mask")
        var emptyLayout = AppSettings()
        emptyLayout.controls.removeAll()
        emptyLayout.sanitize()
        try check(emptyLayout.controls.contains(where: \.isDirection), "An empty stored layout must restore a reachable direction control")
    }

    static func heldSources() throws {
        let input = InputState()
        input.set(source: "finger", buttons: NativeButton.lp.rawValue)
        input.set(source: "custom", buttons: NativeButton.lp.rawValue | NativeButton.hp.rawValue)
        input.set(source: "finger", buttons: 0)
        try check(input.buttons == NativeButton.lp.rawValue | NativeButton.hp.rawValue, "Lifting one finger must preserve a custom button's overlapping hold")
        input.set(source: "left", buttons: NativeButton.left.rawValue)
        input.set(source: "right", buttons: NativeButton.right.rawValue | NativeButton.down.rawValue)
        try check(input.buttons & NativeButton.directions == NativeButton.down.rawValue, "Opposing sources must neutralize only their shared axis")
        input.set(source: "right", buttons: 0)
        try check(input.buttons & NativeButton.directions == NativeButton.left.rawValue, "A surviving direction must reactivate when its opposite releases")
        input.set(source: "custom", buttons: 0x400f)
        try check(input.buttons == NativeButton.left.rawValue, "Reserved-only updates must release the source's previous bindings")
        input.releaseAll()
        try check(input.buttons == 0, "Modal and background cancellation must clear every held input")
    }

    static func concurrentSources() throws {
        let input = InputState()
        DispatchQueue.concurrentPerform(iterations: 256) { index in
            input.set(source: "finger-\(index)", buttons: NativeButton.lk.rawValue)
            _ = input.buttons
        }
        DispatchQueue.concurrentPerform(iterations: 128) { index in
            input.set(source: "finger-\(index)", buttons: 0)
        }
        try check(input.buttons == NativeButton.lk.rawValue, "Concurrent release must preserve other active sources")
        DispatchQueue.concurrentPerform(iterations: 128) { index in
            input.set(source: "finger-\(index + 128)", buttons: 0)
        }
        try check(input.buttons == 0, "Every concurrent source must be independently releasable")
    }

    static func frames() throws {
        let frames = NativeFrameStore()
        try check(frames.snapshot().0 == nil, "A new session must wait for actual pixels")
        try check(NativeCanvasFrame(width: 0, height: 1, rgba: []) == nil, "Empty frames are invalid")
        try check(NativeCanvasFrame(width: 1, height: 1, rgba: [0]) == nil, "A truncated RGBA pixel must be rejected")
        try check(NativeCanvasFrame(width: 4097, height: 1, argb: []) == nil, "Frame dimensions must be bounded before allocation")
        guard let frame = NativeCanvasFrame(width: 2, height: 1, argb: [0x7f123456, 0xffaabbcc]) else {
            throw TestFailure(description: "A complete native ARGB frame must be accepted")
        }
        try check(frame.rgba == [0x12, 0x34, 0x56, 0x7f, 0xaa, 0xbb, 0xcc, 0xff],
                  "Native ARGB words must become explicit RGBA component bytes")
        frames.publish(frame: frame)
        let (published, generation) = frames.snapshot()
        try check(published?.rgba == frame.rgba && generation == 1, "Publishing must retain complete frame pixels")
        frames.clear()
        let (cleared, nextGeneration) = frames.snapshot()
        try check(cleared == nil && nextGeneration == generation + 1, "Stopping a session must remove its old game frame")
        let independent = NativeFrameStore()
        try check(independent.snapshot().0 == nil && independent.snapshot().1 == 0, "A different session must not inherit a frame")
    }

    static func coinEvents() throws {
        let input = InputState()
        try check(input.insertCoin() && input.insertCoin(), "Independent coin taps must both queue")
        try check(input.buttons == 0, "Inserting a coin must not invent a player switch bit")
        try check(input.takeCoin() && input.takeCoin() && !input.takeCoin(), "Each native frame must consume at most one queued coin")
        input.insertCoin()
        input.releaseAll()
        try check(!input.takeCoin(), "Interruptions must not replay a queued coin after resuming")
    }

    #if MVC2_TEST_NATIVE_BRIDGE
    static func nativeStick() throws {
        try check(mvc2_input_stick_buttons(0.4, 0, 0.01, 0) == 0, "Low sensitivity must require more stick travel")
        try check(mvc2_input_stick_buttons(0.4, 0, 1, 0) == NativeButton.right.rawValue, "High sensitivity must accept smaller stick travel")
        try check(mvc2_input_stick_buttons(0.3, 0, 1, 0.35) == 0, "A radial dead zone must suppress smaller stick travel")
        try check(mvc2_input_stick_buttons(1, 1, 0.5, 0) == NativeButton.down.rawValue | NativeButton.right.rawValue, "UIKit's positive Y must map to down through the native helper")
        try check(mvc2_input_stick_buttons(.nan, 1, 1, 0) == 0, "Malformed native axes must be neutral")
    }
    #endif
}
