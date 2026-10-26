#pragma once
#include "astroattitude/astroattitude.hpp"
#include <Eigen/Dense>
#include <algorithm>
#include <bit>
#include <limits>
#include <cmath>
#include <erfa.h>
#include <nlohmann/json.hpp>
#include <numeric>
#include <set>
namespace astroattitude {
#ifdef AA_TEST_HOOKS
namespace testing { extern std::atomic_bool *candidate_seen; extern std::atomic_bool *release_candidate; }
#endif
static_assert(sizeof(void*)==8 && sizeof(double)==8 && std::numeric_limits<double>::is_iec559 && std::endian::native==std::endian::little,"astroattitude v1 requires a 64-bit little-endian IEEE binary64 platform");
using V = Eigen::Vector3d;
using M = Eigen::Matrix3d;
using J = nlohmann::json;
constexpr double pi = 3.14159265358979323846;
struct Star {
  std::string id;
  double ra, dec, pmr, pmd, px, rv, mag;
  bool motion, parallax, radial;
  V icrs;
};
struct Leap {
  int64_t utc;
  int offset;
};
struct Eop {
  int64_t utc;
  double dut, xp, yp;
  bool predicted;
};
struct Catalog::Impl {
  std::string id, ns;
  int64_t from, until;
  size_t bytes;
  std::vector<Star> stars;
};
struct AstrometryData::Impl {
  std::string id;
  int64_t from, until, leap_until;
  size_t bytes;
  std::vector<Leap> leaps;
  std::vector<Eop> eop;
};
struct NativeAccess {
  static const Catalog::Impl &get(const Catalog &c) { return *c.impl_; }
  static const AstrometryData::Impl &get(const AstrometryData &d) {
    return *d.impl_;
  }
};
struct Triangle {
  std::array<uint32_t, 3> ids;
};
struct Engine::Impl {
  Catalog catalog;
  AstrometryData data;
  std::vector<Triangle> triangles;
};
struct Failure {
  Error error;
};
[[noreturn]] inline void fail(ErrorCode code, std::string detail) {
  throw Failure{{code, std::move(detail), {}}};
}
inline void require(bool ok, const char *msg,
                    ErrorCode c = ErrorCode::InvalidInput) {
  if (!ok)
    fail(c, msg);
}
inline bool finite(double x) { return std::isfinite(x); }
inline V vec(Vec3 v) { return {v.x, v.y, v.z}; }
inline Mat3 mat(const M &m) {
  Mat3 r;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      r.row_major[3 * i + j] = m(i, j);
  return r;
}
inline M matrix(const Mat3 &m) {
  M r;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      r(i, j) = m.row_major[3 * i + j];
  return r;
}
inline double angle(const V &a, const V &b) {
  return std::atan2(a.cross(b).norm(), a.dot(b));
}
inline M skew(const V &v) {
  M m;
  m << 0, -v.z(), v.y(), v.z(), 0, -v.x(), -v.y(), v.x(), 0;
  return m;
}
inline void identifier(const std::string &s, size_t max = 128) {
  require(!s.empty() && s.size() <= max, "Invalid identifier");
  for (unsigned char c : s)
    require(c >= 32 && c <= 126, "Identifier must be printable ASCII");
}
J parse_json(std::span<const std::byte>, size_t, size_t, size_t);
void keys(const J &, std::initializer_list<const char *>);
double number(const J &);
int64_t integer(const J &);
std::string string(const J &, size_t = 128);
std::string digest(std::span<const std::byte>);
struct References {
  std::vector<V> rays;
  std::vector<bool> visible;
  bool predicted;
};
References references(const Catalog::Impl &, const AstrometryData::Impl &,
                      const ObservationIdentity &, const EarthContext &,
                      Cancellation cancel = {nullptr});
void validate_identity(const ObservationIdentity &);
struct SpatialIndex {
  struct Node {
    uint32_t id;
    int left = -1, right = -1, axis = 0;
  };
  const std::vector<V> &points;
  std::vector<Node> nodes;
  std::vector<uint32_t> order;
  int root = -1;
  explicit SpatialIndex(const std::vector<V> &p) : points(p) {
    order.resize(p.size());
    std::iota(order.begin(), order.end(), 0);
    nodes.reserve(p.size());
    root = build(0, order.size(), 0);
  }
  int build(size_t lo, size_t hi, int d) {
    if (lo == hi)
      return -1;
    size_t mid = (lo + hi) / 2;
    int axis = d % 3;
    std::nth_element(order.begin() + lo, order.begin() + mid,
                     order.begin() + hi, [&](auto a, auto b) {
                       return points[a][axis] < points[b][axis] ||
                              (points[a][axis] == points[b][axis] && a < b);
                     });
    int n = nodes.size();
    nodes.push_back({order[mid], -1, -1, axis});
    int l = build(lo, mid, d + 1), r = build(mid + 1, hi, d + 1);
    nodes[n].left = l;
    nodes[n].right = r;
    return n;
  }
  template <class F, class G>
  void radius(int n, const V &q, double r2, F &&f, G &&visit) const {
    if (n < 0)
      return;
    visit();
    auto node = nodes[n];
    V delta = points[node.id] - q;
    if ((delta.array().square() <= r2).all())
      f(node.id, delta.squaredNorm());
    double d = q[node.axis] - points[node.id][node.axis];
    radius(d < 0 ? node.left : node.right, q, r2, f, visit);
    if (d * d <= r2)
      radius(d < 0 ? node.right : node.left, q, r2, f, visit);
  }
};
} // namespace astroattitude
