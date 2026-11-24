import UIKit

/// Resolution-independent control faces. Geometry is expressed on a 200-point
/// canvas; input hit areas and activation thresholds remain in TouchSurface.
enum DirectionArtwork {
    private static let cyan = UIColor(red: 0.22, green: 0.88, blue: 1, alpha: 1)
    private static let dark = UIColor(red: 0.035, green: 0.045, blue: 0.065, alpha: 1)
    private static let steel = UIColor(red: 0.31, green: 0.37, blue: 0.44, alpha: 1)
    private static let directions: [UInt16] = [
        NativeButton.up.rawValue, NativeButton.right.rawValue,
        NativeButton.down.rawValue, NativeButton.left.rawValue
    ]

    static func draw(in context: CGContext, bounds: CGRect, style: DirectionStyle,
                     transparent: Bool, pressed: UInt16, contact: CGPoint?, editing: Bool) {
        guard bounds.width > 0, bounds.height > 0 else { return }
        context.saveGState()
        context.translateBy(x: bounds.minX, y: bounds.minY)
        context.scaleBy(x: bounds.width / 200, y: bounds.height / 200)
        if style == .dpad {
            drawPad(context, pressed: editing ? 0 : pressed, transparent: transparent)
        } else {
            let point = contact.map {
                CGPoint(x: ($0.x - bounds.minX) * 200 / bounds.width,
                        y: ($0.y - bounds.minY) * 200 / bounds.height)
            }
            drawStick(context, pressed: editing ? 0 : pressed,
                      contact: editing ? nil : point, hybrid: style == .hybrid,
                      transparent: transparent)
        }
        if editing {
            let guide = UIBezierPath(roundedRect: CGRect(x: 2, y: 2, width: 196, height: 196),
                                     cornerRadius: 14)
            context.saveGState()
            context.setLineDash(phase: 0, lengths: [4, 5])
            stroke(guide, UIColor.systemYellow.withAlphaComponent(0.7), 1.4, context)
            context.restoreGState()
        }
        context.restoreGState()
    }

    private static func drawPad(_ context: CGContext, pressed: UInt16, transparent: Bool) {
        let rim = cross(outer: 10, shoulder: 68, radius: 8)
        if transparent {
            outline(rim, UIColor.white.withAlphaComponent(0.7), 2.2, context)
        } else {
            shadow(rim, offset: CGSize(width: 0, height: 5), blur: 7, context)
            linear(rim, colors: [steel, dark], context)
            stroke(rim, UIColor.white.withAlphaComponent(0.25), 1.2, context)

            let face = cross(outer: 16, shoulder: 74, radius: 5)
            linear(face, colors: [UIColor(white: 0.20, alpha: 1),
                                  UIColor(white: 0.065, alpha: 1)], context)
            stroke(face, UIColor.black.withAlphaComponent(0.85), 1.6, context)
        }

        for index in 0..<4 {
            context.saveGState()
            context.translateBy(x: 100, y: 100)
            context.rotate(by: CGFloat(index) * .pi / 2)
            context.translateBy(x: -100, y: -100)
            let active = pressed & directions[index] != 0
            if active && !transparent {
                let arm = UIBezierPath(roundedRect: CGRect(x: 77, y: 19, width: 46, height: 66),
                                       cornerRadius: 5)
                linear(arm, colors: [cyan.withAlphaComponent(0.49),
                                     cyan.withAlphaComponent(0.025)], context)
                let edge = UIBezierPath()
                edge.move(to: CGPoint(x: 84, y: 23))
                edge.addLine(to: CGPoint(x: 116, y: 23))
                stroke(edge, cyan.withAlphaComponent(0.9), 2, context)
            }
            chevron(context, centerY: 48, active: active, subtle: false, transparent: transparent)

            // Separate each arm from the pivot with a recessed shoulder seam.
            if !transparent {
                let seam = UIBezierPath()
                seam.move(to: CGPoint(x: 79, y: 77))
                seam.addLine(to: CGPoint(x: 88, y: 86))
                seam.move(to: CGPoint(x: 121, y: 77))
                seam.addLine(to: CGPoint(x: 112, y: 86))
                stroke(seam, UIColor.black.withAlphaComponent(0.65), 1.5, context)
            }
            context.restoreGState()
        }

        let pivot = circle(center: CGPoint(x: 100, y: 100), radius: 17)
        if transparent {
            outline(pivot, UIColor.white.withAlphaComponent(0.48), 1.8, context)
            return
        }
        radial(pivot, center: CGPoint(x: 94, y: 92), radius: 31,
               colors: [UIColor(white: 0.25, alpha: 1), dark], context)
        stroke(pivot, UIColor.black.withAlphaComponent(0.8), 1.6, context)
        stroke(circle(center: CGPoint(x: 100, y: 100), radius: 12.5),
               UIColor.white.withAlphaComponent(0.09), 1, context)
        let dot = circle(center: CGPoint(x: 100, y: 100), radius: 3)
        fill(dot, UIColor.black.withAlphaComponent(0.45), context)
    }

    private static func drawStick(_ context: CGContext, pressed: UInt16,
                                  contact: CGPoint?, hybrid: Bool, transparent: Bool) {
        let center = CGPoint(x: 100, y: 100)
        let rim = circle(center: center, radius: 91)
        if transparent {
            outline(rim, UIColor.white.withAlphaComponent(0.7), 2.2, context)
        } else {
            shadow(rim, offset: CGSize(width: 0, height: 4), blur: 7, context)
            linear(rim, colors: [steel, dark], context)
            stroke(rim, UIColor.white.withAlphaComponent(0.28), 1.2, context)
            let base = circle(center: center, radius: 85)
            linear(base, colors: [UIColor(red: 0.12, green: 0.15, blue: 0.19, alpha: 1), dark], context)
            stroke(base, UIColor.black.withAlphaComponent(0.8), 1.7, context)
            let well = circle(center: center, radius: 67)
            radial(well, center: CGPoint(x: 94, y: 90), radius: 81,
                   colors: [UIColor(red: 0.09, green: 0.12, blue: 0.16, alpha: 1),
                            UIColor(white: 0.025, alpha: 1)], context)
            stroke(well, UIColor.white.withAlphaComponent(0.10), 1, context)
            stroke(circle(center: center, radius: 64.5), UIColor.black.withAlphaComponent(0.7), 1, context)
        }

        let gates: [UInt16] = [directions[0], directions[0] | directions[1],
                              directions[1], directions[1] | directions[2],
                              directions[2], directions[2] | directions[3],
                              directions[3], directions[3] | directions[0]]
        for index in 0..<8 {
            let angle = CGFloat(index) * .pi / 4 - .pi / 2
            let active = pressed & gates[index] == gates[index]
            let inside: CGFloat = index.isMultiple(of: 2) ? 74 : 76
            let tick = UIBezierPath()
            tick.move(to: CGPoint(x: 100 + cos(angle) * inside, y: 100 + sin(angle) * inside))
            tick.addLine(to: CGPoint(x: 100 + cos(angle) * 81, y: 100 + sin(angle) * 81))
            let width: CGFloat = index.isMultiple(of: 2) ? 2.2 : 1.5
            if transparent {
                outline(tick, active ? cyan : UIColor.white.withAlphaComponent(0.65), width, context)
            } else {
                stroke(tick, active ? cyan : UIColor.white.withAlphaComponent(0.23), width, context)
            }
        }
        if hybrid {
            for index in 0..<4 {
                context.saveGState()
                context.translateBy(x: 100, y: 100)
                context.rotate(by: CGFloat(index) * .pi / 2)
                context.translateBy(x: -100, y: -100)
                chevron(context, centerY: 47, active: pressed & directions[index] != 0,
                        subtle: true, transparent: transparent)
                context.restoreGState()
            }
        }

        var thumb = center
        if let contact {
            let dx = contact.x - 100, dy = contact.y - 100
            let distance = max(1, hypot(dx, dy))
            let travel = min(1, 57 / distance)
            thumb = CGPoint(x: 100 + dx * travel, y: 100 + dy * travel)
        }
        let gripRim = circle(center: thumb, radius: 32)
        if transparent {
            outline(gripRim, contact == nil ? UIColor.white.withAlphaComponent(0.7) : cyan,
                    2.5, context)
            return
        }
        shadow(gripRim, offset: CGSize(width: 0, height: 5), blur: 7, context)
        linear(gripRim, colors: [UIColor(white: 0.47, alpha: 1), dark], context)
        stroke(gripRim, contact == nil ? UIColor.white.withAlphaComponent(0.25) : cyan,
               contact == nil ? 1.1 : 1.8, context)
        let grip = circle(center: thumb, radius: 28.5)
        radial(grip, center: CGPoint(x: thumb.x - 9, y: thumb.y - 12), radius: 47,
               colors: [UIColor(red: 0.28, green: 0.34, blue: 0.41, alpha: 1),
                        UIColor(red: 0.075, green: 0.095, blue: 0.13, alpha: 1)], context)
        stroke(grip, UIColor.black.withAlphaComponent(0.65), 1, context)
        for radius in [CGFloat(21), 17, 13] {
            stroke(circle(center: thumb, radius: radius),
                   UIColor.black.withAlphaComponent(0.22), 1.6, context)
            stroke(circle(center: CGPoint(x: thumb.x, y: thumb.y + 0.7), radius: radius),
                   UIColor.white.withAlphaComponent(0.07), 0.8, context)
        }
        let dimple = circle(center: thumb, radius: 4)
        linear(dimple, colors: [dark, UIColor(white: 0.23, alpha: 1)], context)
    }

    private static func chevron(_ context: CGContext, centerY: CGFloat,
                                  active: Bool, subtle: Bool, transparent: Bool) {
        let path = UIBezierPath()
        path.move(to: CGPoint(x: 88, y: centerY + 3))
        path.addLine(to: CGPoint(x: 100, y: centerY - 9))
        path.addLine(to: CGPoint(x: 112, y: centerY + 3))
        path.addLine(to: CGPoint(x: 107, y: centerY + 8))
        path.addLine(to: CGPoint(x: 100, y: centerY + 1))
        path.addLine(to: CGPoint(x: 93, y: centerY + 8))
        path.close()
        if transparent {
            outline(path, active ? cyan : UIColor.white.withAlphaComponent(subtle ? 0.5 : 0.82),
                    1.7, context)
            return
        }
        context.saveGState()
        context.translateBy(x: 0, y: 1.2)
        stroke(path, UIColor.white.withAlphaComponent(subtle ? 0.08 : 0.17), 1.3, context)
        context.restoreGState()
        fill(path, active ? cyan.withAlphaComponent(subtle ? 0.65 : 1)
             : UIColor(white: subtle ? 0.25 : 0.42, alpha: 1), context)
        stroke(path, active ? cyan.withAlphaComponent(0.8)
               : UIColor.black.withAlphaComponent(0.6), 0.8, context)
    }

    private static func cross(outer: CGFloat, shoulder: CGFloat, radius: CGFloat) -> UIBezierPath {
        let far = 200 - outer, side = 200 - shoulder
        let points = [CGPoint(x: shoulder, y: outer), CGPoint(x: side, y: outer),
                      CGPoint(x: side, y: shoulder), CGPoint(x: far, y: shoulder),
                      CGPoint(x: far, y: side), CGPoint(x: side, y: side),
                      CGPoint(x: side, y: far), CGPoint(x: shoulder, y: far),
                      CGPoint(x: shoulder, y: side), CGPoint(x: outer, y: side),
                      CGPoint(x: outer, y: shoulder), CGPoint(x: shoulder, y: shoulder)]
        let path = UIBezierPath()
        for index in points.indices {
            let point = points[index]
            let previous = points[(index + points.count - 1) % points.count]
            let next = points[(index + 1) % points.count]
            let before = CGPoint(x: point.x + (previous.x - point.x) /
                hypot(previous.x - point.x, previous.y - point.y) * radius,
                                 y: point.y + (previous.y - point.y) /
                hypot(previous.x - point.x, previous.y - point.y) * radius)
            let after = CGPoint(x: point.x + (next.x - point.x) /
                hypot(next.x - point.x, next.y - point.y) * radius,
                                y: point.y + (next.y - point.y) /
                hypot(next.x - point.x, next.y - point.y) * radius)
            if index == 0 { path.move(to: before) } else { path.addLine(to: before) }
            path.addQuadCurve(to: after, controlPoint: point)
        }
        path.close()
        return path
    }

    private static func circle(center: CGPoint, radius: CGFloat) -> UIBezierPath {
        UIBezierPath(ovalIn: CGRect(x: center.x - radius, y: center.y - radius,
                                   width: radius * 2, height: radius * 2))
    }

    private static func fill(_ path: UIBezierPath, _ color: UIColor, _ context: CGContext) {
        context.setFillColor(color.cgColor)
        context.addPath(path.cgPath)
        context.fillPath()
    }

    private static func stroke(_ path: UIBezierPath, _ color: UIColor,
                               _ width: CGFloat, _ context: CGContext) {
        context.setStrokeColor(color.cgColor)
        context.setLineWidth(width)
        context.setLineJoin(.round)
        context.setLineCap(.round)
        context.addPath(path.cgPath)
        context.strokePath()
    }

    // A thin dark keyline keeps light outlines legible on bright game art.
    private static func outline(_ path: UIBezierPath, _ color: UIColor,
                                _ width: CGFloat, _ context: CGContext) {
        stroke(path, UIColor.black.withAlphaComponent(0.55), width + 2, context)
        stroke(path, color, width, context)
    }

    private static func shadow(_ path: UIBezierPath, offset: CGSize, blur: CGFloat,
                               _ context: CGContext) {
        context.saveGState()
        context.setShadow(offset: offset, blur: blur, color: UIColor.black.withAlphaComponent(0.7).cgColor)
        fill(path, dark, context)
        context.restoreGState()
    }

    private static func linear(_ path: UIBezierPath, colors: [UIColor], _ context: CGContext) {
        guard let gradient = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(),
            colors: colors.map(\.cgColor) as CFArray, locations: nil) else { return }
        context.saveGState()
        context.addPath(path.cgPath)
        context.clip()
        context.drawLinearGradient(gradient, start: CGPoint(x: path.bounds.midX, y: path.bounds.minY),
                                   end: CGPoint(x: path.bounds.midX, y: path.bounds.maxY), options: [])
        context.restoreGState()
    }

    private static func radial(_ path: UIBezierPath, center: CGPoint, radius: CGFloat,
                                colors: [UIColor], _ context: CGContext) {
        guard let gradient = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(),
            colors: colors.map(\.cgColor) as CFArray, locations: nil) else { return }
        context.saveGState()
        context.addPath(path.cgPath)
        context.clip()
        context.drawRadialGradient(gradient, startCenter: center, startRadius: 0,
                                   endCenter: center, endRadius: radius,
                                   options: [.drawsAfterEndLocation])
        context.restoreGState()
    }
}
