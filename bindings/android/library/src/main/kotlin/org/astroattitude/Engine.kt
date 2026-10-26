package org.astroattitude

import java.io.Closeable
import java.util.concurrent.locks.ReentrantReadWriteLock
import kotlin.concurrent.read
import kotlin.concurrent.write
import kotlinx.serialization.*
import kotlinx.serialization.descriptors.*
import kotlinx.serialization.encoding.*
import kotlinx.serialization.json.*

@Serializable(with = UInt64Codec::class)
@JvmInline value class WireUInt64(val value: ULong)
object UInt64Codec : KSerializer<WireUInt64> {
    override val descriptor = PrimitiveSerialDescriptor("WireUInt64", PrimitiveKind.STRING)
    override fun serialize(encoder: Encoder, value: WireUInt64) = encoder.encodeString(value.value.toString())
    override fun deserialize(decoder: Decoder): WireUInt64 {
        val s = decoder.decodeString(); val v = s.toULong()
        require(v.toString() == s) { "Noncanonical uint64" }; return WireUInt64(v)
    }
}
@Serializable(with = Int64Codec::class)
@JvmInline value class WireInt64(val value: Long)
object Int64Codec : KSerializer<WireInt64> {
    override val descriptor = PrimitiveSerialDescriptor("WireInt64", PrimitiveKind.STRING)
    override fun serialize(encoder: Encoder, value: WireInt64) = encoder.encodeString(value.value.toString())
    override fun deserialize(decoder: Decoder): WireInt64 {
        val s = decoder.decodeString(); val v = s.toLong()
        require(v.toString() == s) { "Noncanonical int64" }; return WireInt64(v)
    }
}
/** JNI/ABI failures are exceptions; solver rejections are SolveOutcome.Failure. */
class NativeFailure(val status: Int, val payload: ByteArray?) : RuntimeException("Native ABI failure: $status")
class ResourceFailure(val error: SolverError) : IllegalArgumentException(error.detail)
sealed interface SolveOutcome {
    data class Success(val solution: AttitudeSolution) : SolveOutcome
    data class Failure(val error: SolverError) : SolveOutcome
}
@Serializable private data class Response(val schema: String, val outcome: String, val solution: AttitudeSolution? = null, val error: SolverError? = null)
internal object Native {
    init { System.loadLibrary("astroattitude_jni") }
    external fun create(catalog: ByteArray, astrometry: ByteArray): Long
    external fun solve(engine: Long, request: ByteArray, token: Long): ByteArray
    external fun destroy(engine: Long)
    external fun tokenCreate(): Long
    external fun tokenRequest(token: Long)
    external fun tokenDestroy(token: Long)
    external fun extract(pixels: FloatArray, mask: ByteArray, metadata: ByteArray, token: Long): ByteArray
    external fun calibrationCreate(catalog: ByteArray, astrometry: ByteArray, request: ByteArray, restore: Boolean, token: Long): Long
    external fun calibrationCall(session: Long, request: ByteArray, candidate: Long, token: Long, artifact: LongArray): ByteArray
    external fun calibrationDestroy(session: Long)
    external fun artifactDestroy(artifact: Long)
}
internal class Handle(private var pointer: Long, private val destroy: (Long) -> Unit) : Closeable {
    private val lock = ReentrantReadWriteLock(true)
    fun <T> lease(block: (Long) -> T): T = lock.read {
        check(pointer != 0L) { "Handle is closed" }; block(pointer)
    }
    override fun close() = lock.write { if (pointer != 0L) { destroy(pointer); pointer = 0L } }
}
/** One-way cancellation. close waits for calls using this token. */
class CancellationToken : Closeable {
    internal val handle = Handle(Native.tokenCreate(), Native::tokenDestroy)
    fun request() = handle.lease(Native::tokenRequest)
    override fun close() = handle.close()
}
/** Synchronous API: caller chooses executor, dispatcher or calling thread. */
class Engine(catalog: ByteArray, astrometry: ByteArray) : Closeable {
    private val json = Json { explicitNulls = true; encodeDefaults = true }
    private val handle: Handle
    init {
        val pointer = try { Native.create(catalog, astrometry) } catch (e: NativeFailure) {
            if (e.status == 1 && e.payload != null) throw ResourceFailure(json.decodeFromString<Response>(e.payload.decodeToString(throwOnInvalidSequence = true)).error!!)
            throw e
        }
        handle = Handle(pointer, Native::destroy)
    }
    override fun close() = handle.close()
    fun solveRays(observation: RayObservation, context: EarthContext, options: SolveOptions, cancel: CancellationToken? = null): SolveOutcome =
        solve("rays", json.encodeToJsonElement(observation), JsonNull, context, options, cancel)
    fun solvePixels(observation: PixelObservation, calibration: CameraCalibration, context: EarthContext, options: SolveOptions, cancel: CancellationToken? = null): SolveOutcome =
        solve("pixels", json.encodeToJsonElement(observation), json.encodeToJsonElement(calibration), context, options, cancel)
    private fun solve(kind: String, observation: JsonElement, calibration: JsonElement, context: EarthContext, options: SolveOptions, cancel: CancellationToken?): SolveOutcome {
        val ctx = json.encodeToJsonElement(context).jsonObject.toMutableMap()
        if (context.refraction.kind == RefractionKind.disabled) ctx["refraction"] = buildJsonObject { put("kind", "disabled") }
        val request = buildJsonObject {
            put("schema", "astroattitude.solve/1"); put("kind", kind); put("observation", observation)
            put("calibration", calibration); put("context", JsonObject(ctx)); put("options", json.encodeToJsonElement(options))
        }.toString().encodeToByteArray()
        val result = handle.lease { engine ->
            fun call(token: Long) = Native.solve(engine, request, token)
            if (cancel == null) call(0L) else cancel.handle.lease(::call)
        }
        val response = json.decodeFromString<Response>(result.decodeToString(throwOnInvalidSequence = true))
        check(response.schema == "astroattitude.response/1") { "Invalid response schema" }
        return when (response.outcome) {
            "success" -> SolveOutcome.Success(checkNotNull(response.solution))
            "error" -> SolveOutcome.Failure(checkNotNull(response.error))
            else -> error("Invalid response outcome")
        }
    }
}
