# V1 resources and astrometry model

Status: revision 2 implemented. Only synthetic packages have been generated for
validation. No real catalog/EOP package has been adopted or physically qualified.

## Packaging and ownership

Use two standalone UTF-8 JSON objects, no archive, filesystem paths or executable
plugins: `astroattitude.catalog/1` and `astroattitude.astrometry/1`. Engine loading
owns a validated copy. Maximum byte lengths are 16 MiB and 4 MiB respectively;
maximum parsed depth 8, string length 1024 bytes, and nodes 500000/100000. UTF-8 BOM,
duplicate keys, unknown keys, nonfinite numbers, compressed data and trailing tokens
are rejected. Counts/strings have stricter limits below. Object order is irrelevant;
array order is semantically specified. Hash the exact input bytes using SHA-256;
content_id is `sha256:` plus lowercase hex. No self-referential hash inside a package.

Common required top-level keys:

| Key | Type/meaning |
| --- | --- |
| schema | Exact schema string above |
| provenance | Object described below |
| valid_from_utc_s, valid_until_utc_s | Integral POSIX seconds, half-open validity interval |

`provenance` has exactly `sources`, `conversion_id`, `conversion_manifest_sha256`.
`sources` is an array of 1..16 objects, sorted by unique `source_id`. Each has exactly
`source_id`, `source_version`, `source_uri`, `source_sha256`, `license_id`,
`attribution`, `role` (strings). Role is one of `catalog`, `eop`, `leaps`,
`announcement`, `documentation`. Catalog packages require a catalog source;
astrometry packages require EOP, leap and announcement sources. Source URI is
descriptive and never fetched. Both SHA-256 fields are 64 lowercase hex.
license_id may be `NOASSERTION` or a documented `LicenseRef-...`; neither is a
distribution permission. URI/attribution <=1024 bytes; other strings <=128
printable ASCII bytes, nonempty.

The external conversion manifest is hashed as exact bytes and retained by the
application. It records every input/document hash, converter revision and dependency
pins, parameters, exclusions/counts, epoch/unit transformations, assumptions,
source-column selection, validity derivation and notices. It must not contain the
resulting package hash (avoid a circular hash); a separate adoption record binds
manifest and output hashes. The runtime validates the reference's syntax, not the
external manifest or rights. Provisioning verifies those before admitting a package.
The manifest is not a third runtime input. It keeps composite provenance auditable
without expanding the solve API. See [dependency notices](../notices/README.md).

No catalog or EOP source is bundled by default. Application provisioning supplies
approved packages; runtime performs numerical/schema validation, not copyright or
identity attestation. Tests can load synthetic packages through the real loader;
that does not convert them into trusted application assets. Production target
separation is not a security boundary against a caller fabricating measurements.

## Catalog schema

Additional required keys: `namespace`, `frame`, `epoch_jd_tdb`, `coverage`, `stars`.
namespace is a <=128-byte identifier; frame is exactly `ICRS`; epoch_jd_tdb is exactly
2451545.0 (J2000). Importers normalize other epochs before packaging and record how.
V1 consumes the ERFA-style J2000 catalog convention; a frame label alone does not
perform an epoch transformation.

`coverage` has exactly `kind` = `declination_band`, `min_dec_rad`, `max_dec_rad`,
`limiting_magnitude` (finite). Bounds lie in [-pi/2,pi/2], lower < upper. This is a
declared selection domain, not a guarantee every star is present. Index admission
must not invent all-sky coverage. `stars` has 6..10000 records, sorted lexicographically
by unique `id`. All rows have exactly:

| Key | Type/meaning |
| --- | --- |
| id | Nonempty printable ASCII <=64 bytes, no colon |
| ra_rad | [0,2*pi) |
| dec_rad | (-pi/2,pi/2), and within coverage; exact celestial-pole coordinates unsupported |
| pm_ra_rad_per_jyear | Finite dRA/dt, **not** cos(dec)*dRA/dt |
| pm_dec_rad_per_jyear | Finite dDec/dt |
| parallax_arcsec | Finite >=0 |
| radial_velocity_km_s | Finite, positive receding |
| magnitude | Finite [-30,40] |
| motion_status | `measured` or `assumed` |
| parallax_status | `measured` or `assumed` |
| radial_velocity_status | `measured` or `assumed` |

Required fields cannot be null. Additional admission bounds are abs(each proper
motion component)<=0.1 rad/Julian-year, parallax<=10 arcsec and abs(radial velocity)
<=1000 km/s. These intentionally restrict the v1 catalog domain, not catalog truth. An upstream catalog with unknown motion/distance
must either exclude that row or provide an explicitly assumed numeric value and
status. Measured zero and assumed zero are distinguishable. Nonzero source motion
must not be dropped by an importer. Assumptions in **matched** rows appear in result
limitations. Records outside a mathematically supported backend domain fail loading;
no normalization into physically different stars. Public IDs are namespace + ':' +
id; the resource content ID disambiguates revisions. Per-star astrometric covariance
is outside v1 because the result covariance is explicitly conditional on references.

Statuses describe the origin of the parameter, including deterministic propagation
of a measurement. They do not establish physical accuracy. Unknown radial velocity
cannot be presented as measured distance information. Propagation using an assumed
radial velocity retains that assumption in matched-row limitations. The initial
Hipparcos import profile in `tools/convert.py` excludes absent/invalid astrometry and
nonpositive parallax; it assigns an explicitly assumed zero radial velocity.
No backend warning that changes distance or motion may be silently accepted.

## Astrometry schema

Additional required keys: `model_id`, `leap_valid_until_utc_s`, `leaps`, `eop`.
model_id is exactly `erfa-2.0.1-iau2006a-tt/1`. The valid interval is inside
[2000-01-01T00:00:00Z, 2050-01-01T00:00:00Z); this is a deliberately bounded v1
engineering domain, narrowed by actual data validity, not an accuracy warranty.

`leaps`: 1..256 records, strictly increasing `effective_utc_s` (midnight POSIX second)
and integral `tai_minus_utc_s` in [0,255]. The first must establish the offset at valid_from;
subsequent changes are +/-1 second. No assumed indefinite offset after
leap_valid_until; the whole requested exposure must be supported. Missing/future
announcements are a provisioning decision, never inferred from the machine clock.

`eop`: 2..20000 records, strictly increasing `utc_s`, no gap >172800 s. Each has
exactly `utc_s`, `ut1_minus_utc_s`, `xp_rad`, `yp_rad`, `status`. Status is `observed`
or `predicted`. Values are finite; abs(UT1-UTC)<=1 s and abs(xp),abs(yp)<=1e-4 rad
are schema sanity limits. EOP rows must bracket the requested exposure interval;
no extrapolation. An exact sample uses its own status. Between rows, interpolate
xp/yp and **UT1-TAI** linearly in elapsed TAI seconds, then reconstruct UT1-UTC at
the requested instant. This avoids interpolating across a DUT1 leap discontinuity.
If either used endpoint is predicted, context is predicted; ObservedOnly rejects it.

For the selected USNO source, use all three Bulletin B values when all are present;
otherwise use all three Bulletin A values. Never mix A/B components within a row.
An A row is observed only when both polar-motion and UT1 flags are `I`; either `P`
makes it predicted. Missing/unknown required values or flags exclude the row;
blank future placeholders are not coverage. B rows are observed. This deliberately
does not equate rapid observed values with final or error-free measurements.

Coverage metadata cannot extend past usable rows or leap validity. Time validation
uses the full interval midpoint +/- duration/2 and, when known, +/-3*midpoint_sigma.
Unknown timing uncertainty is retained and reported, not replaced with zero.

## Time and reference-direction decisions

UtcInstant represents ordinary unsmeared POSIX UTC instants only. Leap-second labels
cannot be represented. Reject exposure/support intervals touching the closed window
[transition-2 s,transition+2 s] of a leap offset change; no silent smear/normalization.
This conservative exclusion is a v1 policy. The caller must establish UTC mapping;
POSIX bytes alone cannot prove a camera timestamp is unsmeared or accurate.

Select ERFA 2.0.1 semantics as the reference backend, with IAU 2006/2000A
precession/nutation, Earth rotation from UT1, polar motion, Earth ephemeris,
annual/diurnal aberration, solar deflection, catalog space motion/parallax and
optional ERFA optical refraction. No measured dX/dY corrections, planetary
multi-body deflection, subdaily EOP tidal/libration corrections, wavelength-dependent
centroid model or terrain horizon. Daily linear interpolation is a v1 approximation,
not full IERS Conventions transformation fidelity.
TT = TAI +32.184 s; **TT approximates TDB** for the ephemeris/context calculations
and is reported as an explicit limitation. Do not advertise full apparent-place
accuracy from this bounded model. Final direction conversion from observed azimuth
A (north through east), zenith distance z: ENU = (sin(z)sin(A), sin(z)cos(A), cos(z)).

Use lower-level ERFA routines with caller-computed TT/UT1 and engine-owned leap
resources; do not call eraSetLeapSeconds or depend on mutable global leap tables.
High-level atco13 behavior is a reference, not an instruction to bypass that rule.
NoRefraction supplies zero refraction constants and reports refraction_disabled.
Atmosphere is converted explicitly from Pa/K/m to hPa/Celsius/micrometers.

All matched sources must have **unrefracted modeled elevation >=20 degrees**.
This avoids low-altitude model use in both modes; higher limits can be imposed by
the application. There is no artificial replacement direction below the domain.
A completed search with inadequate supported matches returns NoMatch, with context
limitations; it must not claim the catalog is incomplete merely because no match
was found. Exact catalog/EOP/backend identities remain in each result.

## Engine construction budgets

Maximum engine-owned memory is 256 MiB, including copied resources and derived
indexes. Construction is capped at 20000000 catalog pair/triple geometry evaluations
and returns ResourceLimitExceeded on excess rather than building unbounded triangles.
Indexes are completed during create; no lazy per-solve index mutation is allowed.
Solve counters count query/refinement work, not already completed construction.
Thus cold/warm cache state cannot change search completion semantics. No performance
warranty is inferred from the caps. Dependency/build provenance and allocation
accounting are checked separately from numerical correctness.

## Source basis and adoption policy

- [ERFA 2.0.1 atco13](https://github.com/liberfa/erfa/blob/v2.0.1/src/atco13.c): observed-place inputs, J2000 epoch and dRA/dt convention.
- [ERFA 2.0.1 apco](https://github.com/liberfa/erfa/blob/v2.0.1/src/apco.c): caller-supplied Earth/time context permits avoiding global leap state.
- [ERFA 2.0.1 epv00](https://github.com/liberfa/erfa/blob/v2.0.1/src/epv00.c): documented ephemeris limitations and TT substitution.
- [ERFA license](https://github.com/liberfa/erfa/blob/v2.0.1/LICENSE): BSD-3-Clause-style terms; exact copied source/notices still require inventory before adoption.

These references inform the design; they do not make the native implementation and
its wrappers independent scientific oracles. Dependency versions and notices are
recorded in [third-party notices](../notices/README.md). The MIT license covers
original library code only. Catalog and time resources remain caller-provided and
require their own provenance, intended-use and redistribution review.
