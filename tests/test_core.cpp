#include "astroattitude/astroattitude.h"
#include "internal.hpp"
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <random>
using namespace astroattitude;
static int checks = 0;
void check(bool ok, const char *message) {
  ++checks;
  if (!ok)
    throw std::runtime_error(message);
}
std::span<const std::byte> bytes(const std::string &s) {
  return {reinterpret_cast<const std::byte *>(s.data()), s.size()};
}
template <class T> T value(Outcome<T> o) {
  if (auto e = std::get_if<Error>(&o))
    throw std::runtime_error(e->detail);
  return std::get<T>(o);
}
template <class T> void error(Outcome<T> o, ErrorCode code) {
  check(std::holds_alternative<Error>(o) && std::get<Error>(o).code == code,
        "Wrong error");
}
J provenance(std::vector<std::string> roles) {
  J sources = J::array();
  std::sort(roles.begin(), roles.end());
  for (auto r : roles)
    sources.push_back(
        {{"source_id", r},
         {"source_version", "synthetic-1"},
         {"source_uri", "urn:astroattitude:synthetic"},
         {"source_sha256", std::string(64, '0')},
         {"license_id", "test-only"},
         {"attribution", "Synthetic test fixture; no real sky data"},
         {"role", r}});
  return {{"sources", sources},
          {"conversion_id", "synthetic-1"},
          {"conversion_manifest_sha256", std::string(64, '0')}};
}
int main(int argc, char **argv) {
  try {
    const int64_t t = 1577836800;
    J catalog = {{"schema", "astroattitude.catalog/1"},
                 {"provenance", provenance({"catalog"})},
                 {"valid_from_utc_s", t - 86400},
                 {"valid_until_utc_s", t + 86400},
                 {"namespace", "synthetic"},
                 {"frame", "ICRS"},
                 {"epoch_jd_tdb", 2451545.},
                 {"coverage",
                  {{"kind", "declination_band"},
                   {"min_dec_rad", -.5},
                   {"max_dec_rad", .5},
                   {"limiting_magnitude", 8}}},
                 {"stars", J::array()}};
    std::mt19937_64 random(59382);
    std::uniform_real_distribution<double> uniform(-.16, .16);
    for (int i = 0; i < 8; i++)
      catalog["stars"].push_back({{"id", std::to_string(i + 1)},
                                  {"ra_rad", 1.747 + uniform(random)},
                                  {"dec_rad", uniform(random)},
                                  {"pm_ra_rad_per_jyear", 0},
                                  {"pm_dec_rad_per_jyear", 0},
                                  {"parallax_arcsec", 0},
                                  {"radial_velocity_km_s", 0},
                                  {"magnitude", 4},
                                  {"motion_status", "assumed"},
                                  {"parallax_status", "assumed"},
                                  {"radial_velocity_status", "assumed"}});
    J data = {{"schema", "astroattitude.astrometry/1"},
              {"provenance", provenance({"eop", "leaps", "announcement"})},
              {"valid_from_utc_s", t - 86400},
              {"valid_until_utc_s", t + 86400},
              {"model_id", "erfa-2.0.1-iau2006a-tt/1"},
              {"leap_valid_until_utc_s", t + 172800},
              {"leaps", J::array({{{"effective_utc_s", t - 86400},
                                   {"tai_minus_utc_s", 37}}})},
              {"eop", J::array()}};
    for (int i = -1; i <= 1; i++)
      data["eop"].push_back({{"utc_s", t + i * 86400},
                             {"ut1_minus_utc_s", .1},
                             {"xp_rad", 1e-7},
                             {"yp_rad", 2e-7},
                             {"status", "observed"}});
    auto cs = catalog.dump(), ds = data.dump();
    auto cat = value(Catalog::load(bytes(cs)));
    auto ast = value(AstrometryData::load(bytes(ds)));
    auto engine = value(Engine::create(cat, ast));
    check(cat.content_id() == digest(bytes(cs)), "Content hash");
    check(cat.content_id() !=
              value(Catalog::load(bytes(cs + " "))).content_id(),
          "Exact byte identity");
    error(Catalog::load(bytes("{\"a\":1,\"a\":2}")),
          ErrorCode::InvalidResource);
    auto invalid = catalog;
    invalid["schema"] = "future";
    error(Catalog::load(bytes(invalid.dump())), ErrorCode::UnsupportedVersion);
    ObservationIdentity id{"fixture",
                           "camera",
                           "cal-1",
                           {{t, 123456789}, .01, .001},
                           TemporalModel::StationarySingleExposure,
                           "clock-1"};
    EarthContext ctx{{0, 0, 0}, NoRefraction{}, EopPolicy::ObservedOnly};
    SolveOptions opt{{6, 1e-4, 1e-5, std::nullopt}, {50000, 2000000, 67108864}};
    auto refs =
        references(NativeAccess::get(cat), NativeAccess::get(ast), id, ctx);
    // High-level ERFA integration control uses its independent UTC conversion
    // route.
    for (size_t i = 0; i < 8; i++) {
      auto &s = catalog["stars"][i];
      double az, z, ha, dec, ra, eo;
      check(eraAtco13(s["ra_rad"], s["dec_rad"], 0, 0, 0, 0, 2458849.5,
                      .123456789 / 86400, .1, 0, 0, 0, 1e-7, 2e-7, 0, 10, 0,
                      .55, &az, &z, &ha, &dec, &ra, &eo) == 0,
            "ERFA status");
      V reference(sin(z) * sin(az), sin(z) * cos(az), cos(z));
      check(angle(reference, refs.rays[i]) < 1e-10, "ERFA time/frame parity");
    }
    M truth = Eigen::AngleAxisd(.02, V::UnitZ()).toRotationMatrix() *
              Eigen::AngleAxisd(.03, V::UnitX()).toRotationMatrix();
    std::vector<RayStar> stars;
    J wireStars = J::array();
    for (size_t i = 0; i < 8; i++) {
      V v = truth.transpose() * refs.rays[i];
      M covariance = (M::Identity() - v * v.transpose()) * 1e-12;
      uint64_t identifier = i == 7 ? UINT64_MAX : i;
      stars.push_back({identifier, {v.x(), v.y(), v.z()}, mat(covariance)});
      wireStars.push_back(
          {{"id", std::to_string(identifier)},
           {"direction_camera", {{"x", v.x()}, {"y", v.y()}, {"z", v.z()}}},
           {"direction_covariance_rad2",
            {{"row_major", mat(covariance).row_major}}}});
    }
    auto solution = value(engine.solve(RayObservation{id, stars}, ctx, opt));
    check((matrix(solution.R_enu_from_camera.matrix) - truth).norm() < 1e-8,
          "Known rotation");
    check(solution.matches.size() == 8 &&
              solution.matches.back().detection_id == UINT64_MAX,
          "ID boundaries");
    check(solution.conditional_uncertainty.has_value(), "Covariance supplied");
    J identity = {
        {"observation_id", "fixture"},
        {"camera_configuration_id", "camera"},
        {"calibration_revision", "cal-1"},
        {"exposure",
         {{"midpoint",
           {{"unix_seconds", std::to_string(t)}, {"nanoseconds", 123456789}}},
          {"duration_s", .01},
          {"midpoint_sigma_s", .001}}},
        {"temporal_model", "stationary_single_exposure"},
        {"timing_provenance_id", "clock-1"}};
    J request = {
        {"schema", "astroattitude.solve/1"},
        {"kind", "rays"},
        {"observation", {{"identity", identity}, {"stars", wireStars}}},
        {"calibration", nullptr},
        {"context",
         {{"observer",
           {{"latitude_rad", 0},
            {"longitude_rad", 0},
            {"ellipsoid_height_m", 0}}},
          {"refraction", {{"kind", "disabled"}}},
          {"eop_policy", "observed_only"}}},
        {"options",
         {{"acceptance",
           {{"minimum_inliers", 6},
            {"maximum_match_error_rad", 1e-4},
            {"maximum_fit_rms_rad", 1e-5},
            {"maximum_conditional_sigma_rad", nullptr}}},
          {"limits",
           {{"maximum_hypotheses", 50000},
            {"maximum_candidate_comparisons", 2000000},
            {"maximum_workspace_bytes", 67108864}}}}}};
    aa_engine *handle = nullptr;
    aa_message *message = nullptr;
    check(aa_engine_create_v1((uint8_t *)cs.data(), cs.size(),
                              (uint8_t *)ds.data(), ds.size(), &handle,
                              &message) == 0,
          "C create");
    auto req = request.dump();
    check(aa_solve_v1(handle, (uint8_t *)req.data(), req.size(), nullptr,
                      &message) == 0,
          "C solve");
    uint64_t size;
    auto raw = aa_message_bytes_v1(message, &size);
    auto response = J::parse(raw, raw + size);
    check(response["solution"]["matches"][7]["detection_id"] ==
              "18446744073709551615",
          "C uint64");
    aa_message_destroy_v1(message);
    aa_engine_destroy_v1(handle);
    std::filesystem::path out = argc > 1 ? argv[1] : "fixtures";
    std::filesystem::create_directories(out);
    for (auto [name, j] :
         std::vector<std::pair<std::string, J>>{{"catalog.json", catalog},
                                                {"astrometry.json", data},
                                                {"rays.json", request},
                                                {"expected.json", response}})
      std::ofstream(out / name) << j.dump();
    auto shuffled = stars;
    std::reverse(shuffled.begin(), shuffled.end());
    check(value(engine.solve(RayObservation{id, shuffled}, ctx, opt))
                  .matches[0]
                  .detection_id == 0,
          "Stable detection order");
    auto bad = stars;
    bad[1].id = 0;
    error(engine.solve(RayObservation{id, bad}, ctx, opt),
          ErrorCode::InvalidInput);
    bad = stars;
    bad[1].direction_covariance_rad2.reset();
    error(engine.solve(RayObservation{id, bad}, ctx, opt),
          ErrorCode::InvalidInput);
    error(engine.solve(RayObservation{id, std::span(stars).first(5)}, ctx, opt),
          ErrorCode::InsufficientObservations);
    auto time = id;
    time.exposure.duration_s = 121;
    error(engine.solve(RayObservation{time, stars}, ctx, opt),
          ErrorCode::UnsupportedObservation);
    time = id;
    time.exposure.midpoint.unix_seconds = t + 86400;
    error(engine.solve(RayObservation{time, stars}, ctx, opt),
          ErrorCode::AstrometryDataUnavailable);
    auto small = opt;
    small.limits.maximum_candidate_comparisons = 100;
    error(engine.solve(RayObservation{id, stars}, ctx, small),
          ErrorCode::ResourceLimitExceeded);
    small = opt;
    small.limits.maximum_workspace_bytes = 1;
    error(engine.solve(RayObservation{id, stars}, ctx, small),
          ErrorCode::ResourceLimitExceeded);
    std::atomic_bool cancel = true;
    error(engine.solve(RayObservation{id, stars}, ctx, opt, {&cancel}),
          ErrorCode::Cancelled);
    auto precision = opt;
    precision.acceptance.maximum_conditional_sigma_rad = 1e-12;
    error(engine.solve(RayObservation{id, stars}, ctx, precision),
          ErrorCode::ConditionalPrecisionNotMet);
    bad = stars;
    for (auto &s : bad)
      s.direction_covariance_rad2.reset();
    error(engine.solve(RayObservation{id, bad}, ctx, precision),
          ErrorCode::UncertaintyUnavailable);
    check(!value(engine.solve(RayObservation{id, bad}, ctx, opt))
               .conditional_uncertainty,
          "Absent covariance");
    auto task = [&] {
      return value(engine.solve(RayObservation{id, stars}, ctx, opt))
          .fit_rms_rad;
    };
    auto parallel = std::async(std::launch::async, task);
    check(task() == parallel.get(), "Concurrent deterministic solve");
    // Independently coded Brown forward projection, including asymmetric
    // tangential terms.
    CameraCalibration calibration{
        "cal-1",
        "camera",
        2048,
        2048,
        {1200, 1190, 1000, 1010, .03, -.005, .001, .002, -.003},
        {0, 0, 2047, 2047}};
    std::vector<PixelStar> pixels;
    J pw = J::array();
    for (auto &s : stars) {
      double x = s.direction_camera.x / s.direction_camera.z,
             y = s.direction_camera.y / s.direction_camera.z, r = x * x + y * y,
             a = 1 + .03 * r - .005 * r * r + .001 * r * r * r;
      Vec2 p{1200 * (x * a + .004 * x * y - .003 * (r + 2 * x * x)) + 1000,
             1190 * (y * a + .002 * (r + 2 * y * y) - .006 * x * y) + 1010};
      pixels.push_back({s.id, p, Mat2{{.000001, 0, 0, .000001}}, std::nullopt});
      pw.push_back({{"id", std::to_string(s.id)},
                    {"pixel", {{"x", p.x}, {"y", p.y}}},
                    {"centroid_covariance_px2",
                     {{"row_major", {.000001, 0, 0, .000001}}}},
                    {"flux", nullptr}});
    }
    auto ps = value(engine.solve(PixelObservation{id, 2048, 2048, pixels},
                                 calibration, ctx, opt));
    check((matrix(ps.R_enu_from_camera.matrix) - truth).norm() < 1e-8,
          "Independent Brown control");
    auto wrong = calibration;
    wrong.revision = "other";
    error(
        engine.solve(PixelObservation{id, 2048, 2048, pixels}, wrong, ctx, opt),
        ErrorCode::CalibrationMismatch);
    auto off = pixels;
    off[0].pixel.x = -1;
    error(engine.solve(PixelObservation{id, 2048, 2048, off}, calibration, ctx,
                       opt),
          ErrorCode::OutsideCalibrationDomain);
    auto folded = calibration;
    folded.model.k1 = -1000;
    error(engine.solve(PixelObservation{id, 2048, 2048, pixels}, folded, ctx,
                       opt),
          ErrorCode::OutsideCalibrationDomain);
    request["kind"] = "pixels";
    request["observation"]["width"] = 2048;
    request["observation"]["height"] = 2048;
    request["observation"]["stars"] = pw;
    request["calibration"] = {
        {"revision", "cal-1"},
        {"camera_configuration_id", "camera"},
        {"width", 2048},
        {"height", 2048},
        {"model",
         {{"fx_px", 1200},
          {"fy_px", 1190},
          {"cx_px", 1000},
          {"cy_px", 1010},
          {"k1", .03},
          {"k2", -.005},
          {"k3", .001},
          {"p1", .002},
          {"p2", -.003}}},
        {"valid_region",
         {{"min_x", 0}, {"min_y", 0}, {"max_x", 2047}, {"max_y", 2047}}}};
    std::ofstream(out / "pixels.json") << request.dump();

    auto predicted = data;
    predicted["eop"][2]["status"] = "predicted";
    auto pe = value(Engine::create(
        cat, value(AstrometryData::load(bytes(predicted.dump())))));
    error(pe.solve(RayObservation{id, stars}, ctx, opt),
          ErrorCode::AstrometryDataUnavailable);
    auto tiny = id;
    tiny.exposure = {{t, 1}, 1e-10, 0};
    error(pe.solve(RayObservation{tiny, stars}, ctx, opt),
          ErrorCode::AstrometryDataUnavailable);
    auto before = id;
    before.exposure = {{t - 86400, 0}, 1e-10, 0};
    error(engine.solve(RayObservation{before, stars}, ctx, opt),
          ErrorCode::AstrometryDataUnavailable);
    auto permissive = ctx;
    permissive.eop_policy = EopPolicy::AllowPredicted;
    check(value(pe.solve(RayObservation{id, stars}, permissive, opt))
                  .models.earth_orientation_status == "predicted",
          "Full exposure prediction status");
    auto transition = data;
    transition["leaps"].push_back(
        {{"effective_utc_s", t}, {"tai_minus_utc_s", 38}});
    auto le = value(Engine::create(
        cat, value(AstrometryData::load(bytes(transition.dump())))));
    error(le.solve(RayObservation{id, stars}, ctx, opt),
          ErrorCode::UnsupportedObservation);
    auto repeated = stars;
    for (auto &s : repeated)
      s.direction_camera = stars[0].direction_camera;
    for (auto &s : repeated)
      s.direction_covariance_rad2.reset();
    error(engine.solve(RayObservation{id, repeated}, ctx, opt),
          ErrorCode::UnobservableGeometry);
    auto residualStars = stars;
    V shifted = vec(residualStars[0].direction_camera);
    shifted = Eigen::AngleAxisd(1e-6, V::UnitX()) * shifted;
    residualStars[0].direction_camera = {shifted.x(), shifted.y(), shifted.z()};
    for (auto &s : residualStars)
      s.direction_covariance_rad2.reset();
    auto tight = opt;
    tight.acceptance.maximum_fit_rms_rad = 1e-10;
    error(engine.solve(RayObservation{id, residualStars}, ctx, tight),
          ErrorCode::ResidualLimitExceeded);
    auto duplicateCatalog = catalog;
    for (auto star : catalog["stars"]) {
      star["id"] = "a" + star["id"].get<std::string>();
      duplicateCatalog["stars"].push_back(star);
    }
    auto ae = value(Engine::create(
        value(Catalog::load(bytes(duplicateCatalog.dump()))), ast));
    error(ae.solve(RayObservation{id, stars}, ctx, opt),
          ErrorCode::AmbiguousMatch);
    auto noDirections = stars;
    for (size_t k = 0; k < noDirections.size(); k++) {
      V v(1, double(k + 1) * .3, double(k * k + 1) * .1);
      v.normalize();
      noDirections[k].direction_camera = {v.x(), v.y(), v.z()};
      noDirections[k].direction_covariance_rad2.reset();
    }
    error(engine.solve(RayObservation{id, noDirections}, ctx, opt),
          ErrorCode::NoMatch);
    auto missing = catalog;
    missing["stars"][0].erase("parallax_status");
    error(Catalog::load(bytes(missing.dump())), ErrorCode::InvalidResource);
    auto gaps = data;
    gaps["eop"][1]["utc_s"] = t - 86400;
    error(AstrometryData::load(bytes(gaps.dump())), ErrorCode::InvalidResource);
    // Budget one comparison short of the successful complete search must
    // abstain, even though earlier seed branches already found the correct
    // candidate.
    small = opt;
    small.limits.maximum_candidate_comparisons =
        solution.search.candidate_comparisons - 1;
    error(engine.solve(RayObservation{id, stars}, ctx, small),
          ErrorCode::ResourceLimitExceeded);
#ifdef AA_TEST_HOOKS
    std::atomic_bool candidate_seen = false, release_candidate = false,
                     late_cancel = false;
    testing::candidate_seen = &candidate_seen;
    testing::release_candidate = &release_candidate;
    auto late = std::async(std::launch::async, [&] {
      return engine.solve(RayObservation{id, stars}, ctx, opt, {&late_cancel});
    });
    while (!candidate_seen.load()) {
    };
    late_cancel = true;
    release_candidate = true;
    error(late.get(), ErrorCode::Cancelled);
    testing::candidate_seen = nullptr;
    testing::release_candidate = nullptr;
#endif
    // Independent published ERFA vector: Earth rotation angle at UT1 MJD 54388.
    check(std::abs(eraEra00(2400000.5, 54388.0) - 0.4022837240028158102) <
              1e-12,
          "Published ERA control");
    if (argc > 2 && std::string(argv[2]) == "--qualification") {
      std::normal_distribution<double> normal;
      size_t covered = 0;
      double worst = 0;
      for (int trial = 0; trial < 10000; trial++) {
        auto noisy = stars;
        for (auto &s : noisy) {
          V v = vec(s.direction_camera);
          V noise(normal(random), normal(random), normal(random));
          v = (v + (M::Identity() - v * v.transpose()) * noise * 1e-6)
                  .normalized();
          s.direction_camera = {v.x(), v.y(), v.z()};
          s.direction_covariance_rad2 =
              mat((M::Identity() - v * v.transpose()) * 1e-12);
        }
        auto candidate = engine.solve(RayObservation{id, noisy}, ctx, opt);
        if (auto e = std::get_if<Error>(&candidate)) {
          std::cerr << "trial=" << trial << " code=" << int(e->code) << "\n";
          for (size_t n = 0; n < stars.size(); n++)
            std::cerr << angle(vec(stars[n].direction_camera),
                               vec(noisy[n].direction_camera))
                      << " ";
          std::cerr << "\n";
        }
        auto r = value(std::move(candidate));
        Eigen::AngleAxisd rotation(matrix(r.R_enu_from_camera.matrix) *
                                   truth.transpose());
        V delta = rotation.angle() * rotation.axis();
        double statistic = delta.dot(
            matrix(r.conditional_uncertainty->covariance_enu_rad2).inverse() *
            delta);
        covered += statistic <= 7.814727903251179;
        worst = std::max(worst, rotation.angle());
      }
      check(covered >= 9300 && covered <= 9700,
            "95 percent covariance coverage");
      for (int trial = 0; trial < 30000; trial++) {
        auto negative = stars;
        for (auto &s : negative) {
          V v(normal(random), normal(random), normal(random));
          v.normalize();
          s.direction_camera = {v.x(), v.y(), v.z()};
          s.direction_covariance_rad2.reset();
        }
        if (trial % 4 == 1)
          negative[1].id = negative[0].id;
        if (trial % 4 == 2)
          for (size_t k = 1; k < negative.size(); k++)
            negative[k].direction_camera = negative[0].direction_camera;
        if (trial % 4 == 3)
          for (size_t k = 0; k < 3; k++)
            negative[k] = stars[k];
        auto r = engine.solve(RayObservation{id, negative}, ctx, opt);
        check(std::holds_alternative<Error>(r),
              "False positive negative observation");
      }
      std::cout << "S1 seed=59382 covariance=" << covered
                << "/10000 negatives=30000/30000 worst_rotation_rad=" << worst
                << "\n";
    }
    std::cout << checks << " checks passed\n";
  } catch (const std::exception &e) {
    std::cerr << "FAIL after " << checks << ": " << e.what() << "\n";
    return 1;
  }
}
