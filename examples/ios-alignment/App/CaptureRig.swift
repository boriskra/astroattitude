@preconcurrency import AVFoundation
import Astroattitude
import CoreVideo
import ImageIO
import simd

struct CapturedFrame: @unchecked Sendable {
  let pixels: [Float]
  let luma: [UInt8]
  let width: Int, height: Int
  let orientation: Int
  let layout: SampleLayout
  let black: [Double], white: [Double]
  let midpoint: Double, duration: Double
  let intrinsics: simd_float3x3?
  let provenance: String
}
/// One serial camera owner; no virtual device, automatic zoom or Photos writes.
final class CaptureRig: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate,
  AVCapturePhotoCaptureDelegate, @unchecked Sendable
{
  let session = AVCaptureSession()
  private let queue = DispatchQueue(label: "alignment.camera")
  private let lock = NSLock()
  private var latest: CapturedFrame?
  private var photoWaiter: CheckedContinuation<CapturedFrame, Error>?
  private var photoID: Int64?
  private var rawDelivered: CapturedFrame?
  private let video = AVCaptureVideoDataOutput(), photo = AVCapturePhotoOutput()
  private(set) var device: AVCaptureDevice?
  private var hostMinusUptime = 0.0
  private var generation: UInt64 = 0
  private var allowed = false
  private struct Settings { let focus: Float, seconds: Double, iso: Float }
  private var settings = [String: Settings]()
  func beginSession() {
    lock.lock()
    generation &+= 1
    allowed = true
    lock.unlock()
    queue.async { self.settings = [:] }
  }
  private func active(_ expected: UInt64) -> Bool {
    lock.lock()
    defer { lock.unlock() }
    return allowed && generation == expected
  }
  private var lastVideoDecode = -Double.infinity
  static func cameras() -> [AVCaptureDevice] {
    let types: [AVCaptureDevice.DeviceType] = [
      .builtInWideAngleCamera, .builtInUltraWideCamera, .builtInTelephotoCamera,
    ]
    return AVCaptureDevice.DiscoverySession(deviceTypes: types, mediaType: .video, position: .back)
      .devices.filter { !$0.isVirtualDevice }.sorted {
        (types.firstIndex(of: $0.deviceType) ?? 9) < (types.firstIndex(of: $1.deviceType) ?? 9)
      }
  }
  private func currentGeneration() -> UInt64 {
    lock.lock()
    defer { lock.unlock() }
    return generation
  }
  func configure(_ selected: AVCaptureDevice, stars: Bool) async throws {
    let expected = currentGeneration()
    try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
      queue.async {
        do {
          guard self.active(expected) else { throw CancellationError() }
          guard self.photoID == nil else {
            throw DemoError.message(
              "Waiting for the previous photo to finalize. This camera will be skipped.")
          }
          self.session.stopRunning()
          self.session.beginConfiguration()
          defer { self.session.commitConfiguration() }
          for input in self.session.inputs { self.session.removeInput(input) }
          for output in self.session.outputs { self.session.removeOutput(output) }
          let input = try AVCaptureDeviceInput(device: selected)
          guard self.session.canAddInput(input) else {
            throw DemoError.message("This camera cannot be opened.")
          }
          self.session.addInput(input)
          self.session.sessionPreset = .hd1280x720
          self.video.videoSettings = [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA
          ]
          self.video.alwaysDiscardsLateVideoFrames = true
          self.video.setSampleBufferDelegate(self, queue: self.queue)
          guard self.session.canAddOutput(self.video), self.session.canAddOutput(self.photo) else {
            throw DemoError.message("Camera outputs unavailable.")
          }
          self.session.addOutput(self.video)
          self.session.addOutput(self.photo)
          for connection in self.session.connections {
            if connection.isVideoStabilizationSupported {
              connection.preferredVideoStabilizationMode = .off
            }
            if connection.isVideoMirroringSupported {
              connection.automaticallyAdjustsVideoMirroring = false
              connection.isVideoMirrored = false
            }
            // Portrait video buffers use device +x, -y, -z camera axes.
            if connection.isVideoOrientationSupported { connection.videoOrientation = .portrait }
            if connection.isCameraIntrinsicMatrixDeliverySupported {
              connection.isCameraIntrinsicMatrixDeliveryEnabled = true
            }
          }
          try selected.lockForConfiguration()
          defer { selected.unlockForConfiguration() }
          selected.videoZoomFactor = 1
          if selected.isFocusModeSupported(.continuousAutoFocus) {
            selected.focusMode = .continuousAutoFocus
          }
          if selected.isExposureModeSupported(.continuousAutoExposure) {
            selected.exposureMode = .continuousAutoExposure
          }
          self.device = selected
          self.lock.lock()
          self.latest = nil
          self.lock.unlock()
          self.hostMinusUptime =
            CMClockGetTime(CMClockGetHostTimeClock()).seconds - ProcessInfo.processInfo.systemUptime
          continuation.resume()
        } catch { continuation.resume(throwing: error) }
      }
    }
    await withCheckedContinuation { c in
      queue.async {
        if self.active(expected) { self.session.startRunning() }
        c.resume()
      }
    }
  }
  func freeze(stars: Bool) async throws {
    try await withCheckedThrowingContinuation { (c: CheckedContinuation<Void, Error>) in
      queue.async {
        do {
          guard let d = self.device else { throw DemoError.message("No selected camera.") }
          try d.lockForConfiguration()
          defer { d.unlockForConfiguration() }
          if let saved = self.settings[d.uniqueID] {
            if d.isFocusModeSupported(.locked) {
              d.setFocusModeLocked(lensPosition: saved.focus, completionHandler: nil)
            }
            if d.isExposureModeSupported(.custom) {
              d.setExposureModeCustom(
                duration: CMTime(seconds: saved.seconds, preferredTimescale: 1_000_000),
                iso: saved.iso, completionHandler: nil)
            }
            c.resume()
            return
          }
          if d.isFocusModeSupported(.locked) { d.focusMode = .locked }
          var lockedSeconds = d.exposureDuration.seconds
          var lockedISO = d.iso
          if stars && d.isExposureModeSupported(.custom) {
            // A short processed scout only sets exposure; it is never
            // presented as a RAW measurement or a focus calibration.
            self.lock.lock()
            let scout = self.latest
            self.lock.unlock()
            let values =
              scout?.pixels.enumerated().filter { $0.offset % 257 == 0 }.map { $0.element }.sorted()
              ?? []
            let background = values.isEmpty ? 0 : Double(values[values.count / 2])
            let estimate = background > 0.02 ? d.exposureDuration.seconds * 0.02 / background : 1.0
            let seconds = max(
              d.activeFormat.minExposureDuration.seconds,
              min(estimate, d.activeFormat.maxExposureDuration.seconds, 1.0))
            lockedSeconds = seconds
            lockedISO = max(d.activeFormat.minISO, min(d.activeFormat.maxISO, 1600))
            d.setExposureModeCustom(
              duration: CMTime(seconds: seconds, preferredTimescale: 1_000_000),
              iso: max(d.activeFormat.minISO, min(d.activeFormat.maxISO, 1600)),
              completionHandler: nil)
          } else if d.isExposureModeSupported(.locked) {
            d.exposureMode = .locked
          }
          if d.isWhiteBalanceModeSupported(.locked) { d.whiteBalanceMode = .locked }
          self.settings[d.uniqueID] = Settings(
            focus: d.lensPosition, seconds: lockedSeconds, iso: lockedISO)
          c.resume()
        } catch { c.resume(throwing: error) }
      }
    }
  }
  func stop() {
    lock.lock()
    allowed = false
    generation &+= 1
    lock.unlock()
    queue.async {
      self.session.stopRunning()
      self.resolve(.failure(CancellationError()))
      self.lock.lock()
      self.latest = nil
      self.lock.unlock()
    }
  }
  private func resolve(_ result: Result<CapturedFrame, Error>, finalized: Bool = false) {
    let waiter = photoWaiter
    photoWaiter = nil
    // A timeout ends the wait, not the AVFoundation capture. Keep its ID
    // until didFinishCapture prevents a lens switch across a live photo.
    if finalized {
      photoID = nil
      rawDelivered = nil
    }
    waiter?.resume(with: result)
  }
  func frame(stars: Bool) async throws -> CapturedFrame {
    try Task.checkCancellation()
    return try await withCheckedThrowingContinuation { c in
      queue.async {
        guard self.session.isRunning else {
          c.resume(throwing: DemoError.message("Camera stopped."))
          return
        }
        if stars,
          let format = self.photo.availableRawPhotoPixelFormatTypes.first(
            where: AVCapturePhotoOutput.isBayerRAWPixelFormat)
        {
          guard self.photoID == nil else {
            c.resume(throwing: DemoError.message("Capture already active."))
            return
          }
          let settings = AVCapturePhotoSettings(rawPixelFormatType: format)
          self.photoWaiter = c
          self.photoID = settings.uniqueID
          self.photo.capturePhoto(with: settings, delegate: self)
          self.queue.asyncAfter(deadline: .now() + 5) {
            if self.photoID == settings.uniqueID {
              self.resolve(.failure(DemoError.message("RAW capture timed out.")))
            }
          }
        } else {
          self.lock.lock()
          let frame = self.latest
          self.lock.unlock()
          if let frame, abs(ProcessInfo.processInfo.systemUptime - frame.midpoint) < 3 {
            c.resume(returning: frame)
          } else {
            c.resume(throwing: DemoError.message("Waiting for camera frames."))
          }
        }
      }
    }
  }
  private func uptime(_ timestamp: CMTime) -> Double {
    let host =
      session.masterClock.map {
        CMSyncConvertTime(timestamp, from: $0, to: CMClockGetHostTimeClock())
      } ?? timestamp
    return host.seconds - hostMinusUptime
  }
  func captureOutput(
    _ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer,
    from connection: AVCaptureConnection
  ) {
    let now = ProcessInfo.processInfo.systemUptime
    guard now - lastVideoDecode >= 0.2 else { return }
    lastVideoDecode = now
    guard let buffer = CMSampleBufferGetImageBuffer(sampleBuffer), let device else { return }
    CVPixelBufferLockBaseAddress(buffer, .readOnly)
    defer { CVPixelBufferUnlockBaseAddress(buffer, .readOnly) }
    let width = CVPixelBufferGetWidth(buffer)
    let height = CVPixelBufferGetHeight(buffer)
    let stride = CVPixelBufferGetBytesPerRow(buffer)
    guard width * height <= 1_300_000,
      let base = CVPixelBufferGetBaseAddress(buffer)?.assumingMemoryBound(to: UInt8.self)
    else { return }
    var pixels = [Float](repeating: 0, count: width * height)
    var luma = [UInt8](repeating: 0, count: width * height)
    for y in 0..<height {
      for x in 0..<width {
        let green = base[y * stride + 4 * x + 1]
        let v = Float(green) / 255
        pixels[y * width + x] = v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4)
        luma[y * width + x] = green
      }
    }
    var intrinsic: simd_float3x3?
    if let data = CMGetAttachment(
      sampleBuffer, key: kCMSampleBufferAttachmentKey_CameraIntrinsicMatrix, attachmentModeOut: nil)
      as? Data, data.count == MemoryLayout<simd_float3x3>.size
    {
      intrinsic = data.withUnsafeBytes { $0.loadUnaligned(as: simd_float3x3.self) }
    }
    let frame = CapturedFrame(
      pixels: pixels, luma: luma, width: width, height: height, orientation: 1, layout: .mono,
      black: [0, 0, 0, 0], white: [1, 1, 1, 1],
      midpoint: uptime(CMSampleBufferGetPresentationTimeStamp(sampleBuffer)),
      duration: device.exposureDuration.seconds, intrinsics: intrinsic,
      provenance: "processed-sRGB-assumption;timing-uncertainty-unknown;optics-unqualified")
    lock.lock()
    latest = frame
    lock.unlock()
  }
  func photoOutput(
    _ output: AVCapturePhotoOutput, didFinishProcessingPhoto captured: AVCapturePhoto, error: Error?
  ) {
    queue.async {
      guard self.photoID == captured.resolvedSettings.uniqueID else { return }
      if let error {
        self.resolve(.failure(error))
        return
      }
      guard let buffer = captured.pixelBuffer, captured.isRawPhoto else {
        self.resolve(.failure(DemoError.message("Bayer buffer unavailable.")))
        return
      }
      do { self.rawDelivered = try self.decodeRaw(buffer, captured) } catch {
        // RAW capability does not guarantee supported CFA/levels. Use a
        // separate processed frame with its own dimensions and timestamp.
        self.lock.lock()
        let fallback = self.latest
        self.lock.unlock()
        if let fallback, abs(ProcessInfo.processInfo.systemUptime - fallback.midpoint) < 3 {
          self.rawDelivered = fallback
        } else {
          self.resolve(.failure(error))
        }
      }
    }
  }
  func photoOutput(
    _ output: AVCapturePhotoOutput,
    didFinishCaptureFor resolvedSettings: AVCaptureResolvedPhotoSettings, error: Error?
  ) {
    queue.async {
      guard self.photoID == resolvedSettings.uniqueID else { return }
      if let error {
        self.resolve(.failure(error), finalized: true)
      } else if let frame = self.rawDelivered {
        self.resolve(.success(frame), finalized: true)
      } else {
        self.resolve(.failure(DemoError.message("No decoded camera frame.")), finalized: true)
      }
    }
  }
  private func decodeRaw(_ buffer: CVPixelBuffer, _ captured: AVCapturePhoto) throws
    -> CapturedFrame
  {
    let format = CVPixelBufferGetPixelFormatType(buffer)
    let formats: [OSType: SampleLayout] = [
      kCVPixelFormatType_14Bayer_RGGB: .rggb, kCVPixelFormatType_14Bayer_BGGR: .bggr,
      kCVPixelFormatType_14Bayer_GRBG: .grbg, kCVPixelFormatType_14Bayer_GBRG: .gbrg,
    ]
    guard let layout = formats[format],
      let metadata = captured.metadata[kCGImagePropertyDNGDictionary as String] as? [String: Any]
    else { throw DemoError.message("Unsupported RAW geometry.") }
    func levels(_ key: CFString) -> [Double]? {
      if let a = metadata[key as String] as? [NSNumber] { return a.map { $0.doubleValue } }
      if let n = metadata[key as String] as? NSNumber { return [n.doubleValue] }
      return nil
    }
    guard let blacks = levels(kCGImagePropertyDNGBlackLevel),
      let whites = levels(kCGImagePropertyDNGWhiteLevel)
    else { throw DemoError.message("RAW levels are missing.") }
    func four(_ v: [Double]) -> [Double]? {
      v.count == 1 ? Array(repeating: v[0], count: 4) : (v.count == 4 ? v : nil)
    }
    guard let black = four(blacks), let white = four(whites) else {
      throw DemoError.message("Unsupported RAW levels.")
    }
    let width = CVPixelBufferGetWidth(buffer)
    let height = CVPixelBufferGetHeight(buffer)
    let stride = CVPixelBufferGetBytesPerRow(buffer)
    guard width * height <= 16_000_000, stride >= 2 * width else {
      throw DemoError.message("RAW image exceeds the extraction budget.")
    }
    CVPixelBufferLockBaseAddress(buffer, .readOnly)
    defer { CVPixelBufferUnlockBaseAddress(buffer, .readOnly) }
    guard let base = CVPixelBufferGetBaseAddress(buffer) else {
      throw DemoError.message("Missing RAW pixels.")
    }
    var pixels = [Float](repeating: 0, count: width * height)
    for y in 0..<height {
      let row = base.advanced(by: y * stride).assumingMemoryBound(to: UInt16.self)
      for x in 0..<width { pixels[y * width + x] = Float(row[x]) }
    }
    let exif = captured.metadata[kCGImagePropertyExifDictionary as String] as? [String: Any]
    guard let duration = exif?[kCGImagePropertyExifExposureTime as String] as? Double, duration > 0
    else { throw DemoError.message("RAW exposure duration missing.") }
    return CapturedFrame(
      pixels: pixels, luma: [], width: width, height: height,
      orientation: (captured.metadata[kCGImagePropertyOrientation as String] as? NSNumber)?.intValue
        ?? 0, layout: layout, black: black, white: white, midpoint: uptime(captured.timestamp),
      duration: duration, intrinsics: nil,
      provenance: "native-green-Bayer;photo-timestamp-convention-unqualified;optics-unqualified")
  }
}
