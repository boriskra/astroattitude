#include <astroattitude/astroattitude.hpp>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace astroattitude;
std::string read(const char *path) {
  std::ifstream f(path);
  return {std::istreambuf_iterator<char>(f), {}};
}
std::span<const std::byte> bytes(const std::string &s) {
  return {reinterpret_cast<const std::byte *>(s.data()), s.size()};
}
int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr
        << "Usage: aa_cpp_example catalog.json astrometry.json rays.json\n";
    return 2;
  }
  auto catalog_bytes = read(argv[1]), astrometry_bytes = read(argv[2]);
  auto catalog = Catalog::load(bytes(catalog_bytes));
  auto astrometry = AstrometryData::load(bytes(astrometry_bytes));
  if (std::holds_alternative<Error>(catalog) ||
      std::holds_alternative<Error>(astrometry))
    return 1;
  auto created = Engine::create(std::get<Catalog>(catalog),
                                std::get<AstrometryData>(astrometry));
  if (std::holds_alternative<Error>(created))
    return 1;
  auto request = nlohmann::json::parse(read(argv[3]));
  std::vector<RayStar> stars;
  for (auto &s : request["observation"]["stars"]) {
    auto &v = s["direction_camera"];
    stars.push_back({std::stoull(s["id"].get<std::string>()),
                     {v["x"], v["y"], v["z"]},
                     std::nullopt});
  }
  // Explicit synthetic-only context, matching the generated test fixture.
  ObservationIdentity identity{"example",
                               "camera",
                               "cal-1",
                               {{1577836800, 123456789}, .01, .001},
                               TemporalModel::StationarySingleExposure,
                               "synthetic-clock"};
  EarthContext context{{0, 0, 0}, NoRefraction{}, EopPolicy::ObservedOnly};
  SolveOptions options{{6, 1e-4, 1e-5, std::nullopt},
                       {50000, 2000000, 67108864}};
  auto result = std::get<Engine>(created).solve(RayObservation{identity, stars},
                                                context, options);
  if (auto error = std::get_if<Error>(&result)) {
    std::cerr << error->detail << '\n';
    return 1;
  }
  const auto &solution = std::get<AttitudeSolution>(result);
  std::cout << solution.matches.size()
            << " synthetic matches; RMS=" << solution.fit_rms_rad << " rad\n";
}
