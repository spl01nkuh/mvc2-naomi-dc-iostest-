// macOS GPU validation of the same shader source used by the iOS app.
// Usage: swift tools/test-metal-presentation.swift ports/port/ios/display/Presentation.metal
import Foundation
import Metal

struct Uniforms {
    var controls: SIMD4<Float>
    var source: SIMD4<Float>
    var output: SIMD4<Float>
    var rect: SIMD4<Float>
}

enum TestFailure: Error, CustomStringConvertible {
    case failed(String)
    var description: String { switch self { case .failed(let text): return text } }
}

func require(_ condition: Bool, _ message: String) throws {
    if !condition { throw TestFailure.failed(message) }
}

func checksum(_ data: [UInt8]) -> UInt64 {
    data.reduce(UInt64(14695981039346656037)) { ($0 ^ UInt64($1)) &* 1099511628211 }
}

func run() throws {
    guard let device = MTLCreateSystemDefaultDevice() else {
        print("SKIP: no Metal GPU is available on this runner")
        return
    }
    guard let queue = device.makeCommandQueue() else { throw TestFailure.failed("No Metal command queue") }
    let path = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "ports/port/ios/display/Presentation.metal"
    let source = try String(contentsOfFile: path, encoding: .utf8)
    let library = try device.makeLibrary(source: source, options: nil)
    let pipelineDescription = MTLRenderPipelineDescriptor()
    pipelineDescription.vertexFunction = library.makeFunction(name: "mvc2_present_vertex")
    pipelineDescription.fragmentFunction = library.makeFunction(name: "mvc2_present_fragment")
    pipelineDescription.colorAttachments[0].pixelFormat = .bgra8Unorm
    let pipeline = try device.makeRenderPipelineState(descriptor: pipelineDescription)
    let inputWidth = 160, inputHeight = 120, outputWidth = 640, outputHeight = 480
    let inputDescription = MTLTextureDescriptor.texture2DDescriptor(
        pixelFormat: .rgba8Unorm, width: inputWidth, height: inputHeight, mipmapped: false)
    inputDescription.storageMode = .shared
    inputDescription.usage = .shaderRead
    guard let input = device.makeTexture(descriptor: inputDescription) else {
        throw TestFailure.failed("No source texture")
    }
    var sourcePixels = [UInt8](repeating: 255, count: inputWidth * inputHeight * 4)
    for y in 0..<inputHeight {
        for x in 0..<inputWidth {
            let offset = (y * inputWidth + x) * 4
            sourcePixels[offset] = UInt8(x * 255 / (inputWidth - 1))
            sourcePixels[offset + 1] = UInt8(y * 255 / (inputHeight - 1))
            sourcePixels[offset + 2] = ((x / 3 + y / 3) & 1) == 0 ? 230 : 30
        }
    }
    sourcePixels.withUnsafeBytes { bytes in
        input.replace(region: MTLRegionMake2D(0, 0, inputWidth, inputHeight),
                      mipmapLevel: 0, withBytes: bytes.baseAddress!, bytesPerRow: inputWidth * 4)
    }
    let outputDescription = MTLTextureDescriptor.texture2DDescriptor(
        pixelFormat: .bgra8Unorm, width: outputWidth, height: outputHeight, mipmapped: false)
    outputDescription.storageMode = .shared
    outputDescription.usage = .renderTarget
    guard let output = device.makeTexture(descriptor: outputDescription) else {
        throw TestFailure.failed("No output texture")
    }

    func render(mode: Int, strength: Float, rect: SIMD4<Float>? = nil,
                sourceTexture: MTLTexture? = nil) throws -> [UInt8] {
        let sourceTexture = sourceTexture ?? input
        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = output
        pass.colorAttachments[0].loadAction = .clear
        pass.colorAttachments[0].storeAction = .store
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1)
        guard let command = queue.makeCommandBuffer(),
              let encoder = command.makeRenderCommandEncoder(descriptor: pass) else {
            throw TestFailure.failed("Cannot encode display pass")
        }
        var uniforms = Uniforms(
            controls: SIMD4(strength, Float(mode), 0, 0),
            source: SIMD4(Float(sourceTexture.width), Float(sourceTexture.height), 0, 0),
            output: SIMD4(Float(outputWidth), Float(outputHeight), 0, 0),
            rect: rect ?? SIMD4(0, 0, Float(outputWidth), Float(outputHeight)))
        try require(MemoryLayout<Uniforms>.stride == 64, "Swift/Metal uniform ABI mismatch")
        encoder.setRenderPipelineState(pipeline)
        encoder.setFragmentTexture(sourceTexture, index: 0)
        encoder.setFragmentBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        encoder.endEncoding()
        command.commit()
        command.waitUntilCompleted()
        try require(command.status == .completed,
                    "Metal failed: \(command.error?.localizedDescription ?? "unknown error")")
        var pixels = [UInt8](repeating: 0, count: outputWidth * outputHeight * 4)
        pixels.withUnsafeMutableBytes { bytes in
            output.getBytes(bytes.baseAddress!, bytesPerRow: outputWidth * 4,
                            from: MTLRegionMake2D(0, 0, outputWidth, outputHeight), mipmapLevel: 0)
        }
        return pixels
    }

    let original = try render(mode: 0, strength: 1)
    var seen = Set<UInt64>([checksum(original)])
    for mode in 1...9 {
        try require(try render(mode: mode, strength: 0) == original,
                    "Preset \(mode) changes the image at zero strength")
        let filtered = try render(mode: mode, strength: 1)
        try require(filtered != original, "Preset \(mode) has no visible effect")
        try require(seen.insert(checksum(filtered)).inserted,
                    "Preset \(mode) duplicates another preset")
        let partial = try render(mode: mode, strength: 0.5)
        try require(partial != filtered && partial != original,
                    "Preset \(mode) ignores partial strength")
        print("PASS: preset \(mode), zero/half/full strength, distinct GPU output")
    }
    let letterboxed = try render(mode: 0, strength: 1, rect: SIMD4(80, 60, 480, 360))
    for y in 0..<outputHeight {
        for x in 0..<outputWidth where x < 80 || x >= 560 || y < 60 || y >= 420 {
            let offset = (y * outputWidth + x) * 4
            try require(letterboxed[offset] == 0 && letterboxed[offset + 1] == 0 &&
                        letterboxed[offset + 2] == 0 && letterboxed[offset + 3] == 255,
                        "Display geometry writes outside the destination rectangle")
        }
    }
    let empty = try render(mode: 0, strength: 1, rect: SIMD4(0, 0, 0, 0))
    for offset in stride(from: 0, to: empty.count, by: 4) {
        try require(empty[offset] == 0 && empty[offset + 1] == 0 && empty[offset + 2] == 0,
                    "Empty viewport is not black")
    }
    // A native wide frame has more pixels horizontally. Check the production
    // shader keeps its square marker square and presents both extra margins.
    let wideWidth = 854, wideHeight = 480
    let wideDescription = MTLTextureDescriptor.texture2DDescriptor(
        pixelFormat: .rgba8Unorm, width: wideWidth, height: wideHeight, mipmapped: false)
    wideDescription.storageMode = .shared
    wideDescription.usage = .shaderRead
    guard let wideTexture = device.makeTexture(descriptor: wideDescription) else {
        throw TestFailure.failed("No widescreen source texture")
    }
    var widePixels = [UInt8](repeating: 0, count: wideWidth * wideHeight * 4)
    for y in 0..<wideHeight {
        for x in 0..<wideWidth {
            let offset = (y * wideWidth + x) * 4
            widePixels[offset + 3] = 255
            if x < 107 { widePixels[offset] = 255 }
            if x >= 747 { widePixels[offset + 1] = 255 }
            if abs(x - 427) < 32 && abs(y - 240) < 32 {
                widePixels[offset] = 255
                widePixels[offset + 1] = 255
                widePixels[offset + 2] = 255
            }
        }
    }
    widePixels.withUnsafeBytes { bytes in
        wideTexture.replace(region: MTLRegionMake2D(0, 0, wideWidth, wideHeight),
                            mipmapLevel: 0, withBytes: bytes.baseAddress!, bytesPerRow: wideWidth * 4)
    }
    let fittedHeight = Float(outputWidth) * Float(wideHeight) / Float(wideWidth)
    let wideRect = SIMD4<Float>(0, (Float(outputHeight) - fittedHeight) / 2,
                                Float(outputWidth), fittedHeight)
    let wideOutput = try render(mode: 0, strength: 1, rect: wideRect, sourceTexture: wideTexture)
    func channel(_ x: Int, _ y: Int, _ component: Int) -> UInt8 {
        wideOutput[(y * outputWidth + x) * 4 + component]
    }
    try require(channel(40, 240, 2) == 255 && channel(600, 240, 1) == 255,
                "Widescreen presentation lost an expanded side of the native frame")
    try require(channel(320, 0, 0) == 0 && channel(320, 479, 2) == 0,
                "Widescreen presentation escaped its proportional viewport")
    let whiteColumns = (0..<outputWidth).filter {
        channel($0, 240, 0) > 240 && channel($0, 240, 1) > 240 && channel($0, 240, 2) > 240
    }.count
    let whiteRows = (0..<outputHeight).filter {
        channel(320, $0, 0) > 240 && channel(320, $0, 1) > 240 && channel(320, $0, 2) > 240
    }.count
    try require(whiteColumns > 40 && abs(whiteColumns - whiteRows) <= 1,
                "A square in the native widescreen frame was stretched during presentation")
    for mode in 1...9 {
        try require(try render(mode: mode, strength: 0, rect: wideRect,
                               sourceTexture: wideTexture) == wideOutput,
                    "Preset \(mode) changes a widescreen image at zero strength")
        _ = try render(mode: mode, strength: 1, rect: wideRect, sourceTexture: wideTexture)
    }
    print("PASS: 854x480 widescreen, proportional pixels, expanded margins, all CRT presets")
    print("PASS: presentation geometry, GPU: \(device.name)")
}

do { try run() }
catch {
    fputs("FAIL: \(error)\n", stderr)
    exit(1)
}
