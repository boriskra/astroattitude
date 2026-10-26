#include "astroattitude/astroattitude.h"
#include "workflow_json.hpp"
#include <fstream>
#include <iostream>
using namespace astroattitude;
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      return 2;
    std::ifstream input(std::string(argv[1]) + "/extraction.json");
    WorkflowJson fixture;
    input >> fixture;
    auto m = fixture.at("metadata");
    auto pixels = fixture.at("pixels").get<std::vector<float>>();
    LinearImage image{m.at("width").get<uint32_t>(),
                      m.at("height").get<uint32_t>(),
                      m.at("row_stride").get<uint32_t>(),
                      pixels,
                      m.at("layout").get<SampleLayout>(),
                      m.at("black_levels").get<std::array<double, 4>>(),
                      m.at("white_levels").get<std::array<double, 4>>(),
                      {}};
    auto result =
        StarExtractor::extract(image, m.at("options").get<ExtractionOptions>());
    if (auto error = std::get_if<Error>(&result))
      throw std::runtime_error(error->detail);
    WorkflowJson native = std::get<ExtractionResult>(result);
    if (native != fixture.at("expected"))
      throw std::runtime_error("C++/Python shared fixture mismatch");
    m["schema"] = "astroattitude.extract/1";
    auto request = m.dump();
    aa_message *message = nullptr;
    auto status =
        aa_extract_v1(pixels.data(), pixels.size(), nullptr, 0,
                      reinterpret_cast<const uint8_t *>(request.data()),
                      request.size(), nullptr, &message);
    uint64_t length = 0;
    auto data = aa_message_bytes_v1(message, &length);
    auto text = data ? std::string(reinterpret_cast<const char *>(data), length)
                     : "null";
    aa_message_destroy_v1(message);
    if (status || WorkflowJson::parse(text).at("value") != native)
      throw std::runtime_error("C/C++ shared fixture mismatch");
    std::cout << "Shared C++/C/Python extraction fixture parity passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
