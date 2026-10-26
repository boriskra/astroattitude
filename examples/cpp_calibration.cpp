// Recorded-data guide. This program never acquires images or qualifies a phone.
#include <astroattitude/calibration.hpp>
#include <fstream>
#include <iostream>
using namespace astroattitude;
std::string read(const std::string &p) {
  std::ifstream f(p);
  if (!f)
    throw std::runtime_error("Cannot read input");
  return {std::istreambuf_iterator<char>(f), {}};
}
std::span<const std::byte> bytes(const std::string &s) {
  return {reinterpret_cast<const std::byte *>(s.data()), s.size()};
}
template <class T> T take(Outcome<T> out) {
  if (auto e = std::get_if<Error>(&out))
    throw std::runtime_error(e->detail);
  return std::get<T>(std::move(out));
}
int main(int argc, char **argv) {
  try {
    if (argc != 4) {
      std::cerr << "Usage: cpp_calibration catalog.json astrometry.json "
                   "recorded-journal.json\n";
      return 2;
    }
    auto catalog = read(argv[1]), astrometry = read(argv[2]),
         journal = read(argv[3]);
    auto session = take(CalibrationSession::restore(
        take(Catalog::load(bytes(catalog))),
        take(AstrometryData::load(bytes(astrometry))), journal));
    auto progress = session.progress();
    auto action = session.next_action();
    std::cout << progress.admitted_frames << " of " << progress.frames
              << " frames admitted across " << progress.blocks << " blocks\n"
              << action.reason << '\n';
    if (progress.ready_to_fit) {
      auto result = session.fit();
      if (auto e = std::get_if<Error>(&result))
        std::cout << "More evidence needed: " << e->detail << '\n';
      else {
        auto candidate = std::get<CalibrationCandidate>(result);
        std::cout << "Diagnostic candidate " << candidate.data().id
                  << "; save journal separately from validated export\n";
      }
    }
    // A live adapter uses prepare(camera metadata), add_frame, finish_block and
    // propose_validation. It freezes only before FUTURE exposures, never this
    // replay.
    std::cout << "Retrospective example; absolute accuracy unqualified\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
