#include "astroattitude/astroattitude.h"
#include "internal.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
using namespace astroattitude;
extern thread_local long aa_fail_after;
extern thread_local bool aa_unexpected;
std::string read(const char *p) {
  std::ifstream in(p);
  return {std::istreambuf_iterator<char>(in), {}};
}
int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  auto c = read(argv[1]), d = read(argv[2]), r = read(argv[3]);
  for (long n = 0; n < 250; n++) {
    aa_engine *e = nullptr;
    aa_message *m = nullptr;
    aa_fail_after = n;
    int status = aa_engine_create_v1((uint8_t *)c.data(), c.size(),
                                     (uint8_t *)d.data(), d.size(), &e, &m);
    aa_fail_after = -1;
    if (status != 0 && status != 1 && status != 3)
      return 3;
    aa_message_destroy_v1(m);
    aa_engine_destroy_v1(e);
  }
  aa_engine *e = nullptr;
  aa_message *m = nullptr;
  if (aa_engine_create_v1((uint8_t *)c.data(), c.size(), (uint8_t *)d.data(),
                          d.size(), &e, &m))
    return 4;
  for (long n = 0; n < 500; n++) {
    aa_fail_after = n;
    int status = aa_solve_v1(e, (uint8_t *)r.data(), r.size(), nullptr, &m);
    aa_fail_after = -1;
    if (status != 0 && status != 1 && status != 3)
      return 5;
    aa_message_destroy_v1(m);
  }
  aa_engine_destroy_v1(e);
  aa_cancel *t = nullptr;
  aa_fail_after = 0;
  int status = aa_cancel_create_v1(&t);
  aa_fail_after = -1;
  if (status != 3 || t)
    return 6;

  auto span = [](const std::string &s) {
    return std::span(reinterpret_cast<const std::byte *>(s.data()), s.size());
  };
  auto cat = std::get<Catalog>(Catalog::load(span(c)));
  auto ast = std::get<AstrometryData>(AstrometryData::load(span(d)));
  auto engine = std::get<Engine>(Engine::create(cat, ast));
  auto request = J::parse(r);
  std::vector<RayStar> stars;
  for (auto &s : request["observation"]["stars"]) {
    auto &v = s["direction_camera"];
    stars.push_back({std::stoull(s["id"].get<std::string>()),
                     {v["x"], v["y"], v["z"]},
                     std::nullopt});
  }
  ObservationIdentity id{"fault",
                         "camera",
                         "cal",
                         {{1577836800, 123456789}, .01, .001},
                         TemporalModel::StationarySingleExposure,
                         "clock"};
  EarthContext ctx{{0, 0, 0}, NoRefraction{}, EopPolicy::ObservedOnly};
  SolveOptions opts{{6, 1e-4, 1e-5, std::nullopt}, {50000, 2000000, 67108864}};
  aa_unexpected = true;
  auto result = engine.solve(RayObservation{id, stars}, ctx, opts);
  aa_unexpected = false;
  if (!std::holds_alternative<Error>(result) ||
      std::get<Error>(result).code != ErrorCode::InternalError)
    return 7;
  std::cout << "751 allocation-failure checks and unexpected-exception "
               "containment passed\n";
}
