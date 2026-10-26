package org.astroattitude
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Test
import org.junit.Assert.*
import org.junit.runner.RunWith
import kotlinx.serialization.*
import kotlinx.serialization.json.*
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
@RunWith(AndroidJUnit4::class)
class ConformanceTest {
    private val assets = InstrumentationRegistry.getInstrumentation().context.assets
    private val json = Json { ignoreUnknownKeys = true }
    private fun bytes(name: String) = assets.open(name).use { it.readBytes() }
    @Test fun realSolverAndLifetime() {
        val request = json.parseToJsonElement(bytes("rays.json").decodeToString()).jsonObject
        val obs = json.decodeFromJsonElement<RayObservation>(request.getValue("observation"))
        val context = json.decodeFromJsonElement<EarthContext>(request.getValue("context"))
        val options = json.decodeFromJsonElement<SolveOptions>(request.getValue("options"))
        repeat(20) {
            val engine = Engine(bytes("catalog.json"),bytes("astrometry.json"))
            val result = engine.solveRays(obs,context,options) as SolveOutcome.Success
            assertEquals(ULong.MAX_VALUE,result.solution.matches.last().detection_id.value)
            assertTrue(result.solution.fit_rms_rad < 1e-10)
            val expected = json.parseToJsonElement(bytes("expected.json").decodeToString()).jsonObject.getValue("solution").jsonObject
            val truth = json.decodeFromJsonElement<AttitudeSolution>(expected)
            assertEquals(truth.matches.map { it.catalog_star_id },result.solution.matches.map { it.catalog_star_id })
            assertEquals(truth.models.limitations,result.solution.models.limitations)
            truth.R_enu_from_camera.matrix.row_major.zip(result.solution.R_enu_from_camera.matrix.row_major).forEach { (a,b) -> assertEquals(a,b,1e-10) }
            CancellationToken().use { token -> token.request(); assertEquals(ErrorCode.Cancelled,(engine.solveRays(obs,context,options,token) as SolveOutcome.Failure).error.code) }
            val pool=Executors.newFixedThreadPool(3); val start=CountDownLatch(1)
            val calls=(0..1).map { pool.submit { start.await(); try { engine.solveRays(obs,context,options) } catch (_: IllegalStateException) {} } }
            start.countDown(); engine.close(); calls.forEach { it.get(10,TimeUnit.SECONDS) }; pool.shutdown()
            engine.close()
            try { engine.solveRays(obs,context,options); fail("Closed engine") } catch (_: IllegalStateException) {}
        }
        Engine(bytes("catalog.json"),bytes("astrometry.json")).use { engine ->
            val p=json.parseToJsonElement(bytes("pixels.json").decodeToString()).jsonObject
            assertTrue(engine.solvePixels(json.decodeFromJsonElement(p.getValue("observation")),json.decodeFromJsonElement(p.getValue("calibration")),context,options) is SolveOutcome.Success)
        }
    }
    @Test fun byteHandlingAndBoundaries() {
        val handle=Native.create(bytes("catalog.json"),bytes("astrometry.json"))
        try {
            for (input in listOf(byteArrayOf(0),byteArrayOf(0xc0.toByte(),0xaf.toByte()),"{}\u0000".encodeToByteArray())) {
                val response=json.parseToJsonElement(Native.solve(handle,input,0).decodeToString()).jsonObject
                assertEquals("error",response.getValue("outcome").jsonPrimitive.content)
            }
            val request=json.parseToJsonElement(bytes("rays.json").decodeToString()).jsonObject.toMutableMap()
            val observation=request.getValue("observation").jsonObject.toMutableMap()
            val ids=listOf(0uL,Long.MAX_VALUE.toULong(),Long.MAX_VALUE.toULong()+1u,ULong.MAX_VALUE)
            observation["stars"]=JsonArray(observation.getValue("stars").jsonArray.mapIndexed { i,s -> JsonObject(s.jsonObject.toMutableMap().apply { put("id",JsonPrimitive(if(i<4) ids[i].toString() else (i+100).toString())) }) })
            request["observation"]=JsonObject(observation)
            val response=json.parseToJsonElement(Native.solve(handle,JsonObject(request).toString().encodeToByteArray(),0).decodeToString()).jsonObject
            assertEquals("success",response.getValue("outcome").jsonPrimitive.content)
        } finally { Native.destroy(handle) }
    }

    @Test fun reportSyntheticPerformance() {
        val request=json.parseToJsonElement(bytes("rays.json").decodeToString()).jsonObject
        val observation=json.decodeFromJsonElement<RayObservation>(request.getValue("observation"))
        val context=json.decodeFromJsonElement<EarthContext>(request.getValue("context"))
        val options=json.decodeFromJsonElement<SolveOptions>(request.getValue("options"))
        Engine(bytes("catalog.json"),bytes("astrometry.json")).use { engine ->
            repeat(100) { check(engine.solveRays(observation,context,options) is SolveOutcome.Success) }
            val timings=LongArray(1000) {
                val start=System.nanoTime();check(engine.solveRays(observation,context,options) is SolveOutcome.Success);System.nanoTime()-start
            }.sorted()
            android.util.Log.i("Astroattitude", "8-star Kotlin/JNI synthetic timing: n=1000 p95_ms=${timings[949]/1e6} p99_ms=${timings[989]/1e6}; emulator, not R1")
        }
    }
}
