import AlignmentAdapter
import Astroattitude
import CryptoKit
import Foundation

enum DemoError: LocalizedError {
  static func describe(_ error: Error) -> String {
    if case BridgeError.resource(let failure) = error { return failure.detail }
    return error.localizedDescription
  }
  case message(String)
  var errorDescription: String? {
    if case .message(let text) = self { return text }
    return nil
  }
}
struct SkyResources: Sendable {
  let catalog: Data
  let astrometry: Data
  let validUntil: Date
  let manifest: Data
}
/// These requests carry only fixed public URLs, never observation data.
/// Conversion runs off the main actor; the native loader remains the final gate.
private final class ReferenceRedirects: NSObject, URLSessionTaskDelegate {
  func urlSession(
    _ session: URLSession, task: URLSessionTask,
    willPerformHTTPRedirection response: HTTPURLResponse, newRequest request: URLRequest,
    completionHandler: @escaping (URLRequest?) -> Void
  ) {
    completionHandler(
      request.url?.scheme == "https" && request.url?.host == task.originalRequest?.url?.host
        ? request : nil)
  }
}
enum ReferenceData {
  static let urls = [
    "https://cdsarc.cds.unistra.fr/ftp/cats/I/239/hip_main.dat",
    "https://maia.usno.navy.mil/ser7/finals2000A.all",
    "https://data.iana.org/time-zones/data/leap-seconds.list",
    "https://hpiers.obspm.fr/iers/bul/bulc/bulletinc.dat",
  ]
  static func sha(_ data: Data) -> String {
    SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
  }
  static func json(_ value: Any) throws -> Data {
    try JSONSerialization.data(withJSONObject: value, options: [.sortedKeys])
  }
  static func cacheDirectory() throws -> URL {
    let path = try FileManager.default.url(
      for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true
    ).appendingPathComponent("ReferenceData", isDirectory: true)
    try FileManager.default.createDirectory(at: path, withIntermediateDirectories: true)
    var u = path
    var v = URLResourceValues()
    v.isExcludedFromBackup = true
    try u.setResourceValues(v)
    return path
  }
  static func receipt(catalog: Data, astrometry: Data, manifest: Data) throws -> Data {
    try json([
      "catalog_sha256": sha(catalog), "astrometry_sha256": sha(astrometry),
      "manifest_sha256": sha(manifest),
    ])
  }
  static func verifyReceipt(_ receipt: Data, catalog: Data, astrometry: Data, manifest: Data) throws
  {
    let expected = try self.receipt(catalog: catalog, astrometry: astrometry, manifest: manifest)
    guard receipt == expected else {
      throw DemoError.message("Cached reference data changed. Download it again.")
    }
  }
  static func cached(now: Date = Date()) throws -> SkyResources {
    let dir = try cacheDirectory()
    let c = try Data(contentsOf: dir.appendingPathComponent("catalog.json"))
    let a = try Data(contentsOf: dir.appendingPathComponent("astrometry.json"))
    let manifest = try Data(contentsOf: dir.appendingPathComponent("conversion-manifest.json"))
    try verifyReceipt(
      Data(contentsOf: dir.appendingPathComponent("receipt.json")),
      catalog: c, astrometry: a, manifest: manifest)
    for package in [c, a] {
      let document = try JSONSerialization.jsonObject(with: package) as? [String: Any]
      guard let provenance = document?["provenance"] as? [String: Any],
        provenance["conversion_manifest_sha256"] as? String == sha(manifest)
      else { throw DemoError.message("Reference provenance changed. Download it again.") }
    }
    let parsed = try JSONSerialization.jsonObject(with: a) as? [String: Any]
    guard let until = parsed?["valid_until_utc_s"] as? Double,
      until > now.timeIntervalSince1970 + 300
    else { throw DemoError.message("Download current sky reference data.") }
    let engine = try Engine(catalog: c, astrometry: a)
    engine.close()
    return SkyResources(
      catalog: c, astrometry: a, validUntil: Date(timeIntervalSince1970: until), manifest: manifest)
  }
  static func download() async throws -> SkyResources {
    let config = URLSessionConfiguration.ephemeral
    config.timeoutIntervalForRequest = 60
    config.timeoutIntervalForResource = 240
    config.httpShouldSetCookies = false
    config.urlCache = nil
    let session = URLSession(
      configuration: config, delegate: ReferenceRedirects(), delegateQueue: nil)
    defer { session.invalidateAndCancel() }
    var sources = [Data]()
    for (i, address) in urls.enumerated() {
      try Task.checkCancellation()
      let (bytes, response) = try await session.bytes(from: URL(string: address)!)
      guard (response as? HTTPURLResponse)?.statusCode == 200, response.url?.scheme == "https"
      else { throw DemoError.message("Reference source unavailable. Try again later.") }
      var data = Data()
      let limit = i == 0 ? 64 * 1024 * 1024 : 8 * 1024 * 1024
      for try await byte in bytes {
        data.append(byte)
        if data.count > limit {
          throw DemoError.message("Reference download exceeded its size limit.")
        }
      }
      sources.append(data)
    }
    let converted = try convert(sources, now: Date())
    let dir = try cacheDirectory()
    try converted.catalog.write(to: dir.appendingPathComponent("catalog.json"), options: .atomic)
    try converted.astrometry.write(
      to: dir.appendingPathComponent("astrometry.json"), options: .atomic)
    try converted.manifest.write(
      to: dir.appendingPathComponent("conversion-manifest.json"), options: .atomic)
    try receipt(
      catalog: converted.catalog, astrometry: converted.astrometry,
      manifest: converted.manifest
    ).write(to: dir.appendingPathComponent("receipt.json"), options: .atomic)
    return converted
  }
  static func convert(_ inputs: [Data], now: Date) throws -> SkyResources {
    guard inputs.count == 4 else { throw DemoError.message("Missing reference sources.") }
    let text = try inputs.map { data -> String in
      guard let s = String(data: data, encoding: .ascii) else {
        throw DemoError.message("Unsupported reference encoding.")
      }
      return s
    }
    let announcement = text[3].split(whereSeparator: { $0.isWhitespace }).joined(separator: " ")
    let regex = try NSRegularExpression(
      pattern: "NO leap second will be introduced at the end of (June|December) ([0-9]{4})",
      options: .caseInsensitive)
    guard
      let m = regex.firstMatch(
        in: announcement, range: NSRange(announcement.startIndex..., in: announcement)),
      let mr = Range(m.range(at: 1), in: announcement),
      let yr = Range(m.range(at: 2), in: announcement), let year = Int(announcement[yr]),
      (1972...9998).contains(year)
    else {
      throw DemoError.message(
        "Unsupported leap announcement. Update the example before using this reference data.")
    }
    var calendar = Calendar(identifier: .gregorian)
    calendar.timeZone = TimeZone(secondsFromGMT: 0)!
    let december = announcement[mr].lowercased() == "december"
    guard
      let bound = calendar.date(
        from: DateComponents(year: year + (december ? 1 : 0), month: december ? 1 : 7, day: 1))
    else { throw DemoError.message("Invalid leap announcement.") }
    let start = Int(floor(now.timeIntervalSince1970 / 86400)) * 86400 - 86400
    let end = start + 4 * 86400
    var leapRows = [[String: Any]]()
    var expiry = 0
    for line in text[2].split(separator: "\n") {
      if line.hasPrefix("#@") {
        guard let ntp = Int(line.dropFirst(2).trimmingCharacters(in: .whitespaces)),
          (2_208_988_800...253_402_300_799).contains(ntp)
        else {
          throw DemoError.message("Invalid leap expiry.")
        }
        expiry = ntp - 2_208_988_800
      }
      if line.hasPrefix("#") { continue }
      let f = line.split(whereSeparator: { $0.isWhitespace })
      if f.count >= 2, let t = Int(f[0]), let n = Int(f[1]),
        (2_208_988_800...253_402_300_799).contains(t), (0...100).contains(n)
      {
        leapRows.append(["effective_utc_s": t - 2_208_988_800, "tai_minus_utc_s": n])
      }
    }
    let until = min(expiry, Int(bound.timeIntervalSince1970))
    guard end + 86400 < until,
      let initial = leapRows.lastIndex(where: { ($0["effective_utc_s"] as! Int) <= start })
    else {
      throw DemoError.message(
        "Leap data does not cover the next measurement. Download current data.")
    }
    leapRows = Array(leapRows[initial...]).filter { ($0["effective_utc_s"] as! Int) <= end }
    var eop = [[String: Any]]()
    func field(_ line: String, _ a: Int, _ b: Int) -> String {
      let v = Array(line.utf8)
      guard v.count >= b else { return "" }
      return String(bytes: v[a..<b], encoding: .ascii)!.trimmingCharacters(in: .whitespaces)
    }
    for line in text[1].components(separatedBy: .newlines) {
      guard let mjd = Double(field(line, 7, 15)), (40000...100000).contains(mjd),
        mjd.rounded() == mjd
      else { continue }
      let t = Int((mjd - 40587) * 86400)
      if t < start - 86400 || t > end + 86400 { continue }
      var x = Double(field(line, 134, 144))
      var y = Double(field(line, 144, 154))
      var d = Double(field(line, 154, 165))
      var status = "observed"
      if x == nil || y == nil || d == nil {
        let flags = [field(line, 16, 17), field(line, 57, 58)]
        guard flags.allSatisfy({ $0 == "I" || $0 == "P" }) else { continue }
        status = flags.contains("P") ? "predicted" : "observed"
        x = Double(field(line, 18, 27))
        y = Double(field(line, 37, 46))
        d = Double(field(line, 58, 68))
      }
      if let x, let y, let d {
        eop.append([
          "utc_s": t, "xp_rad": x * .pi / 648000, "yp_rad": y * .pi / 648000, "ut1_minus_utc_s": d,
          "status": status,
        ])
      }
    }
    guard let lo = eop.lastIndex(where: { ($0["utc_s"] as! Int) <= start }),
      let hi = eop.firstIndex(where: { ($0["utc_s"] as! Int) >= end }), lo < hi
    else { throw DemoError.message("Earth-orientation data does not cover this date.") }
    eop = Array(eop[lo...hi])
    var stars = [[String: Any]]()
    var rows = 0
    var excluded = 0
    for line in text[0].split(separator: "\n") {
      rows += 1
      let f = line.components(separatedBy: "|").map { $0.trimmingCharacters(in: .whitespaces) }
      guard f.count > 59, let id = Int(f[1]), ![54738, 114110, 114176].contains(id),
        [2, 6, 10, 59].allSatisfy({ f[$0].isEmpty }), let ra = Double(f[8]), let dec = Double(f[9]),
        let px = Double(f[11]), let pmr = Double(f[12]), let pmd = Double(f[13]),
        let mag = Double(f[5]), [ra, dec, px, pmr, pmd, mag].allSatisfy({ $0.isFinite }),
        (0..<360).contains(ra), px > 0, mag <= 6.5, abs(dec) < 90
      else {
        excluded += 1
        continue
      }
      let factor = Double.pi / 648_000_000
      let input = [
        ra * .pi / 180, dec * .pi / 180, pmr * factor / cos(dec * .pi / 180), pmd * factor,
        px / 1000, 0, 2448349.0625,
      ]
      var out = Array(repeating: 0.0, count: 6)
      guard aa_demo_propagate(input, &out) == 0 else {
        excluded += 1
        continue
      }
      stars.append([
        "id": String(id), "ra_rad": out[0], "dec_rad": out[1], "pm_ra_rad_per_jyear": out[2],
        "pm_dec_rad_per_jyear": out[3], "parallax_arcsec": out[4], "radial_velocity_km_s": out[5],
        "magnitude": mag, "motion_status": "measured", "parallax_status": "measured",
        "radial_velocity_status": "assumed",
      ])
    }
    guard rows > 100000, stars.count >= 6, stars.count <= 10000 else {
      throw DemoError.message("Incomplete or unsupported Hipparcos source.")
    }
    stars.sort { ($0["id"] as! String) < ($1["id"] as! String) }
    let roles = ["catalog", "eop", "leaps", "announcement"]
    let records = inputs.enumerated().map { i, data -> [String: Any] in
      [
        "source_id": roles[i], "source_version": "sha256:" + sha(data), "source_uri": urls[i],
        "source_sha256": sha(data),
        "license_id": i == 2 ? "public-domain" : "LicenseRef-Upstream-Reference-Data",
        "attribution": i == 0
          ? "ESA 1997, The Hipparcos and Tycho Catalogues, ESA SP-1200"
          : "IERS / USNO / IANA reference data", "role": roles[i],
      ]
    }
    let manifest = try json([
      "converter": "ios-alignment-reference/1", "sources": records, "input_rows": rows,
      "selected": stars.count, "excluded": excluded,
      "radial_velocity": "assumed_zero_then_ERFA_pmsafe", "epoch_from": 2448349.0625,
      "epoch_to": 2_451_545, "valid_from": start, "valid_until": end, "announcement_bound": until,
    ])
    func provenance(_ selected: [Int]) -> [String: Any] {
      [
        "sources": selected.map { records[$0] }.sorted {
          ($0["source_id"] as! String) < ($1["source_id"] as! String)
        }, "conversion_id": "ios-alignment-reference/1",
        "conversion_manifest_sha256": sha(manifest),
      ]
    }
    let catalog = try json([
      "schema": "astroattitude.catalog/1", "provenance": provenance([0]), "valid_from_utc_s": start,
      "valid_until_utc_s": end, "namespace": "hipparcos", "frame": "ICRS",
      "epoch_jd_tdb": 2451545.0,
      "coverage": [
        "kind": "declination_band", "min_dec_rad": -Double.pi / 2, "max_dec_rad": Double.pi / 2,
        "limiting_magnitude": 6.5,
      ], "stars": stars,
    ])
    let astro = try json([
      "schema": "astroattitude.astrometry/1", "provenance": provenance([1, 2, 3]),
      "valid_from_utc_s": start, "valid_until_utc_s": end, "model_id": "erfa-2.0.1-iau2006a-tt/1",
      "leap_valid_until_utc_s": until, "leaps": leapRows, "eop": eop,
    ])
    let engine = try Engine(catalog: catalog, astrometry: astro)
    engine.close()
    return SkyResources(
      catalog: catalog, astrometry: astro, validUntil: Date(timeIntervalSince1970: Double(end)),
      manifest: manifest)
  }
}
