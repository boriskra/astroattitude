package org.astroattitude.example
import android.content.res.AssetManager
import org.astroattitude.CalibrationSession

/** Offline guide: no phone acquisition, automatic freeze, or lens coefficients. */
fun calibrationGuide(assets: AssetManager): String {
    fun bytes(name: String) = assets.open(name).use { it.readBytes() }
    return CalibrationSession.restore(bytes("catalog.json"), bytes("astrometry.json"), bytes("workflow-training.json").decodeToString()).use { session ->
        val progress = session.progress()
        val gaps = progress.occupied_cells.count { it < 6u }
        val action = session.nextAction()
        val next = when (action.kind) {
            "PrepareValidation" -> "Plan and freeze fresh validation before any validation exposures."
            "FitCandidate" -> "Fit a diagnostic candidate from completed training and selection blocks."
            "CaptureBurst" -> "Collect the scheduled fixed-view block when acquisition is authorized."
            "SaveCalibration" -> "Request validated export; domain and coverage gates still apply."
            else -> "Review the evidence limitation: ${action.reason}."
        }
        "Stage: ${progress.stage}. ${progress.admitted_frames}/${progress.frames} frames admitted in ${progress.blocks} blocks.\n" +
        "Coverage: $gaps of 12 regions have fewer than six source observations.\nNext: $next\n" +
        "Offline example. Absolute accuracy is unqualified."
    }
}
