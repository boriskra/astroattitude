#include "astroattitude/alignment.hpp"
#include "internal.hpp"
#include <map>
namespace astroattitude {
namespace {
void cancelled(Cancellation c) {
  if (c.requested && c.requested->load())
    fail(ErrorCode::Cancelled, "Alignment cancelled");
}
M rotation(Rotation3 r) {
  M m = matrix(r.matrix);
  require(m.allFinite() && (m.transpose() * m - M::Identity()).norm() < 1e-6 &&
              std::abs(m.determinant() - 1) < 1e-6,
          "Expected proper rotation");
  return m;
}
Vec3 value(V v) { return {v.x(), v.y(), v.z()}; }
V unit(Vec3 v) {
  V x = vec(v);
  require(x.allFinite() && x.norm() > .1 && x.norm() < 20,
          "Invalid gravity vector");
  return x.normalized();
}
M closest(M m) {
  Eigen::JacobiSVD<M> s(m, Eigen::ComputeFullU | Eigen::ComputeFullV);
  M d = M::Identity();
  d(2, 2) = (s.matrixU() * s.matrixV().transpose()).determinant();
  return s.matrixU() * d * s.matrixV().transpose();
}
double distance(M a, M b) {
  return Eigen::AngleAxisd(a.transpose() * b).angle();
}
struct Group {
  std::vector<M> rotations;
  M nominal;
  std::set<std::string> provenance;
};
template <class T, class F> Outcome<T> protect(F f) {
  try {
    return f();
  } catch (const Failure &e) {
    return e.error;
  } catch (const std::bad_alloc &) {
    return Error{ErrorCode::ResourceLimitExceeded, "Alignment allocation", {}};
  } catch (...) {
    return Error{ErrorCode::InternalError, "Alignment failed", {}};
  }
}
} // namespace
Outcome<AlignmentReport> measure_alignment(const AlignmentRequest &q,
                                           Cancellation c) {
  return protect<AlignmentReport>([&] {
    cancelled(c);
    identifier(q.reference_id);
    identifier(q.clock_id);
    require(q.cameras.size() > 0 && q.cameras.size() <= 8 &&
                q.frames.size() <= 256,
            "Alignment input budget", ErrorCode::ResourceLimitExceeded);
    require(q.minimum_frames >= 2 && q.minimum_frames <= 32, "Frame minimum");
    if (q.imu)
      require(finite(q.imu->start_s) && finite(q.imu->end_s) &&
                  q.imu->start_s >= 0 && q.imu->end_s > q.imu->start_s,
              "IMU interval");
    V gravity = q.imu ? unit(q.imu->down_accelerometer) : V::Zero();
    if (q.down_reference)
      unit(*q.down_reference);
    std::map<std::string, Group> groups;
    std::set<std::string> ids;
    for (auto &id : q.cameras) {
      identifier(id);
      require(id != "accelerometer" && groups.emplace(id, Group{}).second,
              "Duplicate camera");
    }
    for (auto &f : q.frames) {
      cancelled(c);
      identifier(f.frame_id);
      identifier(f.optical_provenance, 512);
      require(ids.insert(f.frame_id).second, "Duplicate frame");
      require(groups.count(f.camera_id) && f.reference_id == q.reference_id,
              "Frame/reference mismatch");
      require(finite(f.start_s) && finite(f.end_s) && f.start_s >= 0 &&
                  (!q.imu ||
                   (f.start_s >= q.imu->start_s && f.end_s <= q.imu->end_s)) &&
                  f.end_s >= f.start_s,
              "Frame lacks IMU coverage");
      auto &g = groups.at(f.camera_id);
      auto n = rotation(f.R_camera_from_accelerometer_nominal);
      if (!g.rotations.empty())
        require((g.nominal - n).norm() < 1e-8, "Nominal convention changed");
      g.nominal = n;
      g.rotations.push_back(rotation(f.R_reference_from_camera));
      g.provenance.insert(f.optical_provenance);
    }
    std::map<std::string, M> means;
    std::map<std::string, double> spreads;
    for (auto &[id, g] : groups) {
      if (g.rotations.empty())
        continue;
      M sum = M::Zero();
      for (auto &r : g.rotations)
        sum += r;
      auto mean = closest(sum);
      double spread = 0;
      for (auto &a : g.rotations)
        for (auto &b : g.rotations) {
          cancelled(c);
          spread = std::max(spread, distance(a, b));
        }
      means[id] = mean;
      spreads[id] = spread;
    }
    auto status = [&](const std::string &id) {
      if (!q.imu)
        return AlignmentStatus::motion_evidence_unavailable;
      if (q.motion_detected || !q.imu->stationary)
        return AlignmentStatus::motion_detected;
      if (groups.at(id).rotations.size() < q.minimum_frames)
        return AlignmentStatus::insufficient_frames;
      if (groups.at(id).provenance.size() != 1)
        return AlignmentStatus::optical_configuration_changed;
      if (spreads.at(id) > .02)
        return AlignmentStatus::inconsistent_frames;
      return AlignmentStatus::estimated;
    };
    AlignmentReport out;
    for (size_t i = 0; i < q.cameras.size(); ++i) {
      auto &a = q.cameras[i];
      auto &g = groups.at(a);
      AlignmentPair p;
      p.from_sensor = "accelerometer";
      p.to_sensor = a;
      p.to_frames = g.rotations.size();
      p.status = status(a);
      p.limitations = {"absolute_accuracy_unqualified",
                       "accelerometer_bias_and_scale_unqualified"};
      if (p.status == AlignmentStatus::estimated) {
        p.pose_count = 1;
        if (!q.down_reference)
          p.status = AlignmentStatus::gravity_reference_unavailable;
        else {
          p.status = AlignmentStatus::tilt_only;
          p.gravity_mismatch_rad =
              angle(g.nominal * gravity,
                    means.at(a).transpose() * unit(*q.down_reference));
          p.maximum_repeatability_rad = spreads.at(a);
          p.limitations.push_back("rotation_about_gravity_unobservable");
        }
      }
      for (auto &s : g.provenance)
        p.limitations.push_back(s);
      out.pairs.push_back(p);
      for (size_t j = i + 1; j < q.cameras.size(); ++j) {
        auto &b = q.cameras[j];
        AlignmentPair pair;
        pair.from_sensor = a;
        pair.to_sensor = b;
        pair.from_frames = g.rotations.size();
        pair.to_frames = groups.at(b).rotations.size();
        pair.status = status(a);
        if (pair.status == AlignmentStatus::estimated)
          pair.status = status(b);
        pair.limitations = {"absolute_accuracy_unqualified",
                            "fixed_pose_assumption",
                            "repeatability_is_not_accuracy"};
        if (pair.status == AlignmentStatus::estimated) {
          pair.pose_count = 1;
          M r = means.at(b).transpose() * means.at(a);
          pair.R_to_from = Rotation3{mat(r)};
          Eigen::AngleAxisd aa(r);
          pair.rotation_vector_rad = value(aa.angle() * aa.axis());
          pair.maximum_repeatability_rad =
              std::max(spreads.at(a), spreads.at(b));
        }
        for (auto &s : g.provenance)
          pair.limitations.push_back(a + ":" + s);
        for (auto &s : groups.at(b).provenance)
          pair.limitations.push_back(b + ":" + s);
        out.pairs.push_back(pair);
      }
    }
    return out;
  });
}
Outcome<AlignmentReport>
calibrate_rig_alignment(const RigAlignmentRequest &request, Cancellation c) {
  return protect<AlignmentReport>([&] {
    cancelled(c);
    require(!request.poses.empty() && request.poses.size() <= 6,
            "Need 1..6 stationary poses", ErrorCode::ResourceLimitExceeded);
    const auto &first = request.poses.front();
    std::vector<AlignmentReport> reports;
    std::set<std::string> frame_ids;
    double previous_end = -1;
    for (const auto &pose : request.poses) {
      require(pose.cameras == first.cameras &&
                  pose.reference_id == first.reference_id &&
                  pose.clock_id == first.clock_id,
              "Rig reference or camera set changed");
      require(pose.down_reference.has_value() ==
                  first.down_reference.has_value(),
              "Gravity reference changed");
      if (pose.down_reference)
        require(angle(unit(*pose.down_reference), unit(*first.down_reference)) <
                    1e-8,
                "Gravity reference changed");
      if (pose.imu) {
        require(pose.imu->start_s > previous_end, "Pose intervals overlap");
        previous_end = pose.imu->end_s;
      }
      for (const auto &f : pose.frames)
        require(frame_ids.insert(f.frame_id).second,
                "Frame reused across poses");
      auto report = measure_alignment(pose, c);
      if (auto error = std::get_if<Error>(&report))
        throw Failure{*error};
      reports.push_back(std::get<AlignmentReport>(report));
    }
    AlignmentReport output = reports.front();
    for (size_t pair_index = 0; pair_index < output.pairs.size();
         ++pair_index) {
      auto &out = output.pairs[pair_index];
      out.R_to_from.reset();
      out.rotation_vector_rad.reset();
      out.gravity_mismatch_rad.reset();
      out.maximum_repeatability_rad.reset();
      out.from_frames = out.to_frames = out.pose_count = 0;
      if (out.from_sensor != "accelerometer") {
        M sum = M::Zero();
        std::vector<M> estimates;
        for (const auto &report : reports) {
          const auto &pair = report.pairs[pair_index];
          if (pair.status != AlignmentStatus::estimated)
            continue;
          M r = rotation(*pair.R_to_from);
          sum += r;
          estimates.push_back(r);
          out.from_frames += pair.from_frames;
          out.to_frames += pair.to_frames;
        }
        out.pose_count = estimates.size();
        out.status = AlignmentStatus::insufficient_frames;
        if (estimates.empty())
          continue;
        double spread = 0;
        for (const auto &a : estimates)
          for (const auto &b : estimates)
            spread = std::max(spread, distance(a, b));
        out.maximum_repeatability_rad = spread;
        if (spread > .02) {
          out.status = AlignmentStatus::inconsistent_frames;
          continue;
        }
        M mean = closest(sum);
        out.R_to_from = Rotation3{mat(mean)};
        Eigen::AngleAxisd aa(mean);
        out.rotation_vector_rad = value(aa.angle() * aa.axis());
        out.status = AlignmentStatus::estimated;
        out.limitations.push_back("between_pose_repeatability_not_accuracy");
        continue;
      }
      std::vector<M> camera;
      std::vector<V> acceleration;
      M nominal = M::Identity();
      std::set<std::string> optics;
      for (size_t i = 0; i < request.poses.size(); ++i) {
        const auto &pose = request.poses[i];
        const auto &pair = reports[i].pairs[pair_index];
        if (pair.status != AlignmentStatus::tilt_only &&
            pair.status != AlignmentStatus::gravity_reference_unavailable)
          continue;
        M sum = M::Zero();
        size_t count = 0;
        for (const auto &frame : pose.frames)
          if (frame.camera_id == out.to_sensor) {
            M n = rotation(frame.R_camera_from_accelerometer_nominal);
            if (!camera.empty() || count)
              require((nominal - n).norm() < 1e-8,
                      "Nominal axes changed between poses");
            nominal = n;
            sum += rotation(frame.R_reference_from_camera);
            ++count;
            optics.insert(frame.optical_provenance);
          }
        camera.push_back(closest(sum));
        acceleration.push_back(unit(pose.imu->down_accelerometer));
        out.to_frames += count;
      }
      out.pose_count = camera.size();
      const bool known = first.down_reference.has_value();
      out.limitations = {"absolute_accuracy_unqualified",
                         "accelerometer_bias_and_scale_unqualified",
                         "multi_pose_rotation_only",
                         "repeatability_is_not_accuracy"};
      for (auto &s : optics)
        out.limitations.push_back(s);
      if (optics.size() > 1) {
        out.status = AlignmentStatus::optical_configuration_changed;
        continue;
      }
      if (camera.size() < (known ? 3u : 4u)) {
        out.status = AlignmentStatus::pose_diversity_insufficient;
        continue;
      }
      out.limitations.push_back(known ? "independent_reference_gravity"
                                      : "target_gravity_jointly_estimated");
      auto exp = [](V w) -> M {
        return w.norm() > 0 ? Eigen::AngleAxisd(w.norm(), w.normalized())
                                  .toRotationMatrix()
                            : M::Identity();
      };
      auto residual = [&](M r, V g) {
        Eigen::VectorXd e(3 * camera.size());
        for (size_t i = 0; i < camera.size(); ++i)
          e.segment<3>(3 * i) = camera[i] * r * acceleration[i] - g;
        return e;
      };
      auto tangent = [](V g) {
        V axis = std::abs(g.x()) < .8 ? V::UnitX() : V::UnitY();
        V u = g.cross(axis).normalized();
        return std::pair<V, V>{u, g.cross(u)};
      };
      auto jacobian = [&](M r, V g, const Eigen::VectorXd &e) {
        Eigen::MatrixXd j(e.size(), known ? 3 : 5);
        for (int k = 0; k < 3; ++k) {
          V w = V::Zero();
          w[k] = 1e-6;
          j.col(k) = (residual(exp(w) * r, g) - e) / 1e-6;
        }
        if (!known) {
          auto [u, v] = tangent(g);
          j.col(3) = (residual(r, (g + 1e-6 * u).normalized()) - e) / 1e-6;
          j.col(4) = (residual(r, (g + 1e-6 * v).normalized()) - e) / 1e-6;
        }
        return j;
      };
      struct Fit {
        M r;
        V g;
        double cost;
        Eigen::MatrixXd jac;
      };
      std::vector<Fit> fits;
      for (V perturb : std::vector<V>{{0, 0, 0},
                                      {.2, 0, 0},
                                      {-.2, 0, 0},
                                      {0, .2, 0},
                                      {0, -.2, 0},
                                      {0, 0, .2},
                                      {0, 0, -.2}}) {
        M r = exp(perturb) * nominal;
        V g = V::Zero();
        if (known)
          g = unit(*first.down_reference);
        else {
          for (size_t i = 0; i < camera.size(); ++i)
            g += camera[i] * r * acceleration[i];
          require(g.norm() > .1, "Inconsistent gravity directions");
          g.normalize();
        }
        double lambda = 1e-3;
        auto e = residual(r, g);
        for (int it = 0; it < 100; ++it) {
          cancelled(c);
          auto j = jacobian(r, g, e);
          Eigen::MatrixXd normal = j.transpose() * j;
          normal.diagonal().array() += lambda;
          Eigen::VectorXd d = normal.ldlt().solve(-j.transpose() * e);
          if (!d.allFinite())
            break;
          M nr = exp(d.head<3>()) * r;
          V ng = g;
          if (!known) {
            auto [u, v] = tangent(g);
            ng = (g + d[3] * u + d[4] * v).normalized();
          }
          auto ne = residual(nr, ng);
          if (ne.squaredNorm() < e.squaredNorm()) {
            r = nr;
            g = ng;
            e = ne;
            lambda = std::max(1e-9, lambda / 3);
            if (d.norm() < 1e-10)
              break;
          } else
            lambda = std::min(1e9, lambda * 10);
        }
        fits.push_back({r, g, e.squaredNorm(), jacobian(r, g, e)});
      }
      std::sort(fits.begin(), fits.end(),
                [](const Fit &a, const Fit &b) { return a.cost < b.cost; });
      const auto &best = fits.front();
      Eigen::JacobiSVD<Eigen::MatrixXd> svd(best.jac);
      const auto values = svd.singularValues();
      double ratio = std::pow(values.tail<1>()[0] / values[0], 2);
      out.geometry_ratio = ratio;
      if (ratio < 1e-4) {
        out.status = AlignmentStatus::pose_diversity_insufficient;
        continue;
      }
      bool ambiguous = false;
      for (const auto &fit : fits)
        if (fit.cost <= best.cost + 1e-8 && distance(fit.r, best.r) > .01)
          ambiguous = true;
      if (ambiguous) {
        out.status = AlignmentStatus::ambiguous_alignment;
        continue;
      }
      double maximum = 0;
      for (size_t i = 0; i < camera.size(); ++i)
        maximum = std::max(maximum,
                           angle(camera[i] * best.r * acceleration[i], best.g));
      out.maximum_gravity_residual_rad = maximum;
      // This local alignment example assumes the nominal frame mapping is
      // right. Large corrections signal a convention/input problem, not
      // calibration.
      if (maximum > .02 || distance(best.r, nominal) > .35) {
        out.status = AlignmentStatus::fit_inconsistent;
        continue;
      }
      out.R_to_from = Rotation3{mat(best.r)};
      Eigen::AngleAxisd aa(best.r), correction(best.r * nominal.transpose());
      out.rotation_vector_rad = value(aa.angle() * aa.axis());
      out.misalignment_vector_rad =
          value(correction.angle() * correction.axis());
      out.status = AlignmentStatus::rotation_estimated;
    }
    return output;
  });
}
Outcome<PlanarPose> solve_planar_target(const PlanarPoseRequest &q,
                                        Cancellation c) {
  return protect<PlanarPose>([&] {
    cancelled(c);
    require(q.corners.size() >= 8 && q.corners.size() <= 128,
            "Need 8..128 corners", ErrorCode::InsufficientObservations);
    require(q.width > 0 && q.height > 0 && q.width <= 16384 &&
                q.height <= 16384,
            "Raster size");
    require(finite(q.fx_px) && finite(q.fy_px) && q.fx_px >= 1 &&
                q.fy_px >= 1 && q.fx_px <= 1e6 && q.fy_px <= 1e6 &&
                finite(q.cx_px) && finite(q.cy_px) && q.cx_px >= 0 &&
                q.cx_px < q.width && q.cy_px >= 0 && q.cy_px < q.height,
            "Target intrinsics");
    require(finite(q.maximum_rms_px) && q.maximum_rms_px > 0 &&
                q.maximum_rms_px <= 10 && finite(q.ambiguity_tolerance_px) &&
                q.ambiguity_tolerance_px > 0 &&
                q.ambiguity_tolerance_px <= q.maximum_rms_px,
            "Target tolerances");
    std::set<uint64_t> ids;
    std::vector<Eigen::Vector2d> obj, img;
    Eigen::Vector2d center = Eigen::Vector2d::Zero();
    for (auto &p : q.corners) {
      require(ids.insert(p.id).second, "Duplicate corner");
      Eigen::Vector2d x(p.target_m.x, p.target_m.y), y(p.pixel.x, p.pixel.y);
      require(x.allFinite() && x.norm() < 10 && y.allFinite() && y.x() >= 0 &&
                  y.x() < q.width && y.y() >= 0 && y.y() < q.height,
              "Invalid corner");
      obj.push_back(x);
      img.push_back(y);
      center += x;
    }
    center /= obj.size();
    double scale = 0;
    Eigen::Matrix2d coverage = Eigen::Matrix2d::Zero();
    for (auto &p : obj) {
      auto d = p - center;
      scale += d.squaredNorm();
      coverage += d * d.transpose();
    }
    scale = std::sqrt(scale / obj.size());
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> cov(coverage);
    require(scale > 1e-5 && cov.eigenvalues()(0) > cov.eigenvalues()(1) * .02,
            "Target coverage is nearly collinear",
            ErrorCode::UnobservableGeometry);
    Eigen::MatrixXd a(2 * obj.size(), 9);
    for (size_t i = 0; i < obj.size(); ++i) {
      auto p = (obj[i] - center) / scale;
      double x = (img[i].x() - q.cx_px) / q.fx_px,
             y = (img[i].y() - q.cy_px) / q.fy_px;
      a.row(2 * i) << -p.x(), -p.y(), -1, 0, 0, 0, x * p.x(), x * p.y(), x;
      a.row(2 * i + 1) << 0, 0, 0, -p.x(), -p.y(), -1, y * p.x(), y * p.y(), y;
    }
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(a, Eigen::ComputeFullV);
    require(svd.singularValues()(7) > 1e-7, "Degenerate homography",
            ErrorCode::UnobservableGeometry);
    auto h = svd.matrixV().col(8);
    M H;
    H << h(0), h(1), h(2), h(3), h(4), h(5), h(6), h(7), h(8);
    if (H(2, 2) < 0)
      H = -H;
    double k = 2 / (H.col(0).norm() + H.col(1).norm());
    M raw;
    raw.col(0) = k * H.col(0);
    raw.col(1) = k * H.col(1);
    raw.col(2) = raw.col(0).cross(raw.col(1));
    M initial = closest(raw);
    V trans = k * H.col(2) * scale - initial * V(center.x(), center.y(), 0);
    struct Fit {
      M r;
      V t;
      double rms;
    };
    auto residual = [&](M r, V t) {
      Eigen::VectorXd e(2 * obj.size());
      for (size_t i = 0; i < obj.size(); ++i) {
        V p = r * V(obj[i].x(), obj[i].y(), 0) + t;
        if (p.z() <= 1e-5) {
          e.setConstant(1e8);
          return e;
        }
        e(2 * i) = q.fx_px * p.x() / p.z() + q.cx_px - img[i].x();
        e(2 * i + 1) = q.fy_px * p.y() / p.z() + q.cy_px - img[i].y();
      }
      return e;
    };
    std::vector<Fit> fits;
    // Reflection of the normal about the target-centre viewing ray seeds the
    // competing weak-perspective solution, in addition to local perturbations.
    V bearing = (initial * V(center.x(), center.y(), 0) + trans).normalized();
    V normal = initial.col(2),
      reflected = 2 * bearing.dot(normal) * bearing - normal;
    M reflected_rotation = Eigen::Quaterniond::FromTwoVectors(normal, reflected)
                               .toRotationMatrix() *
                           initial;
    std::vector<M> starts{initial, reflected_rotation};
    for (V perturb :
         std::vector<V>{{.5, 0, 0}, {-.5, 0, 0}, {0, .5, 0}, {0, -.5, 0}})
      starts.push_back(Eigen::AngleAxisd(perturb.norm(), perturb.normalized())
                           .toRotationMatrix() *
                       initial);
    // Multiple bounded starts expose planar pose alternatives; close
    // alternatives with meaningfully different rotations are reported as
    // ambiguous.
    for (M r : starts) {
      V t = trans;
      double lambda = 1e-3;
      auto e = residual(r, t);
      for (int it = 0; it < 40; ++it) {
        cancelled(c);
        Eigen::MatrixXd jac(e.size(), 6);
        for (int j = 0; j < 6; ++j) {
          V d = V::Zero();
          d[j % 3] = 1e-6;
          auto other = j < 3 ? residual(Eigen::AngleAxisd(1e-6, d.normalized())
                                                .toRotationMatrix() *
                                            r,
                                        t)
                             : residual(r, t + d);
          jac.col(j) = (other - e) / 1e-6;
        }
        Eigen::Matrix<double, 6, 6> normal = jac.transpose() * jac;
        normal.diagonal().array() += lambda;
        Eigen::Matrix<double, 6, 1> delta =
            normal.ldlt().solve(-jac.transpose() * e);
        if (!delta.allFinite())
          break;
        V w = delta.head<3>();
        M next = r;
        if (w.norm() > 0)
          next =
              Eigen::AngleAxisd(w.norm(), w.normalized()).toRotationMatrix() *
              r;
        V nt = t + delta.tail<3>();
        auto ne = residual(next, nt);
        if (ne.squaredNorm() < e.squaredNorm()) {
          r = next;
          t = nt;
          e = ne;
          lambda = std::max(1e-9, lambda / 3);
          if (delta.norm() < 1e-9)
            break;
        } else
          lambda = std::min(1e9, lambda * 10);
      }
      fits.push_back({r, t, std::sqrt(e.squaredNorm() / obj.size())});
    }
    std::sort(fits.begin(), fits.end(),
              [](auto &a, auto &b) { return a.rms < b.rms; });
    auto best = fits.front();
    require(best.rms <= q.maximum_rms_px, "Target reprojection limit",
            ErrorCode::ResidualLimitExceeded);
    for (auto &f : fits)
      require(!(f.rms <= best.rms + q.ambiguity_tolerance_px &&
                distance(f.r, best.r) > .01),
              "Ambiguous planar pose", ErrorCode::AmbiguousMatch);
    V view = (best.r * V(center.x(), center.y(), 0) + best.t).normalized();
    require(std::abs(best.r.col(2).dot(view)) < std::cos(.08),
            "Target is too frontal to resolve tilt conservatively",
            ErrorCode::UnobservableGeometry);
    // Frontal, tiny targets cannot reliably disambiguate the plane normal.
    double xlo = 1e20, xhi = -1e20, ylo = 1e20, yhi = -1e20;
    for (auto p : img) {
      xlo = std::min(xlo, p.x());
      xhi = std::max(xhi, p.x());
      ylo = std::min(ylo, p.y());
      yhi = std::max(yhi, p.y());
    }
    require((xhi - xlo) * (yhi - ylo) > q.width * double(q.height) * .01,
            "Target too small", ErrorCode::UnobservableGeometry);
    return PlanarPose{
        {mat(best.r)}, value(best.t), best.rms, uint32_t(obj.size())};
  });
}
} // namespace astroattitude
