import Foundation
import MetalKit
import SwiftUI
import UIKit

struct NativeCanvasView: UIViewRepresentable {
    var settings: AppSettings
    var frames: NativeFrameStore

    func makeUIView(context: Context) -> NativeCanvasSurface {
        NativeCanvasSurface(settings: settings, frames: frames)
    }

    func updateUIView(_ uiView: NativeCanvasSurface, context: Context) {
        uiView.update(settings: settings, frames: frames)
    }

    static func dismantleUIView(_ uiView: NativeCanvasSurface, coordinator: ()) {
        uiView.stop()
    }
}

/// Keeps renderer failures visible instead of silently displaying a black canvas.
final class NativeCanvasSurface: UIView {
    private let metalView = MTKView()
    private let errorLabel = UILabel()
    private var presenter: NativeMetalPresenter?
    private var frameObserver: NSObjectProtocol?
    private var frames: NativeFrameStore

    init(settings: AppSettings, frames: NativeFrameStore) {
        self.frames = frames
        super.init(frame: .zero)
        backgroundColor = .black
        isUserInteractionEnabled = false
        metalView.translatesAutoresizingMaskIntoConstraints = false
        metalView.backgroundColor = .black
        addSubview(metalView)
        NSLayoutConstraint.activate([
            metalView.leadingAnchor.constraint(equalTo: leadingAnchor),
            metalView.trailingAnchor.constraint(equalTo: trailingAnchor),
            metalView.topAnchor.constraint(equalTo: topAnchor),
            metalView.bottomAnchor.constraint(equalTo: bottomAnchor)
        ])
        errorLabel.translatesAutoresizingMaskIntoConstraints = false
        errorLabel.numberOfLines = 0
        errorLabel.textAlignment = .center
        errorLabel.textColor = .white
        errorLabel.font = .preferredFont(forTextStyle: .body)
        errorLabel.isHidden = true
        addSubview(errorLabel)
        NSLayoutConstraint.activate([
            errorLabel.centerXAnchor.constraint(equalTo: centerXAnchor),
            errorLabel.centerYAnchor.constraint(equalTo: centerYAnchor),
            errorLabel.widthAnchor.constraint(lessThanOrEqualTo: widthAnchor, multiplier: 0.85)
        ])
        do {
            presenter = try NativeMetalPresenter(view: metalView, settings: settings, frames: frames)
            presenter?.onError = { [weak self] message in self?.showError(message) }
            observeFrames()
        } catch {
            showError(error.localizedDescription)
        }
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is unavailable") }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        if window != nil { metalView.setNeedsDisplay() }
    }

    func update(settings: AppSettings, frames: NativeFrameStore) {
        presenter?.settings = settings
        presenter?.frames = frames
        if self.frames !== frames {
            self.frames = frames
            observeFrames()
        }
        metalView.setNeedsDisplay()
    }

    private func observeFrames() {
        if let frameObserver { NotificationCenter.default.removeObserver(frameObserver) }
        frameObserver = NotificationCenter.default.addObserver(
            forName: .mvc2NativeFrame, object: frames, queue: .main
        ) { [weak self] _ in self?.metalView.setNeedsDisplay() }
    }

    private func showError(_ message: String) {
        errorLabel.text = "Display unavailable\n\(message)"
        errorLabel.isHidden = false
        errorLabel.accessibilityLabel = errorLabel.text
    }

    func stop() {
        if let frameObserver { NotificationCenter.default.removeObserver(frameObserver) }
        frameObserver = nil
        metalView.delegate = nil
        presenter = nil
    }

    deinit {
        if let frameObserver { NotificationCenter.default.removeObserver(frameObserver) }
    }
}

private struct PresentationUniforms {
    var controls: SIMD4<Float> // strength, mode, reserved, reserved
    var source: SIMD4<Float>   // source dimensions
    var output: SIMD4<Float>   // drawable dimensions
    var rect: SIMD4<Float>     // unclipped destination rectangle, in pixels
}

private enum PresentationError: LocalizedError {
    case unavailable(String)
    var errorDescription: String? {
        switch self { case .unavailable(let message): return message }
    }
}

private final class NativeMetalPresenter: NSObject, MTKViewDelegate {
    var settings: AppSettings
    var frames: NativeFrameStore {
        didSet {
            if oldValue !== frames {
                texture = nil
                uploadedGeneration = nil
            }
        }
    }
    var onError: ((String) -> Void)?
    private let device: MTLDevice
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private var texture: MTLTexture?
    private var uploadedGeneration: UInt64?
    private var frameInFlight = false
    private var redrawPending = false

    init(view: MTKView, settings: AppSettings, frames: NativeFrameStore) throws {
        guard let device = MTLCreateSystemDefaultDevice(),
              let queue = device.makeCommandQueue() else {
            throw PresentationError.unavailable("This device cannot create a Metal renderer.")
        }
        let library: MTLLibrary
        do { library = try device.makeDefaultLibrary(bundle: .main) }
        catch { throw PresentationError.unavailable("The bundled CRT shaders could not be loaded: \(error.localizedDescription)") }
        guard let vertex = library.makeFunction(name: "mvc2_present_vertex"),
              let fragment = library.makeFunction(name: "mvc2_present_fragment") else {
            throw PresentationError.unavailable("The bundled CRT shader functions are missing.")
        }
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.label = "MVC2 native display presentation"
        descriptor.vertexFunction = vertex
        descriptor.fragmentFunction = fragment
        descriptor.colorAttachments[0].pixelFormat = .bgra8Unorm
        self.pipeline = try device.makeRenderPipelineState(descriptor: descriptor)
        self.device = device
        self.queue = queue
        self.settings = settings
        self.frames = frames
        super.init()
        view.device = device
        view.colorPixelFormat = .bgra8Unorm
        view.framebufferOnly = true
        view.clearColor = MTLClearColorMake(0, 0, 0, 1)
        view.enableSetNeedsDisplay = true
        view.isPaused = true
        view.delegate = self
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        view.setNeedsDisplay()
    }

    func draw(in view: MTKView) {
        if frameInFlight {
            redrawPending = true
            return
        }
        guard view.window != nil, view.drawableSize.width > 0, view.drawableSize.height > 0,
              let pass = view.currentRenderPassDescriptor,
              let drawable = view.currentDrawable,
              let command = queue.makeCommandBuffer() else { return }
        let (frame, generation) = frames.snapshot()
        if let frame, uploadedGeneration != generation {
            guard upload(frame: frame) else {
                onError?("There is not enough GPU memory for the decoded image.")
                return
            }
            uploadedGeneration = generation
        }
        if case nil = frame {
            texture = nil
            uploadedGeneration = nil
        }
        guard let encoder = command.makeRenderCommandEncoder(descriptor: pass) else { return }
        if let frame, let texture {
            let drawableSize = CGSize(width: CGFloat(drawable.texture.width),
                                      height: CGFloat(drawable.texture.height))
            var uniforms = makeUniforms(sourceWidth: frame.width, sourceHeight: frame.height,
                                        drawable: drawableSize)
            encoder.setRenderPipelineState(pipeline)
            encoder.setFragmentTexture(texture, index: 0)
            encoder.setFragmentBytes(&uniforms, length: MemoryLayout<PresentationUniforms>.stride, index: 0)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        }
        // An empty session only clears to black. It never substitutes a demo
        // image for a frame from the native game renderer.
        encoder.endEncoding()
        frameInFlight = true
        command.addCompletedHandler { [weak self, weak view] completed in
            let error = completed.status == .error
                ? completed.error?.localizedDescription ?? "The GPU could not present the image."
                : nil
            DispatchQueue.main.async {
                guard let self else { return }
                self.frameInFlight = false
                if let error { self.onError?(error) }
                if self.redrawPending {
                    self.redrawPending = false
                    view?.setNeedsDisplay()
                }
            }
        }
        command.present(drawable)
        command.commit()
    }

    private func upload(frame: NativeCanvasFrame) -> Bool {
        // draw(in:) only uploads after the previous command completes, so a
        // matching texture can be reused without overwriting GPU input.
        if texture?.width != frame.width || texture?.height != frame.height {
            let descriptor = MTLTextureDescriptor.texture2DDescriptor(
                pixelFormat: .rgba8Unorm, width: frame.width, height: frame.height, mipmapped: false
            )
            descriptor.usage = .shaderRead
            descriptor.storageMode = .shared
            guard let next = device.makeTexture(descriptor: descriptor) else { return false }
            texture = next
        }
        guard let texture else { return false }
        return frame.rgba.withUnsafeBytes { bytes in
            guard let pixels = bytes.baseAddress else { return false }
            texture.replace(region: MTLRegionMake2D(0, 0, frame.width, frame.height),
                            mipmapLevel: 0, withBytes: pixels, bytesPerRow: frame.width * 4)
            return true
        }
    }

    private func makeUniforms(sourceWidth: Int, sourceHeight: Int, drawable: CGSize) -> PresentationUniforms {
        let aspect: MVC2Aspect
        switch settings.presentationAspect(sourceWidth: sourceWidth, sourceHeight: sourceHeight) {
        case .fourThree: aspect = MVC2_ASPECT_FOUR_THREE
        case .native: aspect = MVC2_ASPECT_SOURCE
        case .fill: aspect = MVC2_ASPECT_FILL
        }
        let rect = mvc2_viewport(Double(drawable.width), Double(drawable.height),
                                 Double(sourceWidth), Double(sourceHeight), aspect,
                                 settings.screenScale, settings.screenWidth, settings.screenHeight,
                                 settings.screenOffsetX, settings.screenOffsetY)
        let mode: Float
        switch settings.filter {
        case .off: mode = 0
        case .sharp: mode = 1
        case .zfast: mode = 2
        case .lottes: mode = 3
        case .caligari: mode = 4
        case .easyMode: mode = 5
        case .composite: mode = 6
        case .aperture: mode = 7
        case .geom: mode = 8
        case .hyllian: mode = 9
        }
        return PresentationUniforms(
            controls: SIMD4(Float(settings.filterStrength), mode, 0, 0),
            source: SIMD4(Float(sourceWidth), Float(sourceHeight), 0, 0),
            output: SIMD4(Float(drawable.width), Float(drawable.height), 0, 0),
            rect: SIMD4(Float(rect.x), Float(rect.y), Float(rect.width), Float(rect.height))
        )
    }
}
