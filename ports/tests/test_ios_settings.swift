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
        try widescreenSettings()
        try heldSources()
        try shortTouchActions()
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
        options.transparentDirections = false
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
        try check(!restored.transparentDirections, "Solid directional controls must survive a round trip")
        var legacy = try JSONSerialization.jsonObject(with: encoded) as! [String: Any]
        legacy.removeValue(forKey: "transparentDirections")
        let migrated = try JSONDecoder().decode(AppSettings.self,
            from: JSONSerialization.data(withJSONObject: legacy))
        try check(migrated.transparentDirections && migrated.controls == options.controls,
                  "Older settings must gain transparent controls while preserving custom bindings")
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

    static func shortTouchActions() throws {
        let input = InputState()
        let start = NativeButton.start.rawValue
        let lp = NativeButton.lp.rawValue
        let hp = NativeButton.hp.rawValue
        let left = NativeButton.left.rawValue
        let right = NativeButton.right.rawValue
        let combo = left | lp | hp

        input.set(source: "finger", buttons: start, retainTap: true)
        input.endTouch(source: "finger")
        try check(input.buttons == 0, "Released buttons must appear released immediately")
        try check(input.snapshotForFrame().buttons == start,
                  "A START tap between frames must reach the game")
        try check(input.snapshotForFrame().buttons == 0, "A short tap must expire after one frame")

        input.set(source: "finger", buttons: combo, retainTap: true)
        input.endTouch(source: "finger")
        try check(input.snapshotForFrame().buttons == combo,
                  "A short custom tap must preserve all bindings in its chord")

        input.set(source: "hold", buttons: right | hp)
        input.set(source: "finger", buttons: combo, retainTap: true)
        input.endTouch(source: "finger")
        try check(input.snapshotForFrame().buttons == lp | hp,
                  "Buffered and held actions must neutralize conflicting directions")
        try check(input.snapshotForFrame().buttons == right | hp,
                  "A buffered tap must expire independently of overlapping holds")
        input.releaseAll()

        input.set(source: "finger", buttons: lp, retainTap: true)
        try check(input.snapshotForFrame().buttons == lp, "A held button must respond in the current frame")
        input.set(source: "finger", buttons: lp, retainTap: true)
        input.endTouch(source: "finger")
        try check(input.snapshotForFrame().buttons == 0, "Releasing an observed hold must not repeat its press")

        input.set(source: "finger", buttons: lp, retainTap: true)
        input.endTouch(source: "finger", cancelled: true)
        try check(input.snapshotForFrame().buttons == 0, "UIKit cancellation must discard an unplayed tap")
        input.set(source: "finger", buttons: lp, retainTap: true)
        input.endTouch(source: "finger")
        input.cancelTouches(sources: [])
        try check(input.snapshotForFrame().buttons == 0, "Removing the overlay must clear already released taps")

        input.set(source: "finger", buttons: start, retainTap: true)
        input.endTouch(source: "finger")
        input.releaseAll()
        try check(input.snapshotForFrame().buttons == 0, "Suspending play must clear released taps")
    }

    static func widescreenSettings() throws {
        var settings = AppSettings()
        settings.nativeWidescreen = true
        settings.arcade.freePlay = true
        settings.arcade.difficulty = 6
        settings.controls.append(TouchControl(label: "LP + HP", x: 0.5, y: 0.5,
            buttons: NativeButton.lp.rawValue | NativeButton.hp.rawValue, custom: true))
        let encoded = try JSONEncoder().encode(settings)
        let restored = try JSONDecoder().decode(AppSettings.self, from: encoded)
        try check(restored.nativeWidescreen && restored.controls == settings.controls,
                  "Native widescreen and the custom layout must survive relaunch")
        try check(restored.arcade == settings.arcade, "Arcade settings must survive relaunch")
        try check(restored.presentationAspect(sourceWidth: 854, sourceHeight: 480) == .native,
                  "The original default fit must preserve a widescreen source's proportions")

        var legacy = try JSONSerialization.jsonObject(with: encoded) as! [String: Any]
        legacy.removeValue(forKey: "nativeWidescreen")
        legacy.removeValue(forKey: "arcade")
        let legacyData = try JSONSerialization.data(withJSONObject: legacy)
        let migrated = try JSONDecoder().decode(AppSettings.self, from: legacyData)
        try check(!migrated.nativeWidescreen && migrated.controls == settings.controls,
                  "An older settings file must preserve its layout and keep the original 4:3 mode")
        try check(migrated.arcade == ArcadeSettings(), "Older settings must use the arcade defaults")
        try check(migrated.presentationAspect(sourceWidth: 640, sourceHeight: 480) == .fourThree,
                  "Disabling native widescreen must retain the original display fit")

        // The paused display retains its previous frame until the native owner
        // resumes. Both toggle directions must respect that frame's geometry.
        settings.nativeWidescreen = false
        try check(settings.presentationAspect(sourceWidth: 854, sourceHeight: 480) == .native,
                  "Disabling widescreen while paused must preserve the last wide frame's proportions")
        try check(settings.presentationAspect(sourceWidth: 640, sourceHeight: 480) == .fourThree,
                  "The first new 4:3 frame must restore the original display fit")
        settings.nativeWidescreen = true
        try check(settings.presentationAspect(sourceWidth: 640, sourceHeight: 480) == .sixteenNine,
                  "Widescreen must fit 4:3 non-fight frames into a 16:9 viewport")

        settings.aspect = .fill
        try check(settings.presentationAspect(sourceWidth: 854, sourceHeight: 480) == .fill,
                  "Native widescreen must retain an explicitly selected fill adjustment")
        settings.aspect = .native
        try check(settings.presentationAspect(sourceWidth: 640, sourceHeight: 480) == .native,
                  "Source aspect must remain available in both native render modes")
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
        try check(NativeCanvasFrame(width: 0, height: 1, argb: []) == nil, "Empty frames are invalid")
        try check(NativeCanvasFrame(width: 2, height: 1, argb: [0]) == nil, "A truncated frame must be rejected")
        try check(NativeCanvasFrame(width: 4097, height: 1, argb: []) == nil, "Frame dimensions must be bounded before allocation")
        var rendererPixels: [UInt32] = [0x7f123456, 0xffaabbcc]
        let copiedFrame = rendererPixels.withUnsafeBufferPointer {
            NativeCanvasFrame(width: 2, height: 1, argb: $0)
        }
        rendererPixels[0] = 0
        guard let frame = copiedFrame else {
            throw TestFailure(description: "A complete native ARGB frame must be accepted")
        }
        try check(Array(frame.bgra) == [0x56, 0x34, 0x12, 0x7f, 0xcc, 0xbb, 0xaa, 0xff],
                  "The frame must retain its BGRA bytes after the renderer reuses its buffer")
        frames.publish(frame: frame)
        let (published, generation) = frames.snapshot()
        try check(published?.bgra == frame.bgra && generation == 1, "Publishing must retain complete frame pixels")
        frames.clear()
        let (cleared, nextGeneration) = frames.snapshot()
        try check(cleared == nil && nextGeneration == generation + 1, "Stopping a session must remove its old game frame")
        let independent = NativeFrameStore()
        try check(independent.snapshot().0 == nil && independent.snapshot().1 == 0, "A different session must not inherit a frame")

        // A mode switch changes the native output width. The host must retain
        // its whole last column rather than copying only the old 640 pixels.
        var widePixels = [UInt32](repeating: 0xff123456, count: 854 * 480)
        widePixels[widePixels.count - 1] = 0xffabcdef
        guard let wide = widePixels.withUnsafeBufferPointer({
            NativeCanvasFrame(width: 854, height: 480, argb: $0)
        }) else { throw TestFailure(description: "A complete widescreen native frame must be accepted") }
        frames.publish(frame: wide)
        let (publishedWide, _) = frames.snapshot()
        try check(publishedWide?.width == 854 && publishedWide?.height == 480,
                  "A native aspect change must publish the actual frame dimensions")
        try check(Array(wide.bgra.suffix(4)) == [0xef, 0xcd, 0xab, 0xff],
                  "Widescreen frame copying must preserve the last native pixel")
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
