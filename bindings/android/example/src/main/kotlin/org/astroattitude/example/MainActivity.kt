package org.astroattitude.example
import android.app.Activity
import android.os.Bundle
import android.widget.TextView
import kotlinx.serialization.*
import kotlinx.serialization.json.*
import org.astroattitude.*
class MainActivity : Activity() {
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        val view = TextView(this); view.text = "Running synthetic fixture"; setContentView(view)
        Thread {
            val outcome = runCatching {
                val json = Json { ignoreUnknownKeys = true }
                val request = json.parseToJsonElement(assets.open("rays.json").bufferedReader().readText()).jsonObject
                Engine(assets.open("catalog.json").use { it.readBytes() }, assets.open("astrometry.json").use { it.readBytes() }).use { engine ->
                    val result = engine.solveRays(json.decodeFromJsonElement(request.getValue("observation")), json.decodeFromJsonElement(request.getValue("context")), json.decodeFromJsonElement(request.getValue("options")))
                    check(result is SolveOutcome.Success && result.solution.matches.last().detection_id.value == ULong.MAX_VALUE)
                    "PASS: ${result.solution.matches.size} synthetic matches\n\n" + calibrationGuide(assets)
                }
            }
            runOnUiThread { view.text = outcome.getOrElse { "FAIL: $it" }; android.util.Log.i("Astroattitude", view.text.toString()) }
        }.start()
    }
}
