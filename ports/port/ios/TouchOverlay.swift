import SwiftUI
import UIKit

struct TouchOverlay: UIViewRepresentable {
    @Binding var settings: AppSettings
    let input: InputState
    var editing: Bool
    var suspended = false
    var onSelect: (UUID) -> Void

    func makeUIView(context: Context) -> TouchSurface { TouchSurface(input: input) }
    func updateUIView(_ view: TouchSurface, context: Context) {
        if view.editing != editing || view.options.touchEnabled != settings.touchEnabled || view.suspended != suspended { view.cancelInput() }
        view.options = settings
        view.editing = editing
        view.suspended = suspended
        view.onControlsChanged = { settings.controls = $0 }
        view.onSelect = onSelect
        view.isUserInteractionEnabled = !suspended && (editing || settings.touchEnabled)
        view.setNeedsDisplay()
    }
}

final class TouchSurface: UIView {
    var options = AppSettings()
    var editing = false
    var suspended = false
    var onControlsChanged: (([TouchControl]) -> Void)?
    var onSelect: ((UUID) -> Void)?
    private let input: InputState
    private struct Contact {
        var control: UUID?
        var start: CGPoint
        var point: CGPoint
        var originalCenter: CGPoint
        var moved = false
    }
    private var contacts: [ObjectIdentifier: Contact] = [:]

    init(input: InputState) {
        self.input = input
        super.init(frame: .zero)
        backgroundColor = .clear
        isOpaque = false
        isMultipleTouchEnabled = true
        NotificationCenter.default.addObserver(self, selector: #selector(cancelInput), name: UIApplication.willResignActiveNotification, object: nil)
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    deinit { NotificationCenter.default.removeObserver(self) }

    private var area: CGRect { bounds.inset(by: safeAreaInsets).insetBy(dx: 8, dy: 8) }
    private var scale: CGFloat { min(1.25, max(0.55, min(area.width / 740, area.height / 320))) }
    private func frame(_ control: TouchControl) -> CGRect {
        let diameter = CGFloat(control.isDirection ? 156 : control.buttons == NativeButton.start.rawValue ? 62 : 54) * scale * CGFloat(control.size)
        let side = min(diameter, max(0, min(area.width, area.height)))
        let x = min(max(area.minX, area.minX + CGFloat(control.x) * area.width - side / 2), area.maxX - side)
        let y = min(max(area.minY, area.minY + CGFloat(control.y) * area.height - side / 2), area.maxY - side)
        return CGRect(x: x, y: y, width: side, height: side)
    }
    private func hit(_ point: CGPoint) -> TouchControl? {
        options.controls.reversed().first { frame($0).contains(point) }
    }
    private func source(_ key: ObjectIdentifier) -> String { "touch-\(key)" }

    @objc func cancelInput() {
        for key in contacts.keys { input.set(source: source(key), buttons: 0) }
        contacts.removeAll()
        setNeedsDisplay()
    }
    override func didMoveToWindow() {
        super.didMoveToWindow()
        if window == nil { cancelInput() }
    }
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            let point = touch.location(in: self)
            let control = hit(point)
            let rect = control.map(frame) ?? .zero
            contacts[ObjectIdentifier(touch)] = Contact(control: control?.id, start: point, point: point, originalCenter: CGPoint(x: rect.midX, y: rect.midY))
        }
        update(touches)
    }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) { update(touches) }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            let key = ObjectIdentifier(touch)
            if editing, let contact = contacts[key], !contact.moved, let id = contact.control { onSelect?(id) }
            input.set(source: source(key), buttons: 0)
            contacts.removeValue(forKey: key)
        }
        setNeedsDisplay()
    }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            let key = ObjectIdentifier(touch)
            input.set(source: source(key), buttons: 0)
            contacts.removeValue(forKey: key)
        }
        setNeedsDisplay()
    }
    private func update(_ touches: Set<UITouch>) {
        for touch in touches {
            let key = ObjectIdentifier(touch)
            guard var contact = contacts[key] else { continue }
            contact.point = touch.location(in: self)
            let dx = contact.point.x - contact.start.x, dy = contact.point.y - contact.start.y
            if hypot(dx, dy) > 6 { contact.moved = true }
            if editing {
                if let index = options.controls.firstIndex(where: { $0.id == contact.control }), area.width > 0, area.height > 0 {
                    var control = options.controls[index]
                    control.x = Double((contact.originalCenter.x + dx - area.minX) / area.width)
                    control.y = Double((contact.originalCenter.y + dy - area.minY) / area.height)
                    // Keep the full control inside the current safe area.
                    let rect = frame(control)
                    control.x = Double((rect.midX - area.minX) / area.width)
                    control.y = Double((rect.midY - area.minY) / area.height)
                    options.controls[index] = control
                    onControlsChanged?(options.controls)
                }
            } else if let initial = options.controls.first(where: { $0.id == contact.control }), initial.isDirection {
                input.set(source: source(key), buttons: directions(contact.point, frame(initial)))
            } else {
                let current = hit(contact.point)
                input.set(source: source(key), buttons: current?.isDirection == false ? current!.buttons : 0)
            }
            contacts[key] = contact
        }
        setNeedsDisplay()
    }

    private func directions(_ point: CGPoint, _ rect: CGRect) -> UInt16 {
        guard rect.width > 0 else { return 0 }
        let x = Double((point.x - rect.midX) / (rect.width / 2))
        let y = Double((point.y - rect.midY) / (rect.height / 2))
        if options.directionStyle != .dpad {
            // Share the actual native adapter's activation thresholds with
            // controllers and portable tests instead of duplicating the curve.
            return mvc2_input_stick_buttons(Float(x), Float(y), Float(options.stickSensitivity), Float(options.stickDeadZone))
        }
        var mask: UInt16 = 0
        let threshold = 1.0 / 3
        if x < -threshold { mask |= NativeButton.left.rawValue }; if x > threshold { mask |= NativeButton.right.rawValue }
        if y < -threshold { mask |= NativeButton.up.rawValue }; if y > threshold { mask |= NativeButton.down.rawValue }
        return mask
    }

    override func draw(_ rect: CGRect) {
        guard editing || options.touchEnabled, let ctx = UIGraphicsGetCurrentContext() else { return }
        ctx.setAlpha(editing ? 0.9 : CGFloat(options.touchOpacity))
        for control in options.controls {
            let box = frame(control)
            let active = contacts.values.contains { c in
                control.isDirection ? c.control == control.id : box.contains(c.point)
            }
            let path = control.isDirection && options.directionStyle == .dpad ? UIBezierPath(roundedRect: box, cornerRadius: 18) : UIBezierPath(ovalIn: box)
            (active && !editing ? UIColor.systemCyan.withAlphaComponent(0.6) : UIColor.black.withAlphaComponent(0.5)).setFill()
            path.fill()
            UIColor.white.setStroke(); path.lineWidth = editing ? 2.5 : 1.5; path.stroke()
            let label = control.isDirection ? (options.directionStyle == .stick ? "●" : "↑\n←    →\n↓") : control.label
            let style = NSMutableParagraphStyle(); style.alignment = .center
            let fontSize: CGFloat = control.isDirection ? 22 : 13
            let attrs: [NSAttributedString.Key: Any] = [.font: UIFont.boldSystemFont(ofSize: fontSize * scale), .foregroundColor: UIColor.white, .paragraphStyle: style]
            let textSize = (label as NSString).boundingRect(with: box.size, options: .usesLineFragmentOrigin, attributes: attrs, context: nil).size
            (label as NSString).draw(in: CGRect(x: box.minX, y: box.midY - textSize.height / 2, width: box.width, height: textSize.height), withAttributes: attrs)
            if control.isDirection && options.directionStyle != .dpad && !editing,
               let contact = contacts.values.first(where: { $0.control == control.id }) {
                let dx = contact.point.x - box.midX, dy = contact.point.y - box.midY
                let distance = max(1, hypot(dx, dy)), limit = box.width * 0.35
                let thumb = CGPoint(x: box.midX + dx * min(1, limit / distance), y: box.midY + dy * min(1, limit / distance))
                UIColor.systemCyan.setFill()
                UIBezierPath(ovalIn: CGRect(x: thumb.x - 15 * scale, y: thumb.y - 15 * scale, width: 30 * scale, height: 30 * scale)).fill()
            }
            if editing {
                let label = "\(Int((control.size * 100).rounded()))%"
                (label as NSString).draw(at: CGPoint(x: box.minX, y: box.minY - 16), withAttributes: [.font: UIFont.systemFont(ofSize: 11), .foregroundColor: UIColor.systemYellow])
            }
        }
    }
}
