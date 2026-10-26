import AVFoundation
import Astroattitude
import SwiftUI
import WebKit

@main struct AlignmentApp: App {
  @StateObject private var model = AlignmentModel()
  var body: some Scene { WindowGroup { AlignmentView(model: model) } }
}
struct AlignmentView: View {
  @ObservedObject var model: AlignmentModel
  @State private var showTarget = false
  @State private var showExportWarning = false
  var body: some View {
    NavigationStack {
      ScrollView {
        VStack(alignment: .leading, spacing: 18) {
          Text("Measure camera alignment with guided stationary poses.").font(.headline)
          Picker("Scene", selection: $model.mode) {
            ForEach(SceneMode.allCases) { Text($0.rawValue).tag($0) }
          }.pickerStyle(.segmented).disabled(model.running || model.preparing).onChange(
            of: model.mode
          ) { _ in model.resetPreparation() }
          Toggle("Full alignment with guided tilts", isOn: $model.guidedRepositioning).disabled(
            model.running || model.preparing)
          if model.synthetic {
            Label("Synthetic demonstration — no sensor measurements", systemImage: "testtube.2")
              .foregroundStyle(.orange)
          }
          if !model.synthetic && model.previewReady {
            CameraPreview(session: model.rig.session).frame(height: 180).clipped()
              .accessibilityLabel("Camera preview")
          }
          if let count = model.countdown {
            Text("\(count)").font(.system(size: 64, weight: .bold, design: .rounded)).frame(
              maxWidth: .infinity
            ).accessibilityLabel("\(count) seconds to position phone").accessibilityIdentifier(
              "countdown")
          }
          Text(model.status).accessibilityIdentifier("status")
          if !model.cameraNames.isEmpty {
            Text(model.cameraNames.joined(separator: " · ")).font(.caption).foregroundStyle(
              .secondary)
          }
          if model.running {
            if model.waitingForPose {
              Button("Ready for next pose") { model.continuePose() }.buttonStyle(.borderedProminent)
            }
            Button("Cancel", role: .cancel) {
              model.finish("Measurement cancelled. Results are partial.")
            }.buttonStyle(.bordered)
          } else {
            HStack {
              Button("Prepare") { Task { await model.prepare() } }.disabled(model.preparing)
              Button("Start") { model.start() }.disabled(!model.ready || model.preparing)
                .buttonStyle(.borderedProminent)
            }
            if model.mode == .stars && !model.synthetic {
              Button("Download sky references") { Task { await model.download() } }.disabled(
                model.preparing)
              Text(
                "Downloads public reference data only. Location and measurements stay on this phone."
              ).font(.caption)
            }
            if model.mode == .target {
              Button("View / share printable target") { showTarget = true }
            }
          }
          if model.preparing { ProgressView() }
          if let report = model.report {
            Text("Alignment results").font(.title2).accessibilityIdentifier("results")
            ForEach(Array(report.pairs.enumerated()), id: \.offset) { _, pair in
              AlignmentResultRow(
                pair: pair, from: model.displayName(pair.from_sensor),
                to: model.displayName(pair.to_sensor))
            }
            Button("Export result…") { showExportWarning = true }
            if let url = model.exportURL { ShareLink("Share local result file", item: url) }
          }
          ForEach(model.reasons.keys.sorted(), id: \.self) { id in
            Text("\(model.displayName(id)): \(model.reasons[id] ?? "")").font(.caption)
          }
          Text(
            "Experimental. Physical accuracy is unqualified. Full accelerometer rotation requires distinct tilts. Bias and scale remain uncalibrated. No images are saved."
          ).font(.footnote).foregroundStyle(.secondary)
        }.padding()
      }.navigationTitle("Camera alignment")
        .sheet(isPresented: $showTarget) { TargetView() }
        .alert("Export a private measurement?", isPresented: $showExportWarning) {
          Button("Export") { model.saveResult() }
          Button("Cancel", role: .cancel) {}
        } message: {
          Text(
            "Orientation and device information may reveal where a measurement was made. Share only with people you intend to receive it."
          )
        }
    }
  }
}
struct CameraPreview: UIViewRepresentable {
  let session: AVCaptureSession
  final class Host: UIView {
    override class var layerClass: AnyClass { AVCaptureVideoPreviewLayer.self }
  }
  func makeUIView(context: Context) -> Host {
    let v = Host()
    let l = v.layer as! AVCaptureVideoPreviewLayer
    l.session = session
    l.videoGravity = .resizeAspect
    return v
  }
  func updateUIView(_ view: Host, context: Context) {}
}
struct TargetView: View {
  @Environment(\.dismiss) private var dismiss
  var body: some View {
    NavigationStack {
      VStack {
        Text(
          "Print at 100% on A4 paper. Mount flat. The black marker borders are 50 mm wide. Show at least two markers to each camera; avoid a perfectly frontal view."
        ).padding()
        TargetWebView()
        if let url = Bundle.main.url(forResource: "target", withExtension: "svg") {
          ShareLink("Share target", item: url).padding()
        }
      }.navigationTitle("Printed target").toolbar { Button("Done") { dismiss() } }
    }
  }
}
struct TargetWebView: UIViewRepresentable {
  func makeUIView(context: Context) -> WKWebView {
    let view = WKWebView()
    if let u = Bundle.main.url(forResource: "target", withExtension: "svg") {
      view.loadFileURL(u, allowingReadAccessTo: u)
    }
    return view
  }
  func updateUIView(_ view: WKWebView, context: Context) {}
}

private struct AlignmentResultRow: View {
  let pair: AlignmentPair
  let from: String, to: String
  private func vector(_ v: Vec3) -> String {
    String(
      format: "x %.3f°, y %.3f°, z %.3f°", v.x * 180 / Double.pi, v.y * 180 / Double.pi,
      v.z * 180 / Double.pi)
  }
  var body: some View {
    VStack(alignment: .leading, spacing: 6) {
      Text("\(from) → \(to)").font(.headline)
      if let v = pair.misalignment_vector_rad {
        Text("Alignment correction: " + vector(v))
      } else if let v = pair.rotation_vector_rad {
        Text("Relative rotation: " + vector(v))
      }
      if let tilt = pair.gravity_mismatch_rad {
        Text(String(format: "Gravity mismatch: %.3f°", tilt * 180 / Double.pi))
        Text("Twist about gravity is undetermined.").font(.caption)
      }
      if pair.status == .gravity_reference_unavailable {
        Text("Unavailable: the target has no gravity reference in one pose.")
      } else if pair.status == .pose_diversity_insufficient {
        Text("Unavailable: more distinct stationary tilts are needed.")
      } else {
        Text(pair.status.rawValue.replacingOccurrences(of: "_", with: " ")).foregroundStyle(
          .secondary)
      }
      Text("Usable camera frames: \(pair.to_frames); poses: \(pair.pose_count)").font(.caption)
      DisclosureGroup("Details") {
        Text(
          "Matrices map column vectors from the first sensor to the second. Components are axis–angle vectors, not Euler angles. Alignment correction is relative to the nominal device axes. Camera axes: right, down, forward."
        ).font(.caption)
        if let scatter = pair.maximum_repeatability_rad {
          Text(
            String(
              format: "Maximum repeatability change: %.3f°; diagnostic only.",
              scatter * 180 / Double.pi)
          ).font(.caption)
        }
        if let residual = pair.maximum_gravity_residual_rad {
          Text(
            String(
              format: "Maximum gravity fit error: %.3f°; not an accuracy bound.",
              residual * 180 / Double.pi)
          ).font(.caption)
        }
        ForEach(pair.limitations, id: \.self) {
          Text($0.replacingOccurrences(of: "_", with: " ")).font(.caption)
        }
      }
    }.padding().frame(maxWidth: .infinity, alignment: .leading).background(
      .quaternary, in: RoundedRectangle(cornerRadius: 12))
  }
}
