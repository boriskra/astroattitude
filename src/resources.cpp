#include "internal.hpp"
#include <picosha2.h>
namespace astroattitude {
J parse_json(std::span<const std::byte> b, size_t max, size_t depth,
             size_t nodes) {
  require(b.size() <= max, "JSON byte limit exceeded",
          ErrorCode::ResourceLimitExceeded);
  require(!b.empty(), "Empty JSON");
  auto p = reinterpret_cast<const char *>(b.data());
  require(!(b.size() >= 3 && (unsigned char)p[0] == 239 &&
            (unsigned char)p[1] == 187 && (unsigned char)p[2] == 191),
          "BOM forbidden");
  std::vector<std::set<std::string>> objects(depth + 2);
  size_t count = 0;
  auto callback = [&](int d, J::parse_event_t e, J &v) {
    require(d >= 0 && size_t(d) <= depth, "JSON depth exceeded",
            ErrorCode::ResourceLimitExceeded);
    if (e == J::parse_event_t::object_start ||
        e == J::parse_event_t::array_start || e == J::parse_event_t::value ||
        e == J::parse_event_t::key)
      require(++count <= nodes, "JSON node limit exceeded",
              ErrorCode::ResourceLimitExceeded);
    if (e == J::parse_event_t::object_start)
      objects[d].clear();
    if (e == J::parse_event_t::key) {
      auto s = v.get<std::string>();
      require(s.size() <= 1024, "Key too long");
      require(d > 0 && objects[d - 1].insert(s).second, "Duplicate JSON key");
    }
    if (e == J::parse_event_t::value && v.is_string())
      require(v.get_ref<const std::string &>().size() <= 1024,
              "String too long");
    return true;
  };
  return J::parse(p, p + b.size(), callback, true, false);
}
void keys(const J &j, std::initializer_list<const char *> k) {
  require(j.is_object() && j.size() == k.size(), "Wrong object fields");
  for (auto s : k)
    require(j.contains(s), "Missing field");
}
double number(const J &j) {
  require(j.is_number(), "Expected number");
  double d = j.get<double>();
  require(finite(d), "Nonfinite number");
  return d;
}
int64_t integer(const J &j) {
  require(j.is_number_integer(), "Expected integral JSON value");
  if (j.is_number_unsigned())
    require(j.get<uint64_t>() <= uint64_t(INT64_MAX), "Integer overflow");
  return j.get<int64_t>();
}
std::string string(const J &j, size_t max) {
  require(j.is_string(), "Expected string");
  auto s = j.get<std::string>();
  identifier(s, max);
  return s;
}
std::string digest(std::span<const std::byte> b) {
  auto p = reinterpret_cast<const unsigned char *>(b.data());
  return "sha256:" + picosha2::hash256_hex_string(p, p + b.size());
}
static void hash(const J &j) {
  auto s = string(j, 64);
  require(s.size() == 64 &&
              s.find_first_not_of("0123456789abcdef") == std::string::npos,
          "Invalid SHA256");
}
static void provenance(const J &j, bool cat) {
  keys(j, {"sources", "conversion_id", "conversion_manifest_sha256"});
  string(j.at("conversion_id"));
  hash(j.at("conversion_manifest_sha256"));
  auto &a = j.at("sources");
  require(a.is_array() && !a.empty() && a.size() <= 16, "Invalid source list");
  std::string last;
  std::set<std::string> roles;
  for (auto &s : a) {
    keys(s, {"source_id", "source_version", "source_uri", "source_sha256",
             "license_id", "attribution", "role"});
    auto id = string(s.at("source_id"));
    require(id > last, "Sources must have sorted unique IDs");
    last = id;
    string(s.at("source_version"));
    string(s.at("source_uri"), 1024);
    string(s.at("license_id"));
    require(s.at("attribution").is_string() &&
                !s.at("attribution").get_ref<const std::string &>().empty(),
            "Missing attribution");
    hash(s.at("source_sha256"));
    auto r = string(s.at("role"));
    require(r == "catalog" || r == "eop" || r == "leaps" ||
                r == "announcement" || r == "documentation",
            "Unknown source role");
    roles.insert(r);
  }
  require(cat ? roles.count("catalog")
              : (roles.count("eop") && roles.count("leaps") &&
                 roles.count("announcement")),
          "Missing source roles");
}
static bool assumed(const J &j) {
  auto s = string(j);
  require(s == "measured" || s == "assumed", "Invalid parameter status");
  return s == "assumed";
}
static Error resource_error(const Failure &f) {
  auto e = f.error;
  if (e.code == ErrorCode::InvalidInput)
    e.code = ErrorCode::InvalidResource;
  return e;
}
Outcome<Catalog> Catalog::load(std::span<const std::byte> b) {
  try {
    auto j = parse_json(b, 16777216, 8, 500000);
    keys(j, {"schema", "provenance", "valid_from_utc_s", "valid_until_utc_s",
             "namespace", "frame", "epoch_jd_tdb", "coverage", "stars"});
    require(string(j.at("schema")) == "astroattitude.catalog/1",
            "Unsupported catalog schema", ErrorCode::UnsupportedVersion);
    provenance(j.at("provenance"), true);
    auto p = std::make_shared<Impl>();
    p->id = digest(b);
    p->bytes = b.size();
    p->ns = string(j.at("namespace"));
    p->from = integer(j.at("valid_from_utc_s"));
    p->until = integer(j.at("valid_until_utc_s"));
    require(p->from < p->until, "Empty catalog validity");
    require(string(j.at("frame")) == "ICRS" &&
                number(j.at("epoch_jd_tdb")) == 2451545.,
            "Unsupported catalog frame or epoch");
    auto &cov = j.at("coverage");
    keys(cov, {"kind", "min_dec_rad", "max_dec_rad", "limiting_magnitude"});
    require(string(cov.at("kind")) == "declination_band", "Invalid coverage");
    double low = number(cov.at("min_dec_rad")),
           high = number(cov.at("max_dec_rad")),
           mag = number(cov.at("limiting_magnitude"));
    require(low >= -pi / 2 && high <= pi / 2 && low < high,
            "Invalid coverage domain");
    auto &a = j.at("stars");
    require(a.is_array() && a.size() >= 6 && a.size() <= 10000,
            "Catalog count outside domain");
    std::string last;
    for (auto &s : a) {
      keys(s, {"id", "ra_rad", "dec_rad", "pm_ra_rad_per_jyear",
               "pm_dec_rad_per_jyear", "parallax_arcsec",
               "radial_velocity_km_s", "magnitude", "motion_status",
               "parallax_status", "radial_velocity_status"});
      Star v;
      v.id = string(s.at("id"), 64);
      require(v.id > last && v.id.find(':') == std::string::npos,
              "Invalid/sorted duplicate star ID");
      last = v.id;
      v.ra = number(s.at("ra_rad"));
      v.dec = number(s.at("dec_rad"));
      v.pmr = number(s.at("pm_ra_rad_per_jyear"));
      v.pmd = number(s.at("pm_dec_rad_per_jyear"));
      v.px = number(s.at("parallax_arcsec"));
      v.rv = number(s.at("radial_velocity_km_s"));
      v.mag = number(s.at("magnitude"));
      require(v.ra >= 0 && v.ra < 2 * pi && v.dec > -pi / 2 && v.dec < pi / 2 &&
                  v.dec >= low && v.dec <= high && std::abs(v.pmr) <= .1 &&
                  std::abs(v.pmd) <= .1 && v.px >= 0 && v.px <= 10 &&
                  std::abs(v.rv) <= 1000 && v.mag >= -30 && v.mag <= 40 &&
                  v.mag <= mag,
              "Star outside supported domain");
      v.motion = assumed(s.at("motion_status"));
      v.parallax = assumed(s.at("parallax_status"));
      v.radial = assumed(s.at("radial_velocity_status"));
      v.icrs = {cos(v.dec) * cos(v.ra), cos(v.dec) * sin(v.ra), sin(v.dec)};
      p->stars.push_back(v);
    }
    return Catalog(p);
  } catch (const Failure &f) {
    return resource_error(f);
  } catch (const std::bad_alloc &) {
    return Error{
        ErrorCode::ResourceLimitExceeded, "Catalog allocation failed", {}};
  } catch (const std::exception &) {
    return Error{ErrorCode::InvalidResource, "Malformed catalog", {}};
  }
}
std::string_view Catalog::content_id() const { return impl_->id; }
Outcome<AstrometryData> AstrometryData::load(std::span<const std::byte> b) {
  try {
    auto j = parse_json(b, 4194304, 8, 100000);
    keys(j, {"schema", "provenance", "valid_from_utc_s", "valid_until_utc_s",
             "model_id", "leap_valid_until_utc_s", "leaps", "eop"});
    require(string(j.at("schema")) == "astroattitude.astrometry/1",
            "Unsupported astrometry schema", ErrorCode::UnsupportedVersion);
    require(string(j.at("model_id")) == "erfa-2.0.1-iau2006a-tt/1",
            "Unsupported astrometry model", ErrorCode::UnsupportedVersion);
    provenance(j.at("provenance"), false);
    auto p = std::make_shared<Impl>();
    p->id = digest(b);
    p->bytes = b.size();
    p->from = integer(j.at("valid_from_utc_s"));
    p->until = integer(j.at("valid_until_utc_s"));
    p->leap_until = integer(j.at("leap_valid_until_utc_s"));
    require(p->from >= 946684800 && p->until <= 2524608000LL &&
                p->from < p->until && p->leap_until >= p->until,
            "Astrometry validity outside domain");
    auto &a = j.at("leaps");
    require(a.is_array() && !a.empty() && a.size() <= 256,
            "Invalid leap count");
    for (auto &l : a) {
      keys(l, {"effective_utc_s", "tai_minus_utc_s"});
      auto t = integer(l.at("effective_utc_s")),
           o = integer(l.at("tai_minus_utc_s"));
      require(t % 86400 == 0 && o >= 0 && o <= 255, "Invalid leap entry");
      if (!p->leaps.empty())
        require(t > p->leaps.back().utc &&
                    std::abs(o - p->leaps.back().offset) == 1,
                "Invalid leap sequence");
      p->leaps.push_back({t, int(o)});
    }
    require(p->leaps.front().utc <= p->from, "No initial leap offset");
    auto &e = j.at("eop");
    require(e.is_array() && e.size() >= 2 && e.size() <= 20000,
            "Invalid EOP count");
    for (auto &v : e) {
      keys(v, {"utc_s", "ut1_minus_utc_s", "xp_rad", "yp_rad", "status"});
      Eop r{integer(v.at("utc_s")), number(v.at("ut1_minus_utc_s")),
            number(v.at("xp_rad")), number(v.at("yp_rad")), false};
      auto s = string(v.at("status"));
      require(s == "observed" || s == "predicted", "Invalid EOP status");
      r.predicted = s == "predicted";
      require(std::abs(r.dut) <= 1 && std::abs(r.xp) <= 1e-4 &&
                  std::abs(r.yp) <= 1e-4,
              "EOP outside domain");
      if (!p->eop.empty())
        require(r.utc > p->eop.back().utc &&
                    static_cast<long double>(r.utc) - p->eop.back().utc <=
                        172800,
                "EOP gap/order invalid");
      p->eop.push_back(r);
    }
    require(p->eop.front().utc <= p->from && p->eop.back().utc >= p->until,
            "EOP does not bracket package");
    return AstrometryData(p);
  } catch (const Failure &f) {
    return resource_error(f);
  } catch (const std::bad_alloc &) {
    return Error{
        ErrorCode::ResourceLimitExceeded, "Astrometry allocation failed", {}};
  } catch (const std::exception &) {
    return Error{ErrorCode::InvalidResource, "Malformed astrometry", {}};
  }
}
std::string_view AstrometryData::content_id() const { return impl_->id; }
} // namespace astroattitude
