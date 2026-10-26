# Public-source policy

This repository distributes source code, dependency notices and independent synthetic
controls. Real recordings and their derivatives stay outside Git, including source
patches, centroids, catalog associations tied to observations, exact times, sites,
attitudes, sensor logs, handset coefficients, masks and session-specific summaries.
Removing EXIF metadata does not make a sky image anonymous.

## Before a push

Run `python3 tools/check_publication.py`, the synthetic suites and a local credential
scanner. Review every changed file. The explicit inventory in `publication-files.json`
assigns each exported path a purpose. A new path requires deliberate inventory review;
never regenerate the inventory blindly to silence a failure. Binary assets require
an exact hash approval. Media and archives are excluded except the audited Gradle
wrapper. Synthetic generation must not read recordings, even indirectly.

The checker verifies inventory, common credential patterns, private references,
local links and synthetic provenance. Automated scans cannot prove that arbitrary
numeric arrays are anonymous; manual review remains necessary. Do not upload private
files to a scanner or include secret values in reports.

GitHub CI is an additional regression check, not protection against the first unsafe
upload. Public source history starts with an audited snapshot; private branches,
commit metadata, tags, issues and Wiki pages must never be mirrored here.

## License and dependencies

Original code is MIT licensed. Preserve third-party notices. Public dependency URLs
are allowed, including Eigen's pinned upstream source archive. No catalog, Earth
orientation or leap-second source asset is distributed. This initial publication
contains no downloadable native releases or package-registry release.
