#include "astroattitude/calibration.hpp"
#include "internal.hpp"
#include "workflow_json.hpp"
#include <fstream>
#include <iostream>
using namespace astroattitude;
template <class T> T value(Outcome<T> v) {
  if (auto e = std::get_if<Error>(&v))
    throw std::runtime_error(e->detail);
  return std::get<T>(std::move(v));
}
void check(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
std::string read(std::string p) {
  std::ifstream f(p);
  return {std::istreambuf_iterator<char>(f), {}};
}
std::span<const std::byte> bytes(const std::string &s) {
  return {reinterpret_cast<const std::byte *>(s.data()), s.size()};
}
int main(int argc, char **argv) {
  try {
    std::string root = argc > 1 ? argv[1] : "build/fixtures";
    auto cs = read(root + "/catalog.json"),
         as = read(root + "/astrometry.json");
    auto cat = value(Catalog::load(bytes(cs)));
    auto ast = value(AstrometryData::load(bytes(as)));
    int64_t t = 1577836800;
    CalibrationConfiguration cfg;
    cfg.session_id = "synthetic-workflow";
    cfg.camera_configuration_id = "camera";
    cfg.physical_camera_id = "physical";
    cfg.pixel_convention_id = "integer-centers";
    cfg.optical_state_id = "locked";
    cfg.width = cfg.height = 2048;
    cfg.unqualified_seed = {1000, 1000, 1024, 1024, 0, 0, 0, 0, 0};
    cfg.association.acceptance = {6, .01, .005, std::nullopt};
    auto s = value(CalibrationSession::create(cat, ast, cfg));
    EarthContext ctx{{0, 0, 0}, NoRefraction{}, EopPolicy::ObservedOnly};
    ObservationIdentity ident{"seed",
                              "camera",
                              "seed",
                              {{t, 0}, .01, std::nullopt},
                              TemporalModel::StationarySingleExposure,
                              "clock"};
    auto refs =
        references(NativeAccess::get(cat), NativeAccess::get(ast), ident, ctx);
    V z = V::Zero();
    for (auto r : refs.rays)
      z += r;
    z.normalize();
    V x = z.cross(V(0, 0, 1)).normalized(), y = z.cross(x);
    M base;
    base.col(0) = x;
    base.col(1) = y;
    base.col(2) = z;
    PinholeBrownModel truth = cfg.unqualified_seed;
    auto frame = [&](std::string block, CaptureRole role, int64_t time, M rot,
                     int ordinal) {
      CalibrationFrame f;
      f.id = block + "-" + std::to_string(ordinal);
      f.pose_id = "fixed";
      f.block_id = block;
      f.camera_configuration_id = "camera";
      f.evidence_digest = "unique-" + f.id;
      f.quality_provenance_id = "synthetic-integrated";
      f.timing_provenance_id = "synthetic-exact";
      f.role = role;
      f.exposure = {{time, 0}, .01, std::nullopt};
      f.earth = ctx;
      auto id = ident;
      id.exposure = f.exposure;
      auto r =
          references(NativeAccess::get(cat), NativeAccess::get(ast), id, ctx);
      for (size_t k = 0; k < r.rays.size(); ++k) {
        V v = rot.transpose() * r.rays[k];
        check(v.z() > 0, "behind camera");
        double xx = v.x() / v.z(), yy = v.y() / v.z(), rr = xx * xx + yy * yy,
               a = 1 + truth.k1 * rr + truth.k2 * rr * rr;
        f.stars.push_back({k == 7 ? UINT64_MAX : k,
                           {truth.fx_px * xx * a + truth.cx_px,
                            truth.fy_px * yy * a + truth.cy_px},
                           std::nullopt,
                           std::nullopt});
      }
      return f;
    };
    auto block = [&](std::string name, CaptureRole role, int64_t start, M r) {
      BlockCompletion b{name, "fixed", role, {start, 0}, {start + 10, 0},
                        {},   {},      {}};
      for (int i = 0; i < 6; ++i) {
        auto f = frame(name, role, start + i, r, i);
        b.planned_frame_ids.push_back(f.id);
        auto receipt = value(s.add_frame(f));
        if (!receipt.admitted)
          throw std::runtime_error(name + " " + receipt.reason);
      }
      auto result = value(s.finish_block(b));
      check(result.admitted == 6, "block admitted count");
    };
    block("train-a", CaptureRole::Training, t,
          base * Eigen::AngleAxisd(-.5, V::UnitY()).toRotationMatrix());
    block("train-b", CaptureRole::Training, t + 30,
          base * Eigen::AngleAxisd(.5, V::UnitY()).toRotationMatrix());
    block("train-c", CaptureRole::Training, t + 60,
          base * Eigen::AngleAxisd(-.5, V::UnitX()).toRotationMatrix());
    block("train-d", CaptureRole::Training, t + 90,
          base * Eigen::AngleAxisd(.5, V::UnitX()).toRotationMatrix());
    block("select", CaptureRole::ModelSelection, t + 120, base);
    check(s.progress().ready_to_fit, "fit readiness");
    auto candidate = value(s.fit());
    check(std::abs(candidate.data().calibration.model.fx_px - 1000) < .01,
          "focal recovery");
    check(!candidate.data().physical_accuracy_qualified,
          "not physical qualification");
    std::ofstream(root + "/workflow-configuration.json")
        << WorkflowJson(cfg).dump();
    auto journal = value(s.journal());
    std::ofstream(root + "/workflow-training.json") << journal;
    auto corrupted = WorkflowJson::parse(journal);
    for (auto &action : corrupted["actions"])
      if (action["op"] == "fit")
        action["candidate"]["calibration"]["model"]["fx_px"] = 999.;
    check(std::holds_alternative<Error>(
              CalibrationSession::restore(cat, ast, corrupted.dump())),
          "tampered candidate rejected");
    auto restored = value(CalibrationSession::restore(cat, ast, journal));
    check(restored.progress().admitted_frames == 30, "journal restore");
    auto guided = value(s.propose_validation({"guided",
                                              "fixed",
                                              "synthetic-integrated",
                                              {t + 200, 0},
                                              {t + 210, 0}}));
    check(guided.slots.size() == 15 && guided.source_panel.size() == 4,
          "guided fixed panel schedule");
    ValidationPlan plan;
    plan.id = "future";
    plan.pose_id = "fixed";
    plan.admission_policy_id = "synthetic-integrated";
    plan.freeze_time = {t + 200, 0};
    for (auto id : {"1", "2", "3", "4"})
      plan.source_panel.push_back(NativeAccess::get(cat).ns + ":" + id);
    plan.slots = {{"future-a", {t + 210, 0}, {t + 230, 0}},
                  {"future-b", {t + 4420, 0}, {t + 4440, 0}}};
    value(s.freeze(candidate, plan));
    check(value(s.validate()).status == "incomplete",
          "missing blocks not pass");
    check(std::holds_alternative<Error>(s.fit()), "no validation fitting");
    auto stale = frame("old", CaptureRole::Reserve, t + 205, base, 0);
    check(std::holds_alternative<Error>(s.add_frame(stale)),
          "no reserve after freeze");
    auto changed_policy =
        frame("future-a", CaptureRole::Validation, t + 210, base, 0);
    changed_policy.quality_provenance_id = "different-extraction-policy";
    check(std::holds_alternative<Error>(s.add_frame(changed_policy)),
          "frozen admission policy");
    block("future-a", CaptureRole::Validation, t + 210, base);
    block("future-b", CaptureRole::Validation, t + 4420, base);
    auto report = value(s.validate());
    check(report.status == "pass", "synthetic fixed north consistency");
    check(report.max_pairwise_change_rad &&
              *report.max_pairwise_change_rad < 1e-6,
          "known north separation");
    check(!report.physical_accuracy_qualified, "no absolute qualification");
    std::ofstream(root + "/workflow-complete.json") << value(s.journal());
    std::ofstream(root + "/workflow-report.json")
        << WorkflowJson(report).dump();
    auto final =
        value(CalibrationSession::restore(cat, ast, value(s.journal())));
    check(final.progress().stage == CalibrationStage::Complete,
          "complete journal state");
    check(std::holds_alternative<Error>(s.export_validated()),
          "unsupported full-field export");
    check(s.next_action().reason == "UnsupportedFieldRegion",
          "coverage guidance");
    auto empty = value(CalibrationSession::create(cat, ast, cfg));
    auto f = frame("failed", CaptureRole::Training, t, base, 0);
    f.stars.clear();
    check(!value(empty.add_frame(f)).admitted, "empty frame retained");
    BlockCompletion failed{
        "failed",    "fixed",           CaptureRole::Training,
        {t, 0},      {t + 10, 0},       {f.id, "missing"},
        {"missing"}, {"capture_failed"}};
    check(value(empty.finish_block(failed)).status == "insufficient",
          "missing capture retained");
    value(empty.finish_block({"empty-selection",
                              "fixed",
                              CaptureRole::ModelSelection,
                              {t + 20, 0},
                              {t + 21, 0},
                              {"missing-selection"},
                              {"missing-selection"},
                              {"clouds"}}));
    check(empty.progress().stage == CalibrationStage::ModelSelection,
          "empty block advances role");
    auto late_training =
        frame("late-training", CaptureRole::Training, t + 30, base, 0);
    check(std::holds_alternative<Error>(empty.add_frame(late_training)),
          "empty block prevents role rollback");
    value(empty.pause());
    check(std::holds_alternative<Error>(empty.add_frame(f)), "pause guard");
    value(empty.resume());
    value(empty.end());
    check(empty.next_action().reason == "SessionEnded", "ended action");
    check(std::holds_alternative<Error>(empty.resume()), "terminal end");
    s = value(CalibrationSession::create(cat, ast, cfg));
    truth = {1010, 1010, 1028, 1021.5, .0015, -.0003, 0, 0, 0};
    for (int i = 0; i < 6; ++i) {
      double ax = (i / 3 ? 1 : -1) * .4, ay = (i % 3 - 1) * .45;
      block("moving-" + std::to_string(i), CaptureRole::Training, t + i * 30,
            base * Eigen::AngleAxisd(ax, V::UnitX()).toRotationMatrix() *
                Eigen::AngleAxisd(ay, V::UnitY()).toRotationMatrix());
    }
    block("nonzero-selection", CaptureRole::ModelSelection, t + 200, base);
    auto recovered = value(s.fit());
    check(s.next_action().kind == "PrepareValidation", "candidate guidance");
    auto model = recovered.data().calibration.model;
    check(std::abs(model.fx_px - truth.fx_px) < .1 &&
              std::abs(model.cx_px - truth.cx_px) < .2 &&
              std::abs(model.cy_px - truth.cy_px) < .2,
          "moving-block forward center/focal recovery");
    check(std::abs(model.k1 - truth.k1) < .0002 &&
              std::abs(model.k2 - truth.k2) < .0002,
          "forward radial recovery");
    auto bad = cfg;
    bad.unqualified_seed.k1 = .2;
    check(std::holds_alternative<Error>(
              CalibrationSession::create(cat, ast, bad)),
          "unsupported/inverse-like coefficients not exportable");
    truth = cfg.unqualified_seed;
    s = value(CalibrationSession::restore(cat, ast, journal));
    auto cf = value(s.fit());
    value(s.freeze(cf, plan));
    check(std::holds_alternative<Error>(s.freeze(cf, plan)),
          "freeze immutable");
    block("future-a", CaptureRole::Validation, t + 210, base);
    V axis = (base.transpose() * V(0, 1, 0)).cross(V::UnitX()).normalized();
    block("future-b", CaptureRole::Validation, t + 4420,
          base * Eigen::AngleAxisd(.01, axis).toRotationMatrix());
    check(value(s.validate()).status == "target_missed",
          "threshold crossing fails");
    auto invalidPlan = plan;
    invalidPlan.slots.back().end = {t + 4000, 0};
    auto shortSession = value(CalibrationSession::restore(cat, ast, journal));
    auto shortCandidate = value(shortSession.fit());
    check(std::holds_alternative<Error>(
              shortSession.freeze(shortCandidate, invalidPlan)),
          "short schedule rejected");
    auto repeat = value(CalibrationSession::create(cat, ast, cfg));
    auto one = frame("dupes", CaptureRole::Training, t, base, 0);
    value(repeat.add_frame(one));
    auto dup = one;
    dup.id = "duplicate-payload";
    dup.exposure.midpoint.unix_seconds += 1;
    check(value(repeat.add_frame(dup)).reason == "RepeatedPayload",
          "duplicate payload retained");
    check(std::holds_alternative<Error>(repeat.add_frame(dup)),
          "duplicate identity rejected");
    dup.id = "changed";
    dup.camera_configuration_id = "different";
    check(std::holds_alternative<Error>(repeat.add_frame(dup)),
          "configuration change rejected");
    // Reserve data must never enter fitting or change a previously fitted
    // model.
    s = value(CalibrationSession::restore(cat, ast, journal));
    auto before_reserve = value(s.fit());
    block("reserve", CaptureRole::Reserve, t + 160,
          base * Eigen::AngleAxisd(.2, V::UnitX()).toRotationMatrix());
    auto after_reserve = value(s.fit());
    check(before_reserve.data().id == after_reserve.data().id,
          "reserve withheld from fit and identity");

    // Six or more associated sources do not replace a missing member of the
    // immutable four-source panel.
    s = value(CalibrationSession::restore(cat, ast, journal));
    auto panel_candidate = value(s.fit());
    value(s.freeze(panel_candidate, plan));
    BlockCompletion panel_block{"future-a",
                                "fixed",
                                CaptureRole::Validation,
                                {t + 210, 0},
                                {t + 230, 0},
                                {},
                                {},
                                {}};
    for (int i = 0; i < 6; ++i) {
      auto f = frame("future-a", CaptureRole::Validation, t + 210 + i, base, i);
      f.stars.erase(f.stars.begin());
      panel_block.planned_frame_ids.push_back(f.id);
      check(value(s.add_frame(f)).admitted,
            "seven-source association retained");
    }
    value(s.finish_block(panel_block));
    block("future-b", CaptureRole::Validation, t + 4420, base);
    auto missing_panel = value(s.validate());
    check(missing_panel.status == "incomplete" &&
              missing_panel.blocks.front().complete_panel_frames == 0,
          "missing fixed panel member prevents pass");

    s = value(CalibrationSession::restore(cat, ast, journal));
    value(s.finish_block({"failed-selection",
                          "fixed",
                          CaptureRole::ModelSelection,
                          {t + 160, 0},
                          {t + 170, 0},
                          {"absent"},
                          {"absent"},
                          {"capture-failed"}}));
    check(std::holds_alternative<Error>(s.fit()),
          "empty failed selection block prevents selection");
    // Concentrated image coverage must expose an ill-conditioned optical
    // family.
    auto weak_cfg = cfg;
    weak_cfg.unqualified_seed.fx_px = weak_cfg.unqualified_seed.fy_px = 10.;
    truth = weak_cfg.unqualified_seed;
    s = value(CalibrationSession::create(cat, ast, weak_cfg));
    block("weak-a", CaptureRole::Training, t, base);
    block("weak-b", CaptureRole::Training, t + 30, base);
    block("weak-select", CaptureRole::ModelSelection, t + 60, base);
    auto weak_fit = s.fit();
    auto weak_models = s.model_diagnostics();
    check(std::any_of(weak_models.begin(), weak_models.end(),
                      [](auto &m) { return m.status == "weak_geometry"; }),
          "profiled optical weak mode exposed");
    std::cout << "Workflow synthetic controls passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
