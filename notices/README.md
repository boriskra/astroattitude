# Third-party notices

Pinned archive identities are in cmake/dependencies.json. The fetch script verifies
exact bytes before extraction. Third-party source remains in the ignored build
cache; it is not relabeled under a library license.

- ERFA 2.0.1: BSD-style ERFA terms and per-file notices, including SOFA provenance.
- Eigen 3.4.0: MPL-2.0; compiled with EIGEN_MPL2_ONLY. Included BSD portions retain
  their notices. No unsupported/GPL/LGPL module is used.
- nlohmann/json 3.12.0: MIT, with included-file third-party notices retained.
- PicoSHA2 1.0.1: MIT.
- Gradle wrapper: Apache-2.0; Gradle NOTICE retained. Build tool, not solver code.
- Kotlin standard library 2.2.21 and kotlinx.serialization 1.9.0: Apache-2.0;
  runtime dependency versions are locked in the Android Gradle lockfiles.

INCLUDED_FILES.json records SHA-256 for the observed included dependency headers
on the native and both Android builds, plus built ERFA sources. The corresponding
copyright/license comment blocks are retained in INCLUDED_COPYRIGHTS.txt, grouped
without discarding file attribution. tools/audit_included_files.py regenerates
that inventory from Ninja dependency records. System SDK/libc++ headers are
platform toolchain components and are not copied into the library's source bundle.

No Hipparcos, IERS/USNO EOP, leap list or bulletin asset is distributed here.
Synthetic fixtures are test-generated. Their public source-URI strings are labels,
not network requests or approval of real-data rights. See [resource provisioning](../docs/RESOURCES.md).

The optional [iOS alignment example](../examples/ios-alignment/README.md) alone
uses AprilTag 3.4.5 (BSD-2-Clause). Its adapter fetch script pins the archive hash;
its [license](../examples/ios-alignment/Adapter/APRILTAG-LICENSE.txt) also accompanies
the generated family-derived printable target. It is not linked into the core.
