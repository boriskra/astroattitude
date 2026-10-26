package org.astroattitude

import java.io.Closeable
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock
import kotlinx.serialization.*
import kotlinx.serialization.json.*

private val workflowJson = Json { explicitNulls = true; encodeDefaults = true }
private inline fun <T> workflowNative(call: () -> T): T = try { call() } catch (e: NativeFailure) {
    if (e.status == 1 && e.payload != null) {
        val obj = workflowJson.parseToJsonElement(e.payload.decodeToString(throwOnInvalidSequence = true)).jsonObject
        throw ResourceFailure(workflowJson.decodeFromJsonElement<SolverError>(obj.getValue("error")))
    }; throw e
}
private fun workflowValue(bytes: ByteArray): JsonElement {
    val response = workflowJson.parseToJsonElement(bytes.decodeToString(throwOnInvalidSequence = true)).jsonObject
    check(response["schema"]?.jsonPrimitive?.content == "astroattitude.workflow-response/1" && response["outcome"]?.jsonPrimitive?.content == "success")
    return response.getValue("value")
}
private fun <T> withToken(cancel: CancellationToken?, call: (Long) -> T): T = if (cancel == null) call(0L) else cancel.handle.lease(call)
/** Caller supplies linear pixels. No decoding, display transform or camera access. */
object StarExtractor {
    fun extract(pixels: FloatArray, metadata: ExtractionMetadata, mask: ByteArray = byteArrayOf(), cancel: CancellationToken? = null): ExtractionResult {
        require(pixels.size <= 24000000 && mask.size <= 24000000)
        val obj = workflowJson.encodeToJsonElement(metadata).jsonObject.toMutableMap(); obj["schema"] = JsonPrimitive("astroattitude.extract/1")
        val request = JsonObject(obj).toString().encodeToByteArray()
        return withToken(cancel) { token -> workflowJson.decodeFromJsonElement(workflowValue(workflowNative { Native.extract(pixels, mask, request, token) })) }
    }
}
/** Immutable native candidate/report; data is a detached snapshot. */
class CalibrationArtifact<T> internal constructor(pointer: Long, val data: T): Closeable {
    internal val handle = Handle(pointer, Native::artifactDestroy)
    override fun close() = handle.close()
}
/** Synchronous calls are serialized; close waits for active native calls. */
class CalibrationSession private constructor(pointer: Long): Closeable {
    private val handle = Handle(pointer, Native::calibrationDestroy)
    private val serial = ReentrantLock()
    constructor(catalog: ByteArray, astrometry: ByteArray, configuration: CalibrationConfiguration): this(workflowNative {
        val request = buildJsonObject { put("schema", "astroattitude.calibration-create/1"); put("configuration", workflowJson.encodeToJsonElement(configuration)) }.toString().encodeToByteArray()
        Native.calibrationCreate(catalog, astrometry, request, false, 0L)
    })
    constructor(catalog: ByteArray, astrometry: ByteArray, preparation: CalibrationPreparation): this(workflowNative {
        val request = buildJsonObject { put("schema", "astroattitude.calibration-prepare/1"); put("preparation", workflowJson.encodeToJsonElement(preparation)) }.toString().encodeToByteArray()
        Native.calibrationCreate(catalog, astrometry, request, false, 0L)
    })
    companion object {
        fun restore(catalog: ByteArray, astrometry: ByteArray, journal: String, cancel: CancellationToken? = null): CalibrationSession {
            val request = journal.encodeToByteArray(); require(request.size <= 134217728)
            return withToken(cancel) { token -> CalibrationSession(workflowNative { Native.calibrationCreate(catalog, astrometry, request, true, token) }) }
        }
    }
    override fun close() = handle.close()
    private fun call(operation: String, value: JsonElement = JsonNull, candidate: CalibrationArtifact<CandidateData>? = null, cancel: CancellationToken? = null): Pair<JsonElement, Long> {
        val request = buildJsonObject { put("schema", "astroattitude.calibration-call/1"); put("operation", operation); put("value", value) }.toString().encodeToByteArray()
        return serial.withLock { handle.lease { session ->
            fun invoke(cand: Long): Pair<JsonElement, Long> = withToken(cancel) { token ->
                val artifact = LongArray(1)
                try { val bytes = workflowNative { Native.calibrationCall(session, request, cand, token, artifact) }; Pair(workflowValue(bytes), artifact[0]) }
                catch (e: Throwable) { if (artifact[0] != 0L) Native.artifactDestroy(artifact[0]); throw e }
            }
            if (candidate == null) invoke(0L) else candidate.handle.lease(::invoke)
        } }
    }
    private inline fun <reified T> result(operation: String, value: JsonElement = JsonNull, cancel: CancellationToken? = null): T = workflowJson.decodeFromJsonElement(call(operation, value, cancel = cancel).first)
    private inline fun <reified T> artifact(operation: String, cancel: CancellationToken?): CalibrationArtifact<T> {
        val (data,pointer) = call(operation, cancel = cancel); check(pointer != 0L)
        try { return CalibrationArtifact(pointer, workflowJson.decodeFromJsonElement<T>(data)) }
        catch (e: Throwable) { Native.artifactDestroy(pointer); throw e }
    }
    fun addFrame(frame: CalibrationFrame, cancel: CancellationToken? = null): IngestReceipt = result("add_frame", workflowJson.encodeToJsonElement(frame), cancel)
    fun finishBlock(block: BlockCompletion): BlockReceipt = result("finish_block", workflowJson.encodeToJsonElement(block))
    fun progress(): CalibrationProgress = result("progress")
    fun nextAction(): CalibrationAction = result("next_action")
    fun modelDiagnostics(): List<OpticalModelDiagnostic> = result("model_diagnostics")
    fun fit(cancel: CancellationToken? = null): CalibrationArtifact<CandidateData> = artifact("fit",cancel)
    fun proposeValidation(request: ValidationRequest, cancel: CancellationToken? = null): ValidationPlan = result("propose_validation", workflowJson.encodeToJsonElement(request),cancel)
    fun freeze(candidate: CalibrationArtifact<CandidateData>, plan: ValidationPlan): String = call("freeze",workflowJson.encodeToJsonElement(plan),candidate).first.jsonPrimitive.content
    fun validate(cancel: CancellationToken? = null): CalibrationArtifact<ValidationReport> = artifact("validate",cancel)
    fun exportValidated(cancel: CancellationToken? = null): CalibrationPackage = result("export_validated",cancel=cancel)
    fun journal(): String = result("journal")
    fun pause(): CalibrationStage = result("pause")
    fun resume(): CalibrationStage = result("resume")
    fun end(): CalibrationStage = result("end")
}
