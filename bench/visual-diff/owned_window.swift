import AppKit
import CoreVideo
import CoreMedia
import Darwin
import IOSurface
import ScreenCaptureKit

// This program admits exactly one window created here, owned by this process.
@MainActor final class Pattern: NSView {
    var changed = false
    override var isFlipped: Bool { true }
    override func draw(_ rect: NSRect) {
        NSColor.clear.setFill()
        bounds.fill(using: .copy)
        if changed {
            NSColor.red.setFill()
            NSRect(x: 136, y: 72, width: 16, height: 16).fill()
        }
    }
}
enum MeasureError: Error { case failed(String) }
typealias Compare = @convention(c) (UnsafeMutableRawPointer, UnsafeMutableRawPointer) -> Int64

@MainActor func capture(_ filter: SCContentFilter, _ cfg: SCStreamConfiguration) async throws -> CVPixelBuffer {
    let sample = try await SCScreenshotManager.captureSampleBuffer(contentFilter: filter, configuration: cfg)
    guard let buffer = CMSampleBufferGetImageBuffer(sample), CVPixelBufferGetIOSurface(buffer) != nil,
          CVPixelBufferGetPixelFormatType(buffer) == kCVPixelFormatType_32BGRA,
          CVPixelBufferGetWidth(buffer) == 512, CVPixelBufferGetHeight(buffer) == 256
    else { throw MeasureError.failed("Expected an IOSurface-backed 512x256 BGRA sample") }
    return buffer
}

@MainActor func run() async throws {
    guard CGPreflightScreenCaptureAccess() else { throw MeasureError.failed("Read-only capture preflight was denied") }
    let args = CommandLine.arguments
    guard args.count >= 3 else { throw MeasureError.failed("Supply output JSON and one or more named comparison plugins") }
    let window = NSWindow(contentRect: NSRect(x: 80, y: 80, width: 512, height: 256),
                          styleMask: [.borderless], backing: .buffered, defer: false)
    window.hasShadow = false
    window.backgroundColor = .clear
    window.isOpaque = false
    window.isReleasedWhenClosed = false
    let view = Pattern(frame: NSRect(x: 0, y: 0, width: 512, height: 256))
    window.contentView = view
    window.orderFrontRegardless()
    defer { window.close() }
    try await Task.sleep(for: .milliseconds(200))
    let targets = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: true)
    guard let owned = targets.windows.first(where: {
        $0.windowID == CGWindowID(window.windowNumber) && $0.owningApplication?.processID == getpid()
    }) else { throw MeasureError.failed("Owned disposable window is absent") }
    let filter = SCContentFilter(desktopIndependentWindow: owned)
    let cfg = SCStreamConfiguration()
    cfg.width = 512; cfg.height = 256; cfg.pixelFormat = kCVPixelFormatType_32BGRA
    cfg.colorSpaceName = CGColorSpace.sRGB; cfg.showsCursor = false; cfg.ignoreShadowsSingleWindow = true
    var frames: [CVPixelBuffer] = []
    var acquisition: [UInt64] = []
    for changed in [false, false, true] {
        view.changed = changed
        view.display()
        CATransaction.flush()
        try await Task.sleep(for: .milliseconds(150))
        let start = DispatchTime.now().uptimeNanoseconds
        frames.append(try await capture(filter, cfg))
        acquisition.append(DispatchTime.now().uptimeNanoseconds - start)
    }
    window.orderOut(nil)
    // Strong references preserve the completed, immutable compositor samples until all consumers finish.
    try withExtendedLifetime(frames) {
        let surfaces = frames.map { Unmanaged.passUnretained(CVPixelBufferGetIOSurface($0)!.takeUnretainedValue()).toOpaque() }
        var arms: [(String, Compare)] = []
        var handles: [UnsafeMutableRawPointer] = []
        var failures: [String: String] = [:]
        for path in args.dropFirst(2) {
            let name = URL(fileURLWithPath: path).deletingPathExtension().lastPathComponent
            guard let h = dlopen(path, RTLD_NOW | RTLD_LOCAL),
                  let setupSym = dlsym(h, "prepare_comparison"), let compareSym = dlsym(h, "compare_surfaces")
            else { failures[name] = String(cString: dlerror()); continue }
            handles.append(h)
            typealias Prepare = @convention(c) () -> Int32
            guard unsafeBitCast(setupSym, to: Prepare.self)() == 0 else {
                failures[name] = "GPU or surface comparison initialization failed"; continue
            }
            arms.append((name, unsafeBitCast(compareSym, to: Compare.self)))
        }
        var rows: [[String: Any]] = []
        for i in 0..<(16+128) {
            let changed = i % 2 == 1
            let target = surfaces[changed ? 2 : 1]
            for offset in 0..<arms.count {
                let (name, compare) = arms[(i + offset) % arms.count]
                if failures[name] != nil { continue }
                let start = DispatchTime.now().uptimeNanoseconds
                let mask = compare(surfaces[0], target)
                let ns = DispatchTime.now().uptimeNanoseconds - start
                guard mask == (changed ? (1 << 10) : 0) else {
                    failures[name] = "sample \(i) returned mask \(mask), expected \(changed ? 1024 : 0)"
                    continue
                }
                if i >= 16 { rows.append(["arm":name, "changed":changed, "mask":mask,"elapsedNs":ns]) }
            }
        }
        let payload: [String: Any] = ["producer":"WindowServer via ScreenCaptureKit", "windowOwned":true,
            "width":512, "height":256, "format":"BGRA8 sRGB", "captureNs":acquisition, "timing":rows,
            "failures":failures, "modelLoaded":false, "driverTransferCounts":NSNull()]
        try JSONSerialization.data(withJSONObject: payload, options:[.prettyPrinted,.sortedKeys])
            .write(to: URL(fileURLWithPath: args[1]))
        // Keep library-owned workers loaded until process exit.
        withExtendedLifetime(handles) {}
        if !failures.isEmpty { throw MeasureError.failed("Comparison failed; see the saved receipt") }
    }
}
let app = NSApplication.shared
app.setActivationPolicy(.accessory)
Task { @MainActor in
    do { try await run(); exit(0) }
    catch { fputs("\(error)\n", stderr); exit(1) }
}
app.run()
