# Stable C boundary: v1

Status: implemented, unreleased; see include/astroattitude/astroattitude.h and [validation](VALIDATION.md). C++ structs in API.md are
convenience values, not binary layouts. To avoid freezing two large struct graphs,
v1 uses opaque handles and bounded UTF-8 JSON messages at the C boundary. C++ clients
use typed methods directly; Swift/Python adapters encode/decode once per exposure.
This trades modest serialization cost for explicit compatibility and ownership;
P3 must measure the cost before claiming an application runtime target.

## Signatures

```c
typedef struct aa_engine aa_engine;
typedef struct aa_message aa_message;
typedef struct aa_cancel aa_cancel;

uint32_t aa_abi_version(void); /* Returns 1. */

int32_t aa_engine_create_v1(
    const uint8_t *catalog, uint64_t catalog_len,
    const uint8_t *astrometry, uint64_t astrometry_len,
    aa_engine **out_engine, aa_message **out_error);
void aa_engine_destroy_v1(aa_engine *engine);

int32_t aa_solve_v1(
    const aa_engine *engine,
    const uint8_t *request, uint64_t request_len,
    const aa_cancel *cancel,
    aa_message **out_response);

const uint8_t *aa_message_bytes_v1(
    const aa_message *message, uint64_t *out_len);
void aa_message_destroy_v1(aa_message *message);

int32_t aa_cancel_create_v1(aa_cancel **out_cancel);
void aa_cancel_request_v1(aa_cancel *cancel);
void aa_cancel_destroy_v1(aa_cancel *cancel);
```

Supported platforms are 64-bit little-endian arm64 and x86_64 with IEEE binary64;
C uses the platform C calling convention and fixed-width stdint types. No packed
struct, compiler STL, exception, enum layout, boolean layout, allocator or borrowed
result object crosses the ABI. Symbols ending _v1 never change signature/meaning.
`aa_abi_version` is the highest supported major; callers request the explicit suffix,
not assume all future majors are compatible.

## Ownership and status

- Buffers are read-only and borrowed through the synchronous call. The library
  never takes ownership of caller memory. Lengths exclude a terminating NUL.
- Valid out pointers are required. The function sets every valid out pointer to
  NULL before doing work. NULL input with nonzero length is an ABI error; empty
  JSON is a semantic error. Arbitrary invalid addresses cannot be safely detected.
- Status 0: success. create produces an engine and no error; solve produces a
  success response. Status 1: semantic rejection, with a required error response.
- Status 2: invalid ABI arguments; no response promised. Status 3: allocation
  failure; no response promised. Status 4: unexpected internal failure; no response
  promised. C++ bad_alloc maps to ResourceLimitExceeded in the typed API; ABI status
  3 remains distinct because even error-message allocation may be impossible.
- Nonzero status never produces a usable engine or solution. No exception crosses
  C. Adapters preserve semantic ErrorCode separately from ABI failures.
- aa_message_bytes_v1 requires a live message and valid out_len; NULL message
  returns NULL and sets a valid out_len to zero. NULL out_len returns NULL.
- Messages own immutable bytes, valid until aa_message_destroy_v1. Callers must
  not free or mutate returned bytes. NULL message destroy/engine destroy/cancel
  destroy are no-ops; double destroy and use-after-destroy are caller violations.
- A live engine is shareable across calls. Destroy it only after all calls return.
  Message handles may be read concurrently while alive. No callback occurs.
- Cancellation token is optional, thread-safe to request, and one-way (no reset).
  Retain it until all calls using it finish; create a new token for a later request.
  A cancelled solve returns status 1 with ErrorCode Cancelled. Cancellation has the
  check granularity in API.md, not a guaranteed wall-clock response time.
- No thread-local last-error string. Error information belongs to the response.

## Solve request schema

Root has exactly `schema`, `kind`, `observation`, `calibration`, `context`, `options`.
Schema is `astroattitude.solve/1`; kind is `pixels` or `rays`. Observation, context,
options and calibration map one-to-one to API.md structs using the same snake_case
field names. There are no omitted default values. Every optional field is required
and encodes an absent value as JSON null. For pixels calibration is an object; for
rays it is null. The observation's stars array selects PixelStar/RayStar by kind.

Additional mapping rules:

| C++ type/field | JSON |
| --- | --- |
| Vec2, Vec3 | Objects {x,y} and {x,y,z} |
| Mat2, Mat3 | Objects with row_major array of exactly 4 or 9 finite numbers |
| Rotation3 | Object with matrix: Mat3 |
| DetectionId | Canonical unsigned decimal string, 0..2^64-1; no leading zeros except "0" |
| UtcInstant.unix_seconds | Canonical signed decimal string in int64 domain; no + or -0 |
| Other integer fields | Integer JSON numbers within their API bounds |
| ExposureTime.midpoint_sigma_s | Finite nonnegative number or null |
| Refraction variant | {"kind":"disabled"} or {"kind":"atmosphere","parameters":Atmosphere} |
| EopPolicy | "observed_only" or "allow_predicted" |
| TemporalModel | "stationary_single_exposure" or "aligned_effective_exposure" |
| std::span/vector | Array in its element schema |
| Optional covariance/flux/conditional sigma | Its ordinary value or null |

`RayObservation.identity` and `PixelObservation.identity` use the same schema;
there is no ad hoc optional-field bag. Calibration has no parameter_covariance in
v1. Source package schemas are defined separately in RESOURCES.md.

## Response schema

Success root has exactly `schema`, `outcome`, `solution`; schema is
`astroattitude.response/1`, outcome `success`, solution is AttitudeSolution mapped
above. Failure root has exactly `schema`, `outcome`, `error`; outcome `error`, error
is Error with code serialized as the exact PascalCase ErrorCode spelling in API.md,
detail string and affected_detections array. Creation semantic failures use this
same error envelope. Detail is for humans and is not parsed for logic.

ModelReport model_id equals the astrometry package model_id. Required limitations
are `total_accuracy_not_evaluated`, `nuisance_parameters_fixed`,
`catalog_uncertainty_excluded`, `tt_approximates_tdb`,
`celestial_pole_offsets_omitted`, `eop_subdaily_corrections_omitted`, `temporal_model_caller_asserted`.
Conditional flags, when applicable: `measurement_noise_unavailable`,
`timing_uncertainty_unavailable`, `refraction_disabled`, `refraction_model_error`,
`predicted_eop`, `assumed_catalog_motion`, `assumed_catalog_parallax`, `assumed_catalog_radial_velocity`.
Flags are unique and lexicographically sorted; matched rows determine catalog
assumption flags. New flags require a minor documented schema revision and clients
must preserve unknown output flags, not treat them as known-zero limitations.
No source payload, operator media or hidden credentials enter Error.detail.

Input validation rejects unknown keys, duplicate keys, BOM, trailing content,
nonfinite values and wrong types. Request cap is 256 KiB, depth 12, 20000 nodes;
response cap is 256 KiB. Input strings follow API.md, except the explicitly larger
resource and generated catalog-ID fields. Round-trip double text must preserve
binary64 values (17 significant digits or a proven shortest-round-trip equivalent).
No negative-zero normalization or key-order identity is promised for responses.
Resource hashes remain hashes of raw supplied resource bytes, not reserialized JSON.

## Evolution and adapter behavior

Inputs are strict to catch mistakes. A major schema change needs new schema names
and C symbols. Additive output limitation vocabulary alone can evolve under the
rule above; adding required input keys is not backward compatible. Adapters must
verify schema/outcome, reject malformed library output and retain provenance.
Unknown ErrorCode maps to a typed unknown-library-error, never success.

C++ and C encodings must produce the same fit, rejection reason, matched IDs and
resource identities for identical semantic input. C JSON is not a network protocol
or an authenticated admission mechanism. Trust checks remain in the consuming application. No
large public struct ABI or separate Swift/Python numerical implementation is added.

## Additive experimental extraction/calibration operations

`aa_extract_v1` takes a borrowed contiguous float sample buffer, optional dense
uint8 exclusion mask and separate UTF-8 `astroattitude.extract/1` metadata. Metadata
contains raster dimensions/stride, CFA layout, four parity black/white levels and
`ExtractionOptions`; no image samples appear in JSON. Maximum metadata is 16 KiB.
Native C++ types are documented in `extraction.hpp` and `calibration.hpp`.

`aa_calibration_create_v1` accepts `astroattitude.calibration-prepare/1` with a
`preparation` object for ordinary camera metadata, or
`astroattitude.calibration-create/1` with an explicit expert `configuration`.
Preparation derives an unqualified seed in native code. `aa_calibration_restore_v1`
accepts a bounded journal and rechecks its resources and evidence.

`aa_calibration_call_v1` accepts `astroattitude.calibration-call/1`, `operation`,
and `value` (explicit null for operations without input). Supported operations:

| Operation | Input / successful value |
| --- | --- |
| `add_frame` | CalibrationFrame / IngestReceipt with matches and failures |
| `finish_block` | BlockCompletion / BlockReceipt |
| `progress`, `next_action` | null / progress or one actionable reason |
| `fit` | null / CandidateData plus immutable artifact handle |
| `model_diagnostics` | null / attempted family diagnostics |
| `propose_validation` | ValidationRequest / availability-selected future plan |
| `freeze` | ValidationPlan plus candidate handle / freeze digest |
| `validate` | null / ValidationReport plus immutable artifact handle |
| `export_validated` | null / CalibrationPackage, or explicit gate failure |
| `journal` | null / journal string |
| `pause`, `resume`, `end` | null / resulting stage |

Success responses use `astroattitude.workflow-response/1`, outcome `success`,
and `value`. Errors retain the existing error envelope and status conventions.
Workflow response/journal bound is 128 MiB; ordinary call input is 4 MiB. Create
configuration is 16 KiB. Existing solve limits are unchanged. uint64 detection
identities and signed UTC seconds remain canonical decimal strings. Limits/counts
are checked JSON integers, never silently wrapped uint32 values.

Sessions are mutable and must be serialized by C callers. Artifact handles are
immutable and must be destroyed explicitly; a report handle cannot stand in for
a candidate. Buffers live through the synchronous call only. Destruction requires
no active calls. Python/Swift/Kotlin wrappers implement call leases, serialization,
cancellation and explicit close. Their generated value models share one schema;
all extraction, association and fitting remain native. Diagnostic candidate data
and saved journals do not constitute validated export.
