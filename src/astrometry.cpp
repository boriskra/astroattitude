#include "internal.hpp"
namespace astroattitude {
void validate_identity(const ObservationIdentity &i) {
  identifier(i.observation_id);
  identifier(i.camera_configuration_id);
  identifier(i.calibration_revision);
  identifier(i.timing_provenance_id);
  require(i.exposure.midpoint.nanoseconds < 1000000000,
          "Nanoseconds outside domain");
  require(finite(i.exposure.duration_s) && i.exposure.duration_s > 0 &&
              i.exposure.duration_s <= 120,
          "Unsupported exposure duration", ErrorCode::UnsupportedObservation);
  if (i.exposure.midpoint_sigma_s)
    require(finite(*i.exposure.midpoint_sigma_s) &&
                *i.exposure.midpoint_sigma_s >= 0,
            "Invalid timing sigma");
  require(i.temporal_model == TemporalModel::StationarySingleExposure ||
              i.temporal_model == TemporalModel::AlignedEffectiveExposure,
          "Unsupported temporal model", ErrorCode::UnsupportedObservation);
}
static int offset(const AstrometryData::Impl &d, long double t) {
  auto it =
      std::upper_bound(d.leaps.begin(), d.leaps.end(), t,
                       [](long double t, const Leap &l) { return t < l.utc; });
  require(it != d.leaps.begin() && t < d.leap_until, "Leap data unavailable",
          ErrorCode::AstrometryDataUnavailable);
  return std::prev(it)->offset;
}
References references(const Catalog::Impl &c, const AstrometryData::Impl &d,
                      const ObservationIdentity &i, const EarthContext &ctx,
                      Cancellation cancel) {
  validate_identity(i);
  auto p = ctx.observer;
  require(finite(p.latitude_rad) && std::abs(p.latitude_rad) <= pi / 2 &&
              finite(p.longitude_rad) && p.longitude_rad >= -pi &&
              p.longitude_rad < pi && finite(p.ellipsoid_height_m) &&
              p.ellipsoid_height_m >= -500 && p.ellipsoid_height_m <= 10000,
          "Invalid observer");
  require(ctx.eop_policy == EopPolicy::ObservedOnly ||
              ctx.eop_policy == EopPolicy::AllowPredicted,
          "Invalid EOP policy");
  const int64_t whole = i.exposure.midpoint.unix_seconds;
  const double fraction = i.exposure.midpoint.nanoseconds * 1e-9;
  const double radius = i.exposure.duration_s / 2 + 3 * i.exposure.midpoint_sigma_s.value_or(0);
  // Never add nanoseconds to a large POSIX timestamp: long double is binary64
  // on Apple arm64. Compare small differences from integer resource boundaries.
  auto covers = [&](int64_t from, int64_t until) {
    return whole >= from && whole < until &&
      (double(whole)-double(from))+fraction >= radius &&
      (double(until)-double(whole))-fraction > radius;
  };
  require(finite(radius) && covers(d.from,d.until) && covers(c.from,c.until),
    "Exposure outside resource validity",ErrorCode::AstrometryDataUnavailable);
  for(size_t n=1;n<d.leaps.size();++n){double distance=(double(whole)-double(d.leaps[n].utc))+fraction;
    require(distance+radius < -2 || distance-radius > 2,"Exposure touches leap exclusion window",ErrorCode::UnsupportedObservation);}
  bool predicted=false;
  auto interp = [&](double relative) {
    double shift=std::floor(relative);int64_t second=whole+int64_t(shift);double subsecond=relative-shift;
    auto hi=std::lower_bound(d.eop.begin(),d.eop.end(),second,[](const Eop&e,int64_t q){return e.utc<q;});
    if(hi!=d.eop.end()&&hi->utc==second&&subsecond==0){predicted|=hi->predicted;return *hi;}
    if(hi!=d.eop.end()&&hi->utc==second)++hi;
    require(hi!=d.eop.end()&&hi!=d.eop.begin(),"EOP bracket unavailable",ErrorCode::AstrometryDataUnavailable);
    auto lo=std::prev(hi);predicted|=lo->predicted||hi->predicted;
    int at=offset(d,second),low=offset(d,lo->utc),high=offset(d,hi->utc);
    double u=((double(second)-double(lo->utc))+subsecond+at-low)/((double(hi->utc)-double(lo->utc))+high-low);
    return Eop{second,(1-u)*(lo->dut-low)+u*(hi->dut-high)+at,(1-u)*lo->xp+u*hi->xp,(1-u)*lo->yp+u*hi->yp,predicted};
  };
  interp(fraction-radius);interp(fraction+radius);
  for(auto&sample:d.eop){double relative=double(sample.utc)-double(whole);if(relative>=fraction-radius&&relative<=fraction+radius)predicted|=sample.predicted;}
  auto e=interp(fraction);
  require(ctx.eop_policy == EopPolicy::AllowPredicted || !predicted,
          "Predicted EOP disallowed", ErrorCode::AstrometryDataUnavailable);
  double ra = 0, rb = 0;
  if (auto a = std::get_if<Atmosphere>(&ctx.refraction)) {
    require(finite(a->pressure_pa) && a->pressure_pa >= 10000 &&
                a->pressure_pa <= 110000 && finite(a->temperature_kelvin) &&
                a->temperature_kelvin >= 180 && a->temperature_kelvin <= 330 &&
                finite(a->relative_humidity) && a->relative_humidity >= 0 &&
                a->relative_humidity <= 1 &&
                finite(a->effective_wavelength_m) &&
                a->effective_wavelength_m >= 350e-9 &&
                a->effective_wavelength_m <= 1000e-9,
            "Invalid atmosphere");
    eraRefco(a->pressure_pa / 100, a->temperature_kelvin - 273.15,
             a->relative_humidity, a->effective_wavelength_m * 1e6, &ra, &rb);
  }
  // Split dates preserve fractional seconds; UTC is never passed to ERFA
  // globals.
  double day = std::floor(double(i.exposure.midpoint.unix_seconds) / 86400),
         jd = 2440587.5 + day,
         seconds = double(i.exposure.midpoint.unix_seconds) - day * 86400 +
                   i.exposure.midpoint.nanoseconds * 1e-9;
  double tt = (seconds + offset(d, whole) + 32.184) / 86400,
         ut = (seconds + e.dut) / 86400;
  double hp[2][3], bp[2][3], bpn[3][3], x, y;
  require(eraEpv00(jd, tt, hp, bp) == 0, "Ephemeris outside domain",
          ErrorCode::AstrometryDataUnavailable);
  eraPnm06a(jd, tt, bpn);
  eraBpn2xy(bpn, &x, &y);
  double s = eraS06(jd, tt, x, y), theta = eraEra00(jd, ut),
         sp = eraSp00(jd, tt);
  eraASTROM a;
  eraApco(jd, tt, bp, hp[0], x, y, s, theta, p.longitude_rad, p.latitude_rad,
          p.ellipsoid_height_m, e.xp, e.yp, sp, ra, rb, &a);
  auto vacuum = a;
  vacuum.refa = vacuum.refb = 0;
  References r;
  r.predicted = predicted;
  r.rays.reserve(c.stars.size());
  r.visible.reserve(c.stars.size());
  for (auto &star : c.stars) {
    if (cancel.requested && cancel.requested->load(std::memory_order_relaxed))
      fail(ErrorCode::Cancelled, "Solve cancelled during reference conversion");
    double ri, di, az, z, ha, dec, obsra, z0;
    eraAtciq(star.ra, star.dec, star.pmr, star.pmd, star.px, star.rv, &a, &ri,
             &di);
    eraAtioq(ri, di, &vacuum, &az, &z0, &ha, &dec, &obsra);
    eraAtioq(ri, di, &a, &az, &z, &ha, &dec, &obsra);
    V v(sin(z) * sin(az), sin(z) * cos(az), cos(z));
    require(v.allFinite(), "Nonfinite astrometric direction",
            ErrorCode::InternalError);
    r.rays.push_back(v);
    r.visible.push_back(z0 <= pi / 2 - 20 * pi / 180);
  }
  return r;
}
} // namespace astroattitude
