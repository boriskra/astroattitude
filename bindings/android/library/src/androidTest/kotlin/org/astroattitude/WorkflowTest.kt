package org.astroattitude
import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.serialization.*
import kotlinx.serialization.json.*
import org.junit.Test
import org.junit.Assert.*
@Serializable private data class ExtractionFixture(val pixels:List<Float>,val metadata:ExtractionMetadata,val expected:ExtractionResult)
class WorkflowTest {
 @Test fun nativeParity() {
  val assets=InstrumentationRegistry.getInstrumentation().context.assets
  fun bytes(name:String)=assets.open(name).use{it.readBytes()}
  val json=Json
  val ex=json.decodeFromString<ExtractionFixture>(bytes("extraction.json").decodeToString())
  val found=StarExtractor.extract(ex.pixels.toFloatArray(),ex.metadata)
  assertEquals(ex.expected.policy_id,found.policy_id);assertEquals(1,found.stars.size)
  assertEquals(ex.expected.stars[0].pixel.x,found.stars[0].pixel.x,1e-6)
  CancellationToken().use{token->token.request();try{StarExtractor.extract(ex.pixels.toFloatArray(),ex.metadata,cancel=token);fail("cancel")}catch(e:ResourceFailure){assertEquals(ErrorCode.Cancelled,e.error.code)}}
  CalibrationSession.restore(bytes("catalog.json"),bytes("astrometry.json"),bytes("workflow-training.json").decodeToString()).use{s->
   assertEquals(30u,s.progress().admitted_frames)
   s.fit().use{candidate->
    assertEquals(1000.0,candidate.data.calibration.model.fx_px,.01)
    val plan=s.proposeValidation(ValidationRequest("guided","fixed","synthetic-integrated",UtcInstant(WireInt64(1577837000),0u),UtcInstant(WireInt64(1577837010),0u)))
    s.freeze(candidate,plan);s.validate().use{assertEquals("incomplete",it.data.status)}
   }
  }
  val s=CalibrationSession.restore(bytes("catalog.json"),bytes("astrometry.json"),bytes("workflow-complete.json").decodeToString())
  s.validate().use{r->val expected=json.decodeFromString<ValidationReport>(bytes("workflow-report.json").decodeToString());assertEquals("pass",r.data.status);assertEquals(expected.candidate_id,r.data.candidate_id)}
  s.close();s.close();try{s.progress();fail("closed")}catch(_:IllegalStateException){}
 }
}
