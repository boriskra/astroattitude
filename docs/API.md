# C++ API reference

The installed header is `include/astroattitude/astroattitude.hpp`.
[Resources](RESOURCES.md), [C ABI](C_ABI.md) and [validation](VALIDATION.md)
describe provisioning, ownership and interpretation.

## Scope and conventions

Two synchronous entry points accept measured pixels or calibrated rays and return
camera attitude in WGS84 local ENU. No image frontend, streaming estimator,
geographic fix, calibration fitting, or total physical-accuracy certification is
part of v1. A successful solve is an accepted **conditional numerical fit**.

- Camera is right-handed: +X right, +Y down, +Z forward.
- ENU is ellipsoid-based East, North, Up at the supplied optical-center position.
  It is not the local gravity/plumb frame. Longitude defines East at a pole.
- Column vectors: `v_enu = R_enu_from_camera * v_camera`; matrices store row-major.
- The output rotation maps camera axes into geometric ENU. Predicted star directions
  in that basis include the explicitly selected atmospheric correction. There is no
  alternate 'refracted attitude frame'. NoRefraction may bias a real-image fit.
- Pixels have integer centers, top-left `(0,0)`, in the calibration raster. Adapters
  explicitly handle crop, rotation, mirror and processing-mode mapping.
- SI units, radians, WGS84 ellipsoidal meters; no mean-sea-level height substitution.
- Rotation error convention: `R_true = Exp(delta_theta_enu) * R_estimate`.
- Engine/resource values own immutable state. Input spans remain borrowed only for
  the call. Results own their arrays/strings. Concurrent calls use private workspace.
- No network, ambient files, wall-clock lookup, mutable global RNG or last-good pose.

## Public values

The declarations are in namespace `astroattitude`, with the standard C++20 array,
optional, span, string, variant and vector facilities. Class move/copy semantics are
ordinary value semantics over shared immutable storage; string_view accessors are
valid while their owning object remains alive. Destruction must not race a call.

```cpp
using DetectionId = uint64_t;
struct Vec2 { double x, y; };
struct Vec3 { double x, y, z; };
struct Mat2 { std::array<double, 4> row_major; };
struct Mat3 { std::array<double, 9> row_major; };
struct Rotation3 { Mat3 matrix; };

enum class ErrorCode {
    InvalidInput, InvalidResource, UnsupportedVersion, CalibrationMismatch,
    OutsideCalibrationDomain, UnsupportedObservation, InsufficientObservations,
    AstrometryDataUnavailable, NoMatch, AmbiguousMatch, UnobservableGeometry,
    ResidualLimitExceeded, UncertaintyUnavailable, ConditionalPrecisionNotMet,
    ResourceLimitExceeded, Cancelled, InternalError
};
struct Error {
    ErrorCode code;
    std::string detail; // Non-normative; branch on code.
    std::vector<DetectionId> affected_detections;
};
template<class T> using Outcome = std::variant<T, Error>;

struct UtcInstant {
    int64_t unix_seconds;
    uint32_t nanoseconds; // [0, 1e9)
};
struct ExposureTime {
    UtcInstant midpoint;
    double duration_s;
    std::optional<double> midpoint_sigma_s; // Retained, not propagated in v1.
};
struct ObserverPosition {
    double latitude_rad, longitude_rad, ellipsoid_height_m;
};

enum class TemporalModel { StationarySingleExposure, AlignedEffectiveExposure };
struct ObservationIdentity {
    std::string observation_id;
    std::string camera_configuration_id;
    std::string calibration_revision;
    ExposureTime exposure;
    TemporalModel temporal_model;
    std::string timing_provenance_id;
};
struct PixelStar {
    DetectionId id;
    Vec2 pixel;
    std::optional<Mat2> centroid_covariance_px2;
    std::optional<double> flux;
};
struct RayStar {
    DetectionId id;
    Vec3 direction_camera;
    std::optional<Mat3> direction_covariance_rad2;
};
struct PixelObservation {
    ObservationIdentity identity;
    uint32_t width, height;
    std::span<const PixelStar> stars;
};
struct RayObservation {
    ObservationIdentity identity;
    std::span<const RayStar> stars;
};
```

IDs are unique per observation and survive sorting. Flux is finite/positive when
present and is only a ranking hint. Each observation provides covariance for either
**every** detection or **none**; mixed availability is InvalidInput. Supplied
covariances describe independent measurement noise conditional on shared camera,
time and reference parameters. They do not represent cross-star systematic error.
An adapter unable to separate independent noise omits these covariances.

`TemporalModel` is a caller assertion with a provenance ID, not library validation
of motion or registration. Unknown/unsupported timing or rolling-shutter motion is
rejected by the adapter before solving. Exposure length is not a physical motion
certificate. V1 accepts duration in (0,120] s but qualification is narrower.

## Calibration and Earth context

```cpp
struct PinholeBrownModel {
    double fx_px, fy_px, cx_px, cy_px;
    double k1, k2, k3, p1, p2;
};
struct PixelBounds { double min_x, min_y, max_x, max_y; };
struct CameraCalibration {
    std::string revision;
    std::string camera_configuration_id;
    uint32_t width, height;
    PinholeBrownModel model;
    PixelBounds valid_region;
};
struct NoRefraction {};
struct Atmosphere {
    double pressure_pa, temperature_kelvin;
    double relative_humidity, effective_wavelength_m;
};
enum class EopPolicy { ObservedOnly, AllowPredicted };
struct EarthContext {
    ObserverPosition observer;
    std::variant<NoRefraction, Atmosphere> refraction;
    EopPolicy eop_policy;
};
```

Pixels carry the revision they were prepared against. Both revision and camera
configuration must exactly match the supplied calibration; dimensions must match.
IDs do not authenticate calibration. the consuming application retains qualification/revocation
ownership. The ray path carries equivalent provenance and asserts that the caller
already performed unprojection; no fictitious pinhole model is constructed for it.

The pinhole model has zero skew. Brown forward distortion is:

```text
r2 = x*x + y*y; a = 1 + k1*r2 + k2*r2*r2 + k3*r2*r2*r2
xd = x*a + 2*p1*x*y + p2*(r2 + 2*x*x)
yd = y*a + p1*(r2 + 2*y*y) + 2*p2*x*y
u = fx*xd + cx; v = fy*yd + cy
```

Unprojection must recover the central continuous inverse branch with positive
Jacobian determinant and independently check forward error <=1e-6 px. It refuses
folds, nonconvergence or a branch it cannot establish; no claim of global polynomial
injectivity is required. All supplied pixels must be inside inclusive valid_region
and raster bounds. It is an input error to send invalid geometry, not an outlier to
silently drop. Use the ray API for other optics/masks or after filtering invalid
pixels explicitly in the application. Calibration is fixed during a solve; its
parameter covariance was removed because v1 does not propagate shared systematics.

## Numerical admission profile

These are engineering bounds chosen for v1, not measured precision claims. Reject
rather than clamp unless the normalization rule below explicitly permits it.

| Domain | Rule |
| --- | --- |
| Strings | Nonempty printable ASCII, <=128 bytes; generated catalog_star_id <=193 bytes; Error.detail <=1024 UTF-8 bytes |
| Floating values | Finite; positive quantities strictly >0 unless explicitly allowed zero |
| Dimensions | Each 1..32768; fx/fy >0; cx/cy finite (need not be inside raster) |
| Observer | Latitude [-pi/2,pi/2], longitude [-pi,pi), height [-500,10000] m |
| Atmosphere | Pressure 10000..110000 Pa, temperature 180..330 K, humidity [0,1], wavelength 350e-9..1000e-9 m; vacuum uses NoRefraction |
| Unit ray | abs(norm-1) <=1e-10; then normalize once |
| Covariance | Relative symmetry error <=1e-10; symmetrize; materially negative eigenvalues (<-1e-10 times largest absolute eigenvalue) reject |
| Pixel covariance | Strictly positive definite, eigenvalue ratio >=1e-12; no artificial variance floor |
| Ray covariance | Tangent projection changes Frobenius norm by <=1e-10 relative; project; positive tangent eigenvalues, ratio >=1e-12 |
| Returned rotation | Frobenius norm of R^T R-I <=1e-10, abs(det(R)-1)<=1e-10 |
| Fit observability | Tangent information eigenvalue ratio >=1e-10; do not use rank of a three-vector cross-correlation as a substitute |

All-zero covariance is invalid. Tiny tolerated radial/negative roundoff is removed
only by the stated tangent projection; no negative tangent eigenvalue is repaired
into positive certainty. Information scaling must avoid overflow. Inconsistent
numeric outcomes return InternalError or UnobservableGeometry, not a zero matrix.

## Resources, options and methods

```cpp
class Catalog {
public:
    static Outcome<Catalog> load(std::span<const std::byte> json);
    std::string_view content_id() const;
};
class AstrometryData {
public:
    static Outcome<AstrometryData> load(std::span<const std::byte> json);
    std::string_view content_id() const;
};
struct AcceptanceCriteria {
    uint32_t minimum_inliers;
    double maximum_match_error_rad;
    double maximum_fit_rms_rad;
    std::optional<double> maximum_conditional_sigma_rad;
};
struct SolveLimits {
    uint32_t maximum_hypotheses;
    uint64_t maximum_candidate_comparisons;
    uint64_t maximum_workspace_bytes;
};
struct SolveOptions { AcceptanceCriteria acceptance; SolveLimits limits; };
struct Cancellation {
    const std::atomic_bool* requested; // Optional null; borrowed through call.
};
struct AttitudeSolution;
class Engine {
public:
    static Outcome<Engine> create(Catalog catalog, AstrometryData astrometry);
    Outcome<AttitudeSolution> solve(
        const PixelObservation&, const CameraCalibration&, const EarthContext&,
        const SolveOptions&, Cancellation cancel = {nullptr}) const;
    Outcome<AttitudeSolution> solve(
        const RayObservation&, const EarthContext&, const SolveOptions&,
        Cancellation cancel = {nullptr}) const;
};
```

Resource schemas and hard load/engine limits are in [RESOURCES.md](RESOURCES.md).
Each solve accepts 6..128 detections; input beyond the cap is rejected, not
truncated. `minimum_inliers` is 6..number supplied. Match error is (0,0.01] rad;
RMS is positive and <= match error. Conditional sigma, when requested, is positive.
Hypotheses are 1..50000, candidate comparisons 1..2000000, workspace 1..67108864
bytes. A comparison is one exact detection/catalog pair eligibility/residual evaluation (including seed-side checks). Coarse tree-bound visits have a separate 20-million hard cap; they do not silently consume unbounded work.
All internal candidate/search arrays count toward workspace. Output space is
bounded separately by the 128-detection cap and the ABI response limit.

Candidate generation/fit strategy is implementation-versioned, not an ABI choice.
It must be deterministic after sorting by ID, with no process-global randomness.
Hypotheses start from three distinct detection/catalog pairs. Every accepted
hypothesis has at least three verifying correspondences outside that seed,
one-to-one assignment, all final inliers within the match gate, and accepted
RMS/observability. Two accepted hypotheses with different correspondence maps or
rotation separation >1e-8 rad are AmbiguousMatch; no 'best score' tie-break grants
certainty. Identical maps that converge within that rotation tolerance are merged.

Search must finish its declared candidate enumeration before returning success.
Exhausted work/memory limits or cancellation never return a partial winner, even
if one good candidate was found. NoMatch means that completed enumeration found no
acceptable identification; it does not prove no physical solution exists. Uniqueness
is relative to that enumeration, resource coverage and model, not all possible sky
assignments. The output includes search version and actual work counts.

NoMatch applies when no one-to-one identification has the required support. If
supported identities exist but all fail geometry, return UnobservableGeometry;
if any is observable but all fail RMS, return ResidualLimitExceeded. Ambiguity is
tested among supported, observable, residual-accepted candidates before conditional
precision acceptance. Error.affected_detections is sorted/unique, or empty when a
failure concerns the whole observation.

Failure precedence: structural input/resource admission; geometry/context support;
limits/cancellation; identification ambiguity/no-match; final geometry/residual;
conditional uncertainty/precision. A reached cancellation check takes precedence
over publishing success. Check cancellation before/after each hypothesis and at
least every 1024 comparisons. No deadline or hard-real-time latency is guaranteed.
External resource/dependency exceptions are caught and mapped to typed errors;
std::bad_alloc is ResourceLimitExceeded when an Error can be constructed;
an allocation failure while constructing that error may propagate std::bad_alloc
to C++ and maps to ABI status 3. No API can guarantee allocating a failure value
after exhaustion. No C ABI exception escape is permitted.

## Results and conditional covariance

```cpp
struct StarMatch {
    DetectionId detection_id;
    std::string catalog_star_id;
    double angular_residual_rad;
};
struct ConditionalUncertainty {
    Mat3 covariance_enu_rad2;
    // Always conditional on accepted IDs, robust weights and fixed nuisance inputs.
};
struct SearchReport {
    std::string algorithm_version;
    uint32_t hypotheses_evaluated;
    uint64_t candidate_comparisons;
};
struct ModelReport {
    std::string model_id;
    std::string earth_orientation_status; // observed or predicted
    std::string refraction_status; // modeled or disabled
    std::vector<std::string> limitations;
};
struct AttitudeSolution {
    ObservationIdentity identity;
    Rotation3 R_enu_from_camera;
    ObserverPosition observer_used;
    std::optional<ConditionalUncertainty> conditional_uncertainty;
    double fit_rms_rad, fit_max_residual_rad;
    std::vector<StarMatch> matches;
    std::vector<DetectionId> rejected_detections;
    std::string catalog_content_id, astrometry_content_id;
    std::string implementation_version;
    SearchReport search;
    ModelReport models;
};
```

Matches and rejected IDs partition the input, sorted by detection ID. Residuals are
atan2(norm(cross(observed,predicted)),dot(observed,predicted)) in radians. RMS is
unweighted over final inliers; the fit can use their independent measurement noise.

With all covariances supplied, covariance is the inverse local tangent information
matrix for accepted correspondences and final robust weights, with nuisance inputs
fixed. It excludes selection uncertainty, catalog/time/EOP/position/refraction and
shared calibration errors. It is not rescaled from fit RMS and has no invented
floor. Singular information yields UnobservableGeometry. With no measurement
covariances, use equal weights and return null covariance; residual does not supply
missing noise. Requested conditional sigma compares sqrt(max eigenvalue) and returns
UncertaintyUnavailable if absent or ConditionalPrecisionNotMet if above the bound.

V1 removes maximum_attitude_sigma_rad, PropagatedInputBudget and any total-accuracy
success promise. All results carry limitation code total_accuracy_not_evaluated.
Timing sigma is retained as input metadata, not secretly propagated. Downstream
fusion must not use conditional covariance as its entire sensor noise model.
There is no total-uncertainty API until shared/model-error representation is designed.

## Compatibility

This revision is the design baseline, not a claim of binary availability. Breaking
contract changes require a new documented revision before first release and a new
major API/schema version afterward. C++ source compatibility is supported; C++ ABI
is not promised. The stable C surface is the small versioned JSON/handle boundary
in [C_ABI.md](C_ABI.md), not a duplicate set of public native structs.

## Experimental additive workflow

See [native extraction](STAR_EXTRACTION.md), [calibration sessions](CALIBRATION_WORKFLOW.md)
and the public `extraction.hpp` / `calibration.hpp` headers. Existing solve types
and behavior remain unchanged. The new workflow remains physically unqualified.
