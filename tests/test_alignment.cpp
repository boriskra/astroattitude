#include "alignment_json.hpp"
#include "astroattitude/alignment.hpp"
#include "astroattitude/astroattitude.h"
#include <Eigen/Dense>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace astroattitude;
void check(bool x) {
  if (!x)
    throw std::runtime_error("alignment assertion");
}
Rotation3 rotation(double a) {
  return {
      {{std::cos(a), -std::sin(a), 0, std::sin(a), std::cos(a), 0, 0, 0, 1}}};
}
int main(int argc, char **argv) {
  try {
    AlignmentRequest q{"world",
                       "monotonic",
                       {"wide", "tele"},
                       {},
                       AlignmentImu{0, 100, {0, 0, -1}, true},
                       Vec3{0, 0, -1},
                       6,
                       false};
    for (int i = 0; i < 6; ++i)
      for (int k = 0; k < 2; ++k)
        q.frames.push_back({std::to_string(i) + std::to_string(k),
                            k ? "tele" : "wide", "world", "synthetic-optics",
                            double(i + 1), double(i + 1.1), rotation(k * .1),
                            rotation(0)});
    auto r = std::get<AlignmentReport>(measure_alignment(q));
    check(r.pairs.size() == 3);
    check(r.pairs[0].status == AlignmentStatus::tilt_only);
    check(!r.pairs[0].R_to_from);
    check(std::abs(r.pairs[1].rotation_vector_rad->z + .1) < 1e-10);
    auto noImu = q;
    noImu.imu.reset();
    check(std::get<AlignmentReport>(measure_alignment(noImu)).pairs[1].status ==
          AlignmentStatus::motion_evidence_unavailable);
    auto flipped = q;
    flipped.imu->down_accelerometer = {0, 0, 1};
    check(std::abs(*std::get<AlignmentReport>(measure_alignment(flipped))
                        .pairs[0]
                        .gravity_mismatch_rad -
                   std::acos(-1)) < 1e-10);
    auto configuration = q;
    configuration.frames[0].optical_provenance = "different";
    check(std::get<AlignmentReport>(measure_alignment(configuration))
              .pairs[0]
              .status == AlignmentStatus::optical_configuration_changed);
    auto no = q;
    no.down_reference.reset();
    check(std::get<AlignmentReport>(measure_alignment(no)).pairs[0].status ==
          AlignmentStatus::gravity_reference_unavailable);
    auto moved = q;
    moved.motion_detected = true;
    check(std::get<AlignmentReport>(measure_alignment(moved)).pairs[1].status ==
          AlignmentStatus::motion_detected);
    auto bad = q;
    bad.frames.push_back(bad.frames.front());
    check(std::holds_alternative<Error>(measure_alignment(bad)));
    bad = q;
    bad.frames[0].start_s = -1;
    check(std::holds_alternative<Error>(measure_alignment(bad)));
    bad = q;
    bad.cameras.push_back("missing");
    check(
        std::get<AlignmentReport>(measure_alignment(bad)).pairs.back().status ==
        AlignmentStatus::insufficient_frames);
    std::atomic_bool cancel = true;
    check(std::get<Error>(measure_alignment(q, {&cancel})).code ==
          ErrorCode::Cancelled);
    PlanarPoseRequest t{640, 480, 500, 500, 320, 240, {}, 2, .1};
    Eigen::Matrix3d R = (Eigen::AngleAxisd(.4, Eigen::Vector3d::UnitY()) *
                         Eigen::AngleAxisd(.2, Eigen::Vector3d::UnitX()))
                            .toRotationMatrix();
    Eigen::Vector3d T(.02, -.01, .7);
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x) {
        Eigen::Vector3d p((x - 1.5) * .06, (y - 1.5) * .06, 0), c = R * p + T;
        t.corners.push_back(
            {uint64_t(y * 4 + x),
             {p.x(), p.y()},
             {500 * c.x() / c.z() + 320, 500 * c.y() / c.z() + 240}});
      }
    t.corners.back().id = UINT64_MAX;
    auto p = solve_planar_target(t);
    if (auto e = std::get_if<Error>(&p))
      throw std::runtime_error(e->detail);
    auto pose = std::get<PlanarPose>(p);
    check(pose.rms_px < 1e-6);
    check(std::abs(pose.translation_camera_m.z - .7) < 1e-7);
    // Translation is independently recovered; it must not masquerade as
    // rotation.
    auto translated = t;
    Eigen::Vector3d otherT(-.08, .03, .85);
    for (auto &corner : translated.corners) {
      Eigen::Vector3d c =
          R * Eigen::Vector3d(corner.target_m.x, corner.target_m.y, 0) + otherT;
      corner.pixel = {500 * c.x() / c.z() + 320, 500 * c.y() / c.z() + 240};
    }
    auto other = std::get<PlanarPose>(solve_planar_target(translated));
    check(std::abs(other.translation_camera_m.x - otherT.x()) < 1e-6);
    for (int i = 0; i < 9; ++i)
      check(std::abs(other.R_camera_from_target.matrix.row_major[i] -
                     pose.R_camera_from_target.matrix.row_major[i]) < 1e-6);
    auto frontal = t;
    for (auto &corner : frontal.corners)
      corner.pixel = {500 * corner.target_m.x / .7 + 320,
                      500 * corner.target_m.y / .7 + 240};
    check(std::holds_alternative<Error>(solve_planar_target(frontal)));
    auto duplicate = t;
    duplicate.corners[1].id = 0;
    check(std::holds_alternative<Error>(solve_planar_target(duplicate)));
    auto line = t;
    for (auto &v : line.corners)
      v.target_m.y = 0;
    check(std::get<Error>(solve_planar_target(line)).code ==
          ErrorCode::UnobservableGeometry);
    check(std::get<Error>(solve_planar_target(t, {&cancel})).code ==
          ErrorCode::Cancelled);
    RigAlignmentRequest rig;
    Eigen::Matrix3d X = (Eigen::AngleAxisd(.025, Eigen::Vector3d::UnitX()) *
                         Eigen::AngleAxisd(-.018, Eigen::Vector3d::UnitY()))
                            .toRotationMatrix();
    auto packed = [](const Eigen::Matrix3d &m) {
      Rotation3 r;
      for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
          r.matrix.row_major[3 * i + j] = m(i, j);
      return r;
    };
    std::vector<Eigen::Vector2d> tilts{{0, 0}, {.4, 0}, {0, .4}, {-.3, -.3}};
    for (size_t i = 0; i < tilts.size(); ++i) {
      AlignmentRequest pose = q;
      double start = 10 + 20 * i;
      pose.imu->start_s = start;
      pose.imu->end_s = start + 10;
      Eigen::Matrix3d B =
          (Eigen::AngleAxisd(tilts[i].x(), Eigen::Vector3d::UnitX()) *
           Eigen::AngleAxisd(tilts[i].y(), Eigen::Vector3d::UnitY()))
              .toRotationMatrix();
      Eigen::Vector3d a =
          X.transpose() * B.transpose() * Eigen::Vector3d(0, 0, -1);
      pose.imu->down_accelerometer = {a.x(), a.y(), a.z()};
      for (size_t k = 0; k < pose.frames.size(); ++k) {
        auto &f = pose.frames[k];
        f.frame_id = "pose" + std::to_string(i) + "-" + f.frame_id;
        f.start_s = start + 1 + k * .1;
        f.end_s = f.start_s + .01;
        f.R_reference_from_camera =
            packed(B * (f.camera_id == "wide"
                            ? Eigen::Matrix3d::Identity()
                            : Eigen::AngleAxisd(.1, Eigen::Vector3d::UnitZ())
                                  .toRotationMatrix()));
      }
      rig.poses.push_back(pose);
    }
    auto calibrated = calibrate_rig_alignment(rig);
    if (auto e = std::get_if<Error>(&calibrated))
      throw std::runtime_error(e->detail);
    auto full = std::get<AlignmentReport>(calibrated);
    if (full.pairs[0].status != AlignmentStatus::rotation_estimated)
      throw std::runtime_error(WorkflowJson(full).dump());
    check(std::abs(full.pairs[0].misalignment_vector_rad->x - .025) < .001);
    RigAlignmentRequest indoor = rig;
    for (auto &pose : indoor.poses)
      pose.down_reference.reset();
    auto indoorResult = calibrate_rig_alignment(indoor);
    if (auto e = std::get_if<Error>(&indoorResult))
      throw std::runtime_error(e->detail);
    auto indoorReport = std::get<AlignmentReport>(indoorResult);
    if (indoorReport.pairs[0].status != AlignmentStatus::rotation_estimated)
      throw std::runtime_error(WorkflowJson(indoorReport).dump());
    check(std::abs(indoorReport.pairs[0].misalignment_vector_rad->x - .025) <
          .001);
    auto weak = rig;
    for (size_t i = 1; i < weak.poses.size(); ++i) {
      weak.poses[i].imu->down_accelerometer =
          weak.poses[0].imu->down_accelerometer;
      for (size_t j = 0; j < weak.poses[i].frames.size(); ++j)
        weak.poses[i].frames[j].R_reference_from_camera =
            weak.poses[0].frames[j].R_reference_from_camera;
    }
    check(std::get<AlignmentReport>(calibrate_rig_alignment(weak))
              .pairs[0]
              .status == AlignmentStatus::pose_diversity_insufficient);
    check(std::get<Error>(calibrate_rig_alignment(rig, {&cancel})).code ==
          ErrorCode::Cancelled);
    auto abi = [&](std::string schema, auto value, auto fn) {
      auto bytes = WorkflowJson{{"schema", schema}, {"value", value}}.dump();
      aa_message *out = nullptr;
      auto status =
          fn((const uint8_t *)bytes.data(), bytes.size(), nullptr, &out);
      check(status == 0);
      uint64_t n;
      auto ptr = aa_message_bytes_v1(out, &n);
      auto j = WorkflowJson::parse(ptr, ptr + n);
      aa_message_destroy_v1(out);
      return j.at("value");
    };
    check(abi("astroattitude.alignment/1", q, aa_alignment_measure_v1) ==
          WorkflowJson(r));
    check(abi("astroattitude.planar-pose/1", t, aa_planar_pose_v1) ==
          WorkflowJson(pose));
    check(abi("astroattitude.rig-alignment/1", rig, aa_rig_alignment_v1) ==
          WorkflowJson(full));
    if (argc == 2) {
      std::filesystem::create_directories(argv[1]);
      std::ofstream(std::string(argv[1]) + "/rig.json") << WorkflowJson(rig);
      std::ofstream(std::string(argv[1]) + "/rig-target.json")
          << WorkflowJson(indoor);
      std::ofstream(std::string(argv[1]) + "/alignment.json")
          << WorkflowJson(q);
      std::ofstream(std::string(argv[1]) + "/alignment-expected.json")
          << WorkflowJson(r);
      std::ofstream(std::string(argv[1]) + "/target.json") << WorkflowJson(t);
      std::ofstream(std::string(argv[1]) + "/target-expected.json")
          << WorkflowJson(pose);
    }
    std::cout << "Alignment and target controls passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
