# astroattitude

**Estimate a camera's orientation from stars, offline.**

astroattitude is a C++20 library with C, Python, Swift and Kotlin interfaces. Give it
star centroids or calibrated camera rays, an exposure time, an observer position
and catalog/time resources. It returns camera orientation in local East–North–Up
coordinates, or a typed failure that explains why the observation could not be solved.
Geographic location is an input; the library does not estimate your GPS position.

## What you can build

- **Star-based orientation:** source association, explicit coordinate conventions,
  bounded work and caller-controlled resource validity.
- **Star extraction:** synchronous processing of decoded linear monochrome or Bayer
  pixels, with source diagnostics and optional pixel masks.
- **Calibration workflows:** guided collection stages, forward Brown optical models,
  immutable validation plans and explicit incomplete-evidence reports.
- **Alignment measurements:** compare cameras in a common frame, with explicit
  gravity observability. Try the [iOS alignment app](examples/ios-alignment/README.md).
- **Portable integration:** one native implementation, explicit ownership and
  cancellation, with no camera access or runtime network requests.

**Status: experimental, source-only.** Synthetic controls exercise the implementation;
physical accuracy is unqualified. A small residual or repeatable result is not an
absolute pointing-accuracy bound. See [validation and limitations](docs/VALIDATION.md).

## Try a synthetic solve

You need a C++20 compiler, CMake 3.24+, Ninja and Python 3.9+. From a fresh clone:

```sh
git clone https://github.com/boriskra/astroattitude.git
cd astroattitude
python3 tools/fetch_dependencies.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DAA_BUILD_EXAMPLES=ON
cmake --build build -j12
ctest --test-dir build --output-on-failure -j12
./build/aa_cpp_example build/fixtures/catalog.json build/fixtures/astrometry.json build/fixtures/rays.json
```

Expected output starts with `8 synthetic matches; RMS=` followed by a near-zero
residual in radians (the last digits vary by platform). The test
suite generates its own catalog and observations; these are not sky measurements
and must not be used as production resources. Dependency downloads are hash-pinned;
the library itself runs offline.

## Choose your interface

| Interface | How to use it |
| --- | --- |
| C++20 | Link the native library with CMake. |
| C | Use the versioned C ABI and opaque handles. |
| Python 3.9+ | Load your locally built shared library through the ctypes wrapper. |
| Swift / macOS and iOS | Build the XCFramework, then use the local Swift package. |
| Kotlin / Android | Build both native ABIs, then package the local Android library. |

Start with the [build guide](docs/BUILD.md) for platform requirements and runnable
examples. No PyPI, Maven or remotely installable Swift package is published yet.

## Learn more

- [Documentation index](docs/README.md)
- [Extract stars from pixels](docs/STAR_EXTRACTION.md)
- [Walk through calibration](docs/CALIBRATION_WORKFLOW.md)
- [Provide catalog and time resources](docs/RESOURCES.md)
- [C++ API](docs/API.md) and [C ABI](docs/C_ABI.md)

Applications provide image decoding, observation location/time, resource admission
and calibration activation. No real catalog, Earth-orientation data, images or device
calibration is bundled. Missing measurements remain missing; covariance is not
manufactured from fit residuals.

## Contribute

See [CONTRIBUTING.md](CONTRIBUTING.md) for development and synthetic bug reports.
Keep sky images and observation metadata private: their combination can reveal a
physical location. Use [SECURITY.md](SECURITY.md) for sensitive reports.

## License

Original code is available under the [MIT license](LICENSE).
[Third-party notices](notices/README.md) cover dependencies and the Gradle wrapper.
