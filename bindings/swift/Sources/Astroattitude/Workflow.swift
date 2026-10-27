import Foundation
import CAstroattitude

private struct WorkflowResponse<T: Decodable>: Decodable {
    let schema: String; let outcome: String; let value: T?; let error: SolverError?
}
private func workflowResponse<T: Decodable>(_ status: Int32, _ pointer: OpaquePointer?, as: T.Type) throws -> T {
    defer { aa_message_destroy_v1(pointer) }
    guard status <= 1 else { throw BridgeError.abi(status) }
    var length: UInt64 = 0
    guard let pointer, let bytes = aa_message_bytes_v1(pointer, &length), length <= 134217728 else { throw BridgeError.invalidResponse }
    let r = try JSONDecoder().decode(WorkflowResponse<T>.self, from: Data(bytes: bytes, count: Int(length)))
    if status == 1, let error = r.error { throw BridgeError.resource(error) }
    guard r.schema == "astroattitude.workflow-response/1", r.outcome == "success", let value = r.value else { throw BridgeError.invalidResponse }
    return value
}
private func workflowJSON<T: Encodable>(_ value: T) throws -> Any {
    func normalize(_ input: Any) -> Any {
        if let array = input as? [Any] { return array.map(normalize) }
        guard var obj = input as? [String: Any] else { return input }
        for (k,v) in obj { obj[k] = normalize(v) }
        if obj["duration_s"] != nil && obj["midpoint_sigma_s"] == nil { obj["midpoint_sigma_s"] = NSNull() }
        if obj["minimum_inliers"] != nil && obj["maximum_conditional_sigma_rad"] == nil { obj["maximum_conditional_sigma_rad"] = NSNull() }
        if obj["pixel"] != nil && obj["id"] != nil { for key in ["centroid_covariance_px2", "flux"] { if obj[key] == nil { obj[key] = NSNull() } } }
        if obj["cameras"] != nil && obj["frames"] != nil { for key in ["imu", "down_reference"] { if obj[key] == nil { obj[key] = NSNull() } } }
        if obj["kind"] as? String == "disabled" { obj.removeValue(forKey: "parameters") }
        return obj
    }
    return normalize(try JSONSerialization.jsonObject(with: JSONEncoder().encode(value), options: [.fragmentsAllowed]))
}
private func withToken<T>(_ cancel: CancellationToken?, _ call: (OpaquePointer?) throws -> T) throws -> T {
    if let cancel { return try cancel.handle.withLease { try call($0) } }
    return try call(nil)
}
/// Synchronous native extraction; pixels remain linear, in the declared raster/CFA layout.
public enum StarExtractor {
    public static func extract(pixels: [Float], metadata: ExtractionMetadata, mask: [UInt8] = [], cancel: CancellationToken? = nil) throws -> ExtractionResult {
        guard pixels.count <= 24000000, mask.count <= 24000000 else { throw BridgeError.abi(2) }
        var obj = try workflowJSON(metadata) as! [String: Any]; obj["schema"] = "astroattitude.extract/1"
        let data = try JSONSerialization.data(withJSONObject: obj)
        return try withToken(cancel) { token in
            var out: OpaquePointer?
            let status = pixels.withUnsafeBufferPointer { p in mask.withUnsafeBufferPointer { m in data.withUnsafeBytes { d in
                aa_extract_v1(p.baseAddress, UInt64(p.count), m.baseAddress, UInt64(m.count), d.bindMemory(to: UInt8.self).baseAddress, UInt64(d.count), token, &out)
            } } }
            return try workflowResponse(status, out, as: ExtractionResult.self)
        }
    }
}
/// Detached value snapshot plus an immutable native object. close waits for active calls.
public final class CalibrationArtifact<T: Decodable & Sendable>: @unchecked Sendable {
    public let data: T
    fileprivate let handle: Handle
    fileprivate init(_ p: OpaquePointer, _ data: T) { self.data = data; handle = Handle(p, aa_calibration_artifact_destroy_v1) }
    public func close() { handle.close() }
}
/// Calls are serialized. Camera acquisition and image decoding remain caller responsibilities.
public final class CalibrationSession: @unchecked Sendable {
    private let handle: Handle
    private let serial = NSLock()
    private init(pointer: OpaquePointer) { handle = Handle(pointer, aa_calibration_destroy_v1) }
    public convenience init(catalog: Data, astrometry: Data, configuration: CalibrationConfiguration) throws {
        let request = try JSONSerialization.data(withJSONObject: ["schema":"astroattitude.calibration-create/1", "configuration": workflowJSON(configuration)])
        var p: OpaquePointer?; var error: OpaquePointer?
        let status = catalog.withUnsafeBytes { c in astrometry.withUnsafeBytes { a in request.withUnsafeBytes { r in
            aa_calibration_create_v1(c.bindMemory(to: UInt8.self).baseAddress, UInt64(c.count), a.bindMemory(to: UInt8.self).baseAddress, UInt64(a.count), r.bindMemory(to: UInt8.self).baseAddress, UInt64(r.count), &p, &error)
        } } }
        if status != 0 { let _: String = try workflowResponse(status, error, as: String.self); throw BridgeError.invalidResponse }
        guard let p else { throw BridgeError.invalidResponse }; self.init(pointer:p)
    }
    public convenience init(catalog: Data, astrometry: Data, preparation: CalibrationPreparation) throws {
        let request = try JSONSerialization.data(withJSONObject: ["schema":"astroattitude.calibration-prepare/1", "preparation": workflowJSON(preparation)])
        var p: OpaquePointer?; var error: OpaquePointer?
        let status = catalog.withUnsafeBytes { c in astrometry.withUnsafeBytes { a in request.withUnsafeBytes { r in
            aa_calibration_create_v1(c.bindMemory(to: UInt8.self).baseAddress, UInt64(c.count), a.bindMemory(to: UInt8.self).baseAddress, UInt64(a.count), r.bindMemory(to: UInt8.self).baseAddress, UInt64(r.count), &p, &error)
        } } }
        if status != 0 { let _: String = try workflowResponse(status, error, as: String.self); throw BridgeError.invalidResponse }
        guard let p else { throw BridgeError.invalidResponse }; self.init(pointer:p)
    }
    public static func restore(catalog: Data, astrometry: Data, journal: String, cancel: CancellationToken? = nil) throws -> CalibrationSession {
        let request = Data(journal.utf8); guard request.count <= 134217728 else { throw BridgeError.abi(2) }
        return try withToken(cancel) { token in
            var p: OpaquePointer?; var error: OpaquePointer?
            let status = catalog.withUnsafeBytes { c in astrometry.withUnsafeBytes { a in request.withUnsafeBytes { r in
                aa_calibration_restore_v1(c.bindMemory(to: UInt8.self).baseAddress, UInt64(c.count), a.bindMemory(to: UInt8.self).baseAddress, UInt64(a.count), r.bindMemory(to: UInt8.self).baseAddress, UInt64(r.count), token, &p, &error)
            } } }
            if status != 0 { let _: String = try workflowResponse(status, error, as: String.self); throw BridgeError.invalidResponse }
            guard let p else { throw BridgeError.invalidResponse }; return CalibrationSession(pointer:p)
        }
    }
    public func close() { handle.close() }
    private func call<T: Decodable>(_ op: String, _ value: Any = NSNull(), candidate: CalibrationArtifact<CandidateData>? = nil, cancel: CancellationToken? = nil, as: T.Type) throws -> (T, OpaquePointer?) {
        let request = try JSONSerialization.data(withJSONObject: ["schema":"astroattitude.calibration-call/1", "operation":op, "value":value])
        serial.lock(); defer { serial.unlock() }
        return try handle.withLease { session in
            func invoke(_ cand: OpaquePointer?) throws -> (T, OpaquePointer?) {
                try withToken(cancel) { token in
                    var out: OpaquePointer?; var artifact: OpaquePointer?
                    let status = request.withUnsafeBytes { aa_calibration_call_v1(session, $0.bindMemory(to: UInt8.self).baseAddress, UInt64($0.count), cand, token, &artifact, &out) }
                    do { return (try workflowResponse(status, out, as: T.self), artifact) }
                    catch { aa_calibration_artifact_destroy_v1(artifact); throw error }
                }
            }
            if let candidate { return try candidate.handle.withLease { try invoke($0) } }
            return try invoke(nil)
        }
    }
    public func addFrame(_ frame: CalibrationFrame, cancel: CancellationToken? = nil) throws -> IngestReceipt { try call("add_frame", workflowJSON(frame), cancel: cancel, as: IngestReceipt.self).0 }
    public func finishBlock(_ block: BlockCompletion) throws -> BlockReceipt { try call("finish_block", workflowJSON(block), as: BlockReceipt.self).0 }
    public func progress() throws -> CalibrationProgress { try call("progress", as: CalibrationProgress.self).0 }
    public func nextAction() throws -> CalibrationAction { try call("next_action", as: CalibrationAction.self).0 }
    public func modelDiagnostics() throws -> [OpticalModelDiagnostic] { try call("model_diagnostics", as:[OpticalModelDiagnostic].self).0 }
    public func fit(cancel: CancellationToken? = nil) throws -> CalibrationArtifact<CandidateData> { let (data,p) = try call("fit", cancel:cancel, as: CandidateData.self); guard let p else { throw BridgeError.invalidResponse }; return CalibrationArtifact(p,data) }
    public func proposeValidation(_ request: ValidationRequest, cancel: CancellationToken? = nil) throws -> ValidationPlan { try call("propose_validation", workflowJSON(request), cancel:cancel, as:ValidationPlan.self).0 }
    public func freeze(_ candidate: CalibrationArtifact<CandidateData>, plan: ValidationPlan) throws -> String { try call("freeze", workflowJSON(plan), candidate:candidate, as:String.self).0 }
    public func validate(cancel: CancellationToken? = nil) throws -> CalibrationArtifact<ValidationReport> { let (data,p) = try call("validate", cancel:cancel, as:ValidationReport.self); guard let p else { throw BridgeError.invalidResponse }; return CalibrationArtifact(p,data) }
    public func exportValidated(cancel: CancellationToken? = nil) throws -> CalibrationPackage { try call("export_validated", cancel:cancel, as:CalibrationPackage.self).0 }
    public func journal() throws -> String { try call("journal", as:String.self).0 }
    public func pause() throws -> CalibrationStage { try call("pause", as:CalibrationStage.self).0 }
    public func resume() throws -> CalibrationStage { try call("resume", as:CalibrationStage.self).0 }
    public func end() throws -> CalibrationStage { try call("end", as:CalibrationStage.self).0 }
}

/// Experimental camera comparisons and rotation-only calibration from distinct poses.
public enum Alignment {
    public static func measure(_ request: AlignmentRequest, cancel: CancellationToken? = nil) throws -> AlignmentReport {
        try call(request, schema: "astroattitude.alignment/1", target: false, cancel: cancel)
    }
    public static func calibrateRig(_ request: RigAlignmentRequest, cancel: CancellationToken? = nil) throws -> AlignmentReport {
        try call(request, schema: "astroattitude.rig-alignment/1", target: false, rig: true, cancel: cancel)
    }
    public static func targetPose(_ request: PlanarPoseRequest, cancel: CancellationToken? = nil) throws -> PlanarPose {
        try call(request, schema: "astroattitude.planar-pose/1", target: true, cancel: cancel)
    }
    private static func call<T: Encodable, U: Decodable>(_ value: T, schema: String, target: Bool, rig: Bool = false, cancel: CancellationToken?) throws -> U {
        let data = try JSONSerialization.data(withJSONObject: ["schema":schema,"value":try workflowJSON(value)])
        guard data.count <= 6 * 1048576 else { throw BridgeError.invalidResponse }
        return try withToken(cancel) { token in
            var response: OpaquePointer?
            let status = data.withUnsafeBytes { bytes in
                let p = bytes.bindMemory(to: UInt8.self).baseAddress
                if rig { return aa_rig_alignment_v1(p, UInt64(data.count), token, &response) }
                return target ? aa_planar_pose_v1(p, UInt64(data.count), token, &response) : aa_alignment_measure_v1(p, UInt64(data.count), token, &response)
            }
            return try workflowResponse(status, response, as: U.self)
        }
    }
}
