// Synthetic matching workload only. Never linked into a production target.
#include "astroattitude/astroattitude.h"
#include "internal.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
using namespace astroattitude;
extern std::atomic<size_t> aa_live_bytes, aa_peak_bytes;
using Clock = std::chrono::steady_clock;
template <class T> T take(Outcome<T> o) {
  if (auto e = std::get_if<Error>(&o))
    throw std::runtime_error(e->detail);
  return std::get<T>(o);
}
std::span<const std::byte> bytes(const std::string &s) {
  return {reinterpret_cast<const std::byte *>(s.data()), s.size()};
}
int main(int argc, char **argv) {
  try {
    if (argc < 2)
      throw std::runtime_error("Usage: aa_benchmark fixture-dir [samples]");
    std::string dir = argv[1];
    int samples = argc > 2 ? std::stoi(argv[2]) : 1000;
    int detections = argc > 3 ? std::stoi(argv[3]) : 32;
    if (detections != 8 && detections != 32 && detections != 128)
      throw std::runtime_error("Detections must be 8,32,128");
    J cj, dj;
    std::ifstream(dir + "/catalog.json") >> cj;
    std::ifstream(dir + "/astrometry.json") >> dj;
    cj["stars"] = J::array();
    cj["coverage"]["min_dec_rad"] = -pi / 2;
    cj["coverage"]["max_dec_rad"] = pi / 2;
    std::mt19937_64 random(924720);
    std::uniform_real_distribution<double> u(0, 1);
    for (int n = 0; n < 10000; n++)
      cj["stars"].push_back({{"id", std::to_string(10000 + n)},
                             {"ra_rad", 2 * pi * u(random)},
                             {"dec_rad", asin(2 * u(random) - 1)},
                             {"pm_ra_rad_per_jyear", 0},
                             {"pm_dec_rad_per_jyear", 0},
                             {"parallax_arcsec", 0},
                             {"radial_velocity_km_s", 0},
                             {"magnitude", 4},
                             {"motion_status", "assumed"},
                             {"parallax_status", "assumed"},
                             {"radial_velocity_status", "assumed"}});
    size_t base = aa_live_bytes.load();
    aa_peak_bytes = base;
    auto start = Clock::now();
    auto cat = take(Catalog::load(bytes(cj.dump())));
    auto data = take(AstrometryData::load(bytes(dj.dump())));
    auto engine = take(Engine::create(cat, data));
    double construction =
        std::chrono::duration<double>(Clock::now() - start).count();
    size_t construction_peak = aa_peak_bytes.load() - base;
    ObservationIdentity id{"benchmark",
                           "camera",
                           "cal",
                           {{1577836800, 0}, .01, .001},
                           TemporalModel::StationarySingleExposure,
                           "synthetic"};
    EarthContext ctx{{0, 0, 0}, NoRefraction{}, EopPolicy::ObservedOnly};
    SolveOptions opts{
        {6, .0005817764173314432, .0001454441043328608, std::nullopt},
        {20000, 1000000, 67108864}};
    if (detections == 128)
      opts.limits = {50000, 2000000, 67108864};
    auto refs =
        references(NativeAccess::get(cat), NativeAccess::get(data), id, ctx);
    std::vector<size_t> order(refs.rays.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
      return refs.rays[a].z() > refs.rays[b].z();
    });
    bool measure_abi = argc > 4 && std::string(argv[4]) == "--abi";
    aa_engine *raw_engine = nullptr;
    aa_message *creation_error = nullptr;
    auto cs = cj.dump(), ds = dj.dump();
    if (measure_abi &&
        aa_engine_create_v1(reinterpret_cast<const uint8_t *>(cs.data()),
                            cs.size(),
                            reinterpret_cast<const uint8_t *>(ds.data()),
                            ds.size(), &raw_engine, &creation_error))
      throw std::runtime_error("ABI benchmark creation failed");
    std::unique_ptr<aa_engine, decltype(&aa_engine_destroy_v1)> abi_engine(
        raw_engine, aa_engine_destroy_v1);
    J request;
    std::ifstream(dir + "/rays.json") >> request;
    request["observation"]["identity"] = {
        {"observation_id", "benchmark"},
        {"camera_configuration_id", "camera"},
        {"calibration_revision", "cal"},
        {"exposure",
         {{"midpoint", {{"unix_seconds", "1577836800"}, {"nanoseconds", 0}}},
          {"duration_s", .01},
          {"midpoint_sigma_s", .001}}},
        {"temporal_model", "stationary_single_exposure"},
        {"timing_provenance_id", "synthetic"}};
    request["options"]["acceptance"]["maximum_match_error_rad"] =
        opts.acceptance.maximum_match_error_rad;
    request["options"]["acceptance"]["maximum_fit_rms_rad"] =
        opts.acceptance.maximum_fit_rms_rad;
    request["options"]["limits"]["maximum_hypotheses"] =
        opts.limits.maximum_hypotheses;
    request["options"]["limits"]["maximum_candidate_comparisons"] =
        opts.limits.maximum_candidate_comparisons;
    std::vector<double> abi_times, overheads;
    std::vector<double> times;
    std::map<int, int> errors;
    int success = 0;
    size_t workspace_peak = 0;
    for (int trial = (samples < 1000 ? 0 : -100); trial < samples; trial++) {
      M r = Eigen::AngleAxisd((trial + 100) * .0001, V::UnitZ())
                .toRotationMatrix();
      std::vector<RayStar> stars;
      for (int k = 0; k < detections; k++) {
        V v = r.transpose() * refs.rays[order[k]];
        stars.push_back({uint64_t(k), {v.x(), v.y(), v.z()}, std::nullopt});
      }
      size_t baseline = aa_live_bytes.load();
      aa_peak_bytes = baseline;
      auto before = Clock::now();
      auto result = engine.solve(RayObservation{id, stars}, ctx, opts);
      double elapsed =
          std::chrono::duration<double, std::milli>(Clock::now() - before)
              .count();
      workspace_peak =
          std::max(workspace_peak, aa_peak_bytes.load() - baseline);
      if (measure_abi) {
        auto abi_start = Clock::now();
        request["observation"]["stars"] = J::array();
        for (auto &s : stars)
          request["observation"]["stars"].push_back(
              {{"id", std::to_string(s.id)},
               {"direction_camera",
                {{"x", s.direction_camera.x},
                 {"y", s.direction_camera.y},
                 {"z", s.direction_camera.z}}},
               {"direction_covariance_rad2", nullptr}});
        auto wire = request.dump();
        aa_message *output = nullptr;
        int status = aa_solve_v1(raw_engine,
                                 reinterpret_cast<const uint8_t *>(wire.data()),
                                 wire.size(), nullptr, &output);
        std::unique_ptr<aa_message, decltype(&aa_message_destroy_v1)> message(
            output, aa_message_destroy_v1);
        if (status != int(std::holds_alternative<Error>(result)))
          throw std::runtime_error("ABI/typed benchmark outcome mismatch");
        uint64_t n = 0;
        auto data = aa_message_bytes_v1(output, &n);
        auto response = J::parse(data, data + n);
        if (status == 1 && response["error"]["code"] != "ResourceLimitExceeded")
          throw std::runtime_error("ABI benchmark error mismatch");
        double total =
            std::chrono::duration<double, std::milli>(Clock::now() - abi_start)
                .count();
        if (trial >= 0) {
          abi_times.push_back(total);
          overheads.push_back(total - elapsed);
        }
      }
      if (trial >= 0) {
        times.push_back(elapsed);
        if (auto e = std::get_if<Error>(&result)) {
          errors[int(e->code)]++;
          if (trial == 0)
            std::cerr << e->detail << "\n";
        } else {
          success++;
          auto &s = std::get<AttitudeSolution>(result);
          if ((matrix(s.R_enu_from_camera.matrix) - r).norm() > 1e-8)
            throw std::runtime_error("Wrong accepted rotation");
        }
      }
    }
    std::sort(times.begin(), times.end());
    J result = {{"profile", detections == 32
                                ? "synthetic matching workload; no runtime guarantee"
                            : detections == 128
                                ? "128-detection stress"
                                : "8-detection positive cohort"},
                {"seed", 924720},
                {"catalog_stars", 10000},
                {"detections", detections},
                {"warmups", samples < 1000 ? 0 : 100},
                {"samples", samples},
                {"construction_s", construction},
                {"construction_peak_cpp_bytes", construction_peak},
                {"solve_peak_cpp_bytes", workspace_peak},
                {"solve_p95_ms", times.at(size_t(.95 * (samples - 1)))},
                {"solve_p99_ms", times.at(size_t(.99 * (samples - 1)))},
                {"accepted", success},
                {"errors", errors}};
    if (measure_abi) {
      std::sort(abi_times.begin(), abi_times.end());
      std::sort(overheads.begin(), overheads.end());
      result["abi_total_p95_ms"] = abi_times.at(size_t(.95 * (samples - 1)));
      result["paired_abi_overhead_p95_ms"] =
          overheads.at(size_t(.95 * (samples - 1)));
      result["abi_measurement"] = "paired encode/native-call/decode minus "
                                  "typed time; includes timing noise";
    }
    std::cout << result.dump(2) << "\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
