import Foundation

enum CRTFilter: Int, Codable, CaseIterable, Identifiable {
    case off, sharp, zfast, lottes, caligari, easyMode, composite, aperture, geom, hyllian
    var id: Int { rawValue }
    var title: String {
        ["Off", "Sharp Bilinear", "CRT zfast", "CRT Lottes", "CRT Caligari", "CRT EasyMode", "Composite TV", "CRT Aperture", "CRT Geom", "CRT Hyllian"][rawValue]
    }
}

enum ScreenAspect: String, Codable, CaseIterable, Identifiable {
    case fourThree, sixteenNine, native, fill
    var id: String { rawValue }
    var title: String {
        switch self {
        case .fourThree: return "4:3"
        case .sixteenNine: return "16:9"
        case .native: return "Native pixels"
        case .fill: return "Fill"
        }
    }
}

enum DirectionStyle: String, Codable, CaseIterable, Identifiable {
    case hybrid, stick, dpad
    var id: String { rawValue }
    var title: String { self == .dpad ? "D-pad" : rawValue.capitalized }
}

// Matches port/include/naomi_input.h's JVS values. Coin insertion is a separate
// frame event in HOST_INPUT, never a player switch bit.
enum NativeButton: UInt16, CaseIterable, Identifiable {
    case start = 0x8000, up = 0x2000, down = 0x1000, left = 0x0800, right = 0x0400
    case lp = 0x0200, hp = 0x0100, a1 = 0x0080, lk = 0x0040, hk = 0x0020, a2 = 0x0010
    var id: UInt16 { rawValue }
    var title: String {
        switch self {
        case .start: return "START"; case .up: return "↑"; case .down: return "↓"
        case .left: return "←"; case .right: return "→"; case .lp: return "LP"
        case .hp: return "HP"; case .a1: return "A1"; case .lk: return "LK"
        case .hk: return "HK"; case .a2: return "A2"
        }
    }
    static let directions: UInt16 = 0x3c00
    static let validMask: UInt16 = 0xbff0
}

struct TouchControl: Codable, Identifiable, Equatable {
    var id = UUID()
    var label: String
    var x: Double
    var y: Double
    var size: Double = 1
    var buttons: UInt16
    var isDirection = false
    var custom = false

    static var defaults: [TouchControl] {
        [TouchControl(label: "Direction", x: 0.13, y: 0.72, buttons: 0, isDirection: true),
         TouchControl(label: "LP", x: 0.73, y: 0.62, buttons: NativeButton.lp.rawValue),
         TouchControl(label: "HP", x: 0.83, y: 0.62, buttons: NativeButton.hp.rawValue),
         TouchControl(label: "A1", x: 0.93, y: 0.62, buttons: NativeButton.a1.rawValue),
         TouchControl(label: "LK", x: 0.73, y: 0.84, buttons: NativeButton.lk.rawValue),
         TouchControl(label: "HK", x: 0.83, y: 0.84, buttons: NativeButton.hk.rawValue),
         TouchControl(label: "A2", x: 0.93, y: 0.84, buttons: NativeButton.a2.rawValue),
         TouchControl(label: "START", x: 0.50, y: 0.90, buttons: NativeButton.start.rawValue)]
    }
}

struct ArcadeSettings: Codable, Equatable {
    var difficulty = 1
    var damage = 1
    var timerSpeed = 1
    var gameSpeed = 0
    var freePlay = false
    var demoSound = true
    var continues = true

    mutating func sanitize() {
        difficulty = min(max(difficulty, 0), 7)
        damage = min(max(damage, 0), 3)
        timerSpeed = min(max(timerSpeed, 0), 3)
        gameSpeed = min(max(gameSpeed, 0), 2)
    }
}

struct AppSettings: Codable {
    var filter: CRTFilter = .off
    var filterStrength = 0.75
    var aspect: ScreenAspect = .fourThree
    var nativeWidescreen = false
    var arcade = ArcadeSettings()
    var screenScale = 1.0
    var screenWidth = 1.0
    var screenHeight = 1.0
    var screenOffsetX = 0.0
    var screenOffsetY = 0.0
    var touchEnabled = true
    var touchOpacity = 0.65
    var stickSensitivity = 0.5
    var stickDeadZone = 0.0
    var directionStyle: DirectionStyle = .hybrid
    var transparentDirections = true
    var controls = TouchControl.defaults

    func presentationAspect(sourceWidth: Int, sourceHeight: Int) -> ScreenAspect {
        if nativeWidescreen && aspect != .fill { return .sixteenNine }
        guard aspect == .fourThree else { return aspect }
        let hasWideFrame = sourceWidth > 0 && sourceHeight > 0 &&
            Double(sourceWidth) / Double(sourceHeight) > 4.0 / 3.0
        if hasWideFrame { return .native }
        return .fourThree
    }

    private enum CodingKeys: String, CodingKey {
        case filter, filterStrength, aspect, nativeWidescreen, arcade
        case screenScale, screenWidth, screenHeight, screenOffsetX, screenOffsetY
        case touchEnabled, touchOpacity, stickSensitivity, stickDeadZone, directionStyle
        case transparentDirections, controls
    }

    init() {}

    /// Adding a render option must not discard an existing saved control
    /// layout. Missing keys inherit their defaults; invalid values still fail
    /// decoding so the host can report corrupt settings to the user.
    init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        filter = try values.decodeIfPresent(CRTFilter.self, forKey: .filter) ?? filter
        filterStrength = try values.decodeIfPresent(Double.self, forKey: .filterStrength) ?? filterStrength
        aspect = try values.decodeIfPresent(ScreenAspect.self, forKey: .aspect) ?? aspect
        nativeWidescreen = try values.decodeIfPresent(Bool.self, forKey: .nativeWidescreen) ?? nativeWidescreen
        arcade = try values.decodeIfPresent(ArcadeSettings.self, forKey: .arcade) ?? arcade
        screenScale = try values.decodeIfPresent(Double.self, forKey: .screenScale) ?? screenScale
        screenWidth = try values.decodeIfPresent(Double.self, forKey: .screenWidth) ?? screenWidth
        screenHeight = try values.decodeIfPresent(Double.self, forKey: .screenHeight) ?? screenHeight
        screenOffsetX = try values.decodeIfPresent(Double.self, forKey: .screenOffsetX) ?? screenOffsetX
        screenOffsetY = try values.decodeIfPresent(Double.self, forKey: .screenOffsetY) ?? screenOffsetY
        touchEnabled = try values.decodeIfPresent(Bool.self, forKey: .touchEnabled) ?? touchEnabled
        touchOpacity = try values.decodeIfPresent(Double.self, forKey: .touchOpacity) ?? touchOpacity
        stickSensitivity = try values.decodeIfPresent(Double.self, forKey: .stickSensitivity) ?? stickSensitivity
        stickDeadZone = try values.decodeIfPresent(Double.self, forKey: .stickDeadZone) ?? stickDeadZone
        directionStyle = try values.decodeIfPresent(DirectionStyle.self, forKey: .directionStyle) ?? directionStyle
        transparentDirections = try values.decodeIfPresent(Bool.self, forKey: .transparentDirections) ?? transparentDirections
        controls = try values.decodeIfPresent([TouchControl].self, forKey: .controls) ?? controls
    }

    mutating func sanitize() {
        arcade.sanitize()
        func clamped(_ v: Double, _ min: Double, _ max: Double, fallback: Double) -> Double {
            v.isFinite ? Swift.min(max, Swift.max(min, v)) : fallback
        }
        filterStrength = clamped(filterStrength, 0, 1, fallback: 0.75)
        screenScale = clamped(screenScale, 0.25, 1, fallback: 1)
        screenWidth = clamped(screenWidth, 0.25, 2, fallback: 1)
        screenHeight = clamped(screenHeight, 0.25, 2, fallback: 1)
        screenOffsetX = clamped(screenOffsetX, -1, 1, fallback: 0)
        screenOffsetY = clamped(screenOffsetY, -1, 1, fallback: 0)
        touchOpacity = clamped(touchOpacity, 0.1, 1, fallback: 0.65)
        stickSensitivity = clamped(stickSensitivity, 0.01, 1, fallback: 0.5)
        stickDeadZone = clamped(stickDeadZone, 0, 0.35, fallback: 0)
        if controls.isEmpty { controls = TouchControl.defaults }
        controls = Array(controls.prefix(24))
        var used = Set<UUID>()
        for i in controls.indices {
            while used.contains(controls[i].id) { controls[i].id = UUID() }
            used.insert(controls[i].id)
            controls[i].x = clamped(controls[i].x, 0, 1, fallback: 0.5)
            controls[i].y = clamped(controls[i].y, 0, 1, fallback: 0.5)
            controls[i].size = clamped(controls[i].size, 0.55, 2, fallback: 1)
            controls[i].buttons &= NativeButton.validMask
            let label = controls[i].label.trimmingCharacters(in: .whitespacesAndNewlines)
            controls[i].label = label.isEmpty ? (controls[i].isDirection ? "Direction" : "CUSTOM") : String(label.prefix(12))
        }
    }
}
