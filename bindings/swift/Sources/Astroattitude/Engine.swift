import Foundation
import CAstroattitude

public struct WireUInt64: Codable, Sendable, ExpressibleByIntegerLiteral {
    public var value: UInt64
    public init(_ value: UInt64) { self.value = value }
    public init(integerLiteral value: UInt64) { self.value = value }
    public init(from decoder: Decoder) throws {
        let c = try decoder.singleValueContainer()
        let s = try c.decode(String.self)
        guard let v = UInt64(s), String(v) == s else { throw DecodingError.dataCorruptedError(in: c, debugDescription: "Invalid uint64") }
        value = v
    }
    public func encode(to encoder: Encoder) throws { var c = encoder.singleValueContainer(); try c.encode(String(value)) }
}
public struct WireInt64: Codable, Sendable, ExpressibleByIntegerLiteral {
    public var value: Int64
    public init(_ value: Int64) { self.value = value }
    public init(integerLiteral value: Int64) { self.value = value }
    public init(from decoder: Decoder) throws {
        let c = try decoder.singleValueContainer(); let s = try c.decode(String.self)
        guard let v = Int64(s), String(v) == s else { throw DecodingError.dataCorruptedError(in: c, debugDescription: "Invalid int64") }
        value = v
    }
    public func encode(to encoder: Encoder) throws { var c = encoder.singleValueContainer(); try c.encode(String(value)) }
}
public enum BridgeError: Error { case abi(Int32), closed, resource(SolverError), invalidResponse }
public enum SolveOutcome: Sendable { case success(AttitudeSolution), failure(SolverError) }
private struct Response: Decodable { let schema: String; let outcome: String; let solution: AttitudeSolution?; let error: SolverError? }
private func response(_ pointer: OpaquePointer?) throws -> Response {
    guard let pointer else { throw BridgeError.invalidResponse }
    defer { aa_message_destroy_v1(pointer) }
    var length: UInt64 = 0
    guard let bytes = aa_message_bytes_v1(pointer, &length), length <= 262144 else { throw BridgeError.invalidResponse }
    return try JSONDecoder().decode(Response.self, from: Data(bytes: bytes, count: Int(length)))
}
// Active calls retain the object; close blocks until their leases return. Concurrent
// solves and cancellation requests do not hold the condition while native code runs.
final class Handle: @unchecked Sendable {
    let condition = NSCondition()
    var pointer: OpaquePointer?
    var active = 0
    var closing = false
    let destroy: (OpaquePointer?) -> Void
    init(_ pointer: OpaquePointer?, _ destroy: @escaping (OpaquePointer?) -> Void) { self.pointer = pointer; self.destroy = destroy }
    func withLease<T>(_ body: (OpaquePointer) throws -> T) throws -> T {
        condition.lock()
        guard !closing, let p = pointer else { condition.unlock(); throw BridgeError.closed }
        active += 1; condition.unlock()
        defer { condition.lock(); active -= 1; condition.broadcast(); condition.unlock() }
        return try body(p)
    }
    func close() {
        condition.lock(); defer { condition.unlock() }
        closing = true
        while active > 0 { condition.wait() }
        if let p = pointer { destroy(p); pointer = nil }
    }
    deinit { close() }
}
public final class CancellationToken: @unchecked Sendable {
    let handle: Handle
    public init() throws { var p: OpaquePointer?; let status = aa_cancel_create_v1(&p); guard status == 0 else { throw BridgeError.abi(status) }; handle = Handle(p, aa_cancel_destroy_v1) }
    public func request() throws { try handle.withLease { aa_cancel_request_v1($0) } }
    public func close() { handle.close() }
}
public final class Engine: @unchecked Sendable {
    private let handle: Handle
    public init(catalog: Data, astrometry: Data) throws {
        var p: OpaquePointer?; var e: OpaquePointer?
        let status = catalog.withUnsafeBytes { c in astrometry.withUnsafeBytes { d in
            aa_engine_create_v1(c.bindMemory(to: UInt8.self).baseAddress, UInt64(c.count), d.bindMemory(to: UInt8.self).baseAddress, UInt64(d.count), &p, &e)
        } }
        if status == 1 { guard let error = try response(e).error else { throw BridgeError.invalidResponse }; throw BridgeError.resource(error) }
        guard status == 0 else { throw BridgeError.abi(status) }
        handle = Handle(p, aa_engine_destroy_v1)
    }
    public func close() { handle.close() }
    public func solve(_ observation: RayObservation, context: EarthContext, options: SolveOptions, cancel: CancellationToken? = nil) throws -> SolveOutcome {
        try solveValue(kind: "rays", observation: observation, calibration: nil, context: context, options: options, cancel: cancel)
    }
    public func solve(_ observation: PixelObservation, calibration: CameraCalibration, context: EarthContext, options: SolveOptions, cancel: CancellationToken? = nil) throws -> SolveOutcome {
        try solveValue(kind: "pixels", observation: observation, calibration: calibration, context: context, options: options, cancel: cancel)
    }
    private func solveValue<T: Encodable>(kind: String, observation: T, calibration: CameraCalibration?, context: EarthContext, options: SolveOptions, cancel: CancellationToken?) throws -> SolveOutcome {
        // Swift's synthesized optional encoding omits nil: wire v1 requires explicit
        // null except the tagged disabled-refraction branch. Normalize that shape.
        func json<U: Encodable>(_ value: U) throws -> Any { try JSONSerialization.jsonObject(with: JSONEncoder().encode(value)) }
        var obs = try json(observation) as! [String: Any]
        var identity = obs["identity"] as! [String: Any]; var exposure = identity["exposure"] as! [String: Any]
        if exposure["midpoint_sigma_s"] == nil { exposure["midpoint_sigma_s"] = NSNull() }; identity["exposure"] = exposure; obs["identity"] = identity
        obs["stars"] = (obs["stars"] as! [[String: Any]]).map { star -> [String: Any] in var s = star; for key in kind == "rays" ? ["direction_covariance_rad2"] : ["centroid_covariance_px2", "flux"] { if s[key] == nil { s[key] = NSNull() } }; return s }
        var opts = try json(options) as! [String: Any]; var acceptance = opts["acceptance"] as! [String: Any]
        if acceptance["maximum_conditional_sigma_rad"] == nil { acceptance["maximum_conditional_sigma_rad"] = NSNull() }; opts["acceptance"] = acceptance
        let data = try JSONSerialization.data(withJSONObject: ["schema": "astroattitude.solve/1", "kind": kind, "observation": obs, "calibration": try calibration.map { try json($0) } ?? NSNull(), "context": try json(context), "options": opts])
        return try handle.withLease { engine in
            func call(_ token: OpaquePointer?) throws -> SolveOutcome {
                var message: OpaquePointer?
                let status = data.withUnsafeBytes { aa_solve_v1(engine, $0.bindMemory(to: UInt8.self).baseAddress, UInt64($0.count), token, &message) }
                guard status == 0 || status == 1 else { throw BridgeError.abi(status) }
                let result = try response(message)
                if status == 0, let solution = result.solution { return .success(solution) }
                if status == 1, let error = result.error { return .failure(error) }
                throw BridgeError.invalidResponse
            }
            if let cancel { return try cancel.handle.withLease { try call($0) } }
            return try call(nil)
        }
    }
}
