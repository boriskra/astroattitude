#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
namespace astroattitude {
using DetectionId = uint64_t;
struct Vec2 {
  double x, y;
};
struct Vec3 {
  double x, y, z;
};
struct Mat2 {
  std::array<double, 4> row_major;
};
struct Mat3 {
  std::array<double, 9> row_major;
};
struct Rotation3 {
  Mat3 matrix;
};

enum class ErrorCode {
  InvalidInput,
  InvalidResource,
  UnsupportedVersion,
  CalibrationMismatch,
  OutsideCalibrationDomain,
  UnsupportedObservation,
  InsufficientObservations,
  AstrometryDataUnavailable,
  NoMatch,
  AmbiguousMatch,
  UnobservableGeometry,
  ResidualLimitExceeded,
  UncertaintyUnavailable,
  ConditionalPrecisionNotMet,
  ResourceLimitExceeded,
  Cancelled,
  InternalError
};
struct Error {
  ErrorCode code;
  std::string detail; // Non-normative; branch on code.
  std::vector<DetectionId> affected_detections;
};
template <class T> using Outcome = std::variant<T, Error>;

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

struct PinholeBrownModel {
  double fx_px, fy_px, cx_px, cy_px;
  double k1, k2, k3, p1, p2;
};
struct PixelBounds {
  double min_x, min_y, max_x, max_y;
};
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

class Catalog {
public:
  struct Impl;

private:
  friend class Engine;
  friend struct NativeAccess;
  std::shared_ptr<const Impl> impl_;
  explicit Catalog(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}

public:
  static Outcome<Catalog> load(std::span<const std::byte> json);
  std::string_view content_id() const;
};
class AstrometryData {
public:
  struct Impl;

private:
  friend class Engine;
  friend struct NativeAccess;
  std::shared_ptr<const Impl> impl_;
  explicit AstrometryData(std::shared_ptr<const Impl> p)
      : impl_(std::move(p)) {}

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
struct SolveOptions {
  AcceptanceCriteria acceptance;
  SolveLimits limits;
};
struct Cancellation {
  const std::atomic_bool *requested; // Optional null; borrowed through call.
};
struct AttitudeSolution;
class Engine {
public:
  struct Impl;

private:
  friend struct NativeAccess;
  std::shared_ptr<const Impl> impl_;
  explicit Engine(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}

public:
  static Outcome<Engine> create(Catalog catalog, AstrometryData astrometry);
  Outcome<AttitudeSolution> solve(const PixelObservation &,
                                  const CameraCalibration &,
                                  const EarthContext &, const SolveOptions &,
                                  Cancellation cancel = {nullptr}) const;
  Outcome<AttitudeSolution> solve(const RayObservation &, const EarthContext &,
                                  const SolveOptions &,
                                  Cancellation cancel = {nullptr}) const;
};

struct StarMatch {
  DetectionId detection_id;
  std::string catalog_star_id;
  double angular_residual_rad;
};
struct ConditionalUncertainty {
  Mat3 covariance_enu_rad2;
  // Always conditional on accepted IDs, robust weights and fixed nuisance
  // inputs.
};
struct SearchReport {
  std::string algorithm_version;
  uint32_t hypotheses_evaluated;
  uint64_t candidate_comparisons;
};
struct ModelReport {
  std::string model_id;
  std::string earth_orientation_status; // observed or predicted
  std::string refraction_status;        // modeled or disabled
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

} // namespace astroattitude
