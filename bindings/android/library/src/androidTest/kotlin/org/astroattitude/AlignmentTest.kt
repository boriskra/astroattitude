package org.astroattitude

import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.serialization.*
import kotlinx.serialization.json.*
import org.junit.Test
import org.junit.Assert.*

class AlignmentTest {
    @Test fun sharedNativeMeasurements() {
        val assets = InstrumentationRegistry.getInstrumentation().context.assets
        fun text(name: String) = assets.open(name).use { it.readBytes().decodeToString() }
        val input = Json.decodeFromString<AlignmentRequest>(text("alignment.json"))
        val result = Alignment.measure(input)
        assertEquals(AlignmentStatus.tilt_only, result.pairs[0].status)
        assertNull(result.pairs[0].R_to_from)
        assertEquals(-0.1, result.pairs[1].rotation_vector_rad!!.z, 1e-9)
        val target = Json.decodeFromString<PlanarPoseRequest>(text("target.json"))
        val pose = Alignment.targetPose(target)
        assertTrue(pose.rms_px < 1e-6)
        assertEquals(0.7, pose.translation_camera_m.z, 1e-7)
        for (name in listOf("rig.json","rig-target.json")) {
            val full=Alignment.calibrateRig(Json.decodeFromString<RigAlignmentRequest>(text(name)))
            assertEquals(AlignmentStatus.rotation_estimated,full.pairs[0].status)
            assertEquals(0.025,full.pairs[0].misalignment_vector_rad!!.x,0.001)
        }
        CancellationToken().use { token ->
            token.request()
            try { Alignment.measure(input, token); fail("cancellation ignored") }
            catch (e: ResourceFailure) { assertEquals(ErrorCode.Cancelled, e.error.code) }
            try { Alignment.targetPose(target, token); fail("cancellation ignored") }
            catch (e: ResourceFailure) { assertEquals(ErrorCode.Cancelled, e.error.code) }
        }
        assertEquals(AlignmentStatus.motion_evidence_unavailable, Alignment.measure(input.copy(imu=null)).pairs[0].status)
    }
}
