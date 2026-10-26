# Camera alignment for iPhone

A small SwiftUI example for measuring rear-camera and accelerometer alignment.
Choose **Stars** or **Printed target**, prepare the app, then press **Start**.
You have 15 seconds to position the phone. A tone starts measurement; another
announces completion. With **Full alignment with guided tilts** enabled (the
default), intermediate tones ask you to reposition the phone. Secure it, press
Ready for next pose, and wait through a five-second settling countdown. The result
table includes unavailable cameras and explains missing evidence.

The normal budget is 120 seconds from Start, with a 240-second watchdog including
analysis. Each camera gets a share of the remaining normal budget and at most
12 attempts per block. Full alignment seeks two usable frames per camera at each
of three star poses or four target poses. The optional one-pose check seeks six
frames per camera. Repositioning time counts toward the hard limit. Downloads and permissions happen before
Start. Real-device speed and physical accuracy have not been qualified.

| Mode | Scene | Results |
| --- | --- | --- |
| Stars | Clear stars visible through each rear lens, three distinct tilts | Full accelerometer-to-camera and camera-to-camera rotation |
| Printed target | The supplied fixed flat board, four distinct phone poses | Full rotations, with board-frame gravity jointly estimated |

## Build

Use Xcode with the iOS 16+ SDK and an installed simulator runtime, CMake, Ninja,
and Python 3.9+. From the repository root:

```sh
python3 tools/fetch_dependencies.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j12
ctest --test-dir build --output-on-failure -j12
sh tools/build_apple.sh
python3 examples/ios-alignment/prepare.py
python3 examples/ios-alignment/generate_target.py
python3 examples/ios-alignment/generate_scene.py
open examples/ios-alignment/Alignment.xcodeproj
```

Select the **AlignmentDemo** scheme. The project uses the local Swift package and
locally built XCFrameworks; there is no downloaded app binary or remote package.
The build phase copies generated synthetic fixtures from `build/fixtures`. Keep
that directory available even when building for a phone. Simulator slices match
the build Mac's architecture; select a matching simulator. For an unsigned generic
simulator build, pass `ARCHS=$(uname -m)` to `xcodebuild`. For a phone, select your
own signing team in Xcode. There is no checked-in team ID or signing credential.

Run the unit and UI tests without a phone:

```sh
python3 examples/ios-alignment/test_simulator.py
```

The script creates and removes its own simulator. Simulator runs are visibly
labelled **Synthetic demonstration** and never request camera or location access.
They exercise rendered star/target inputs and shared native alignment fixtures.
The generated target scene lives only in ignored build output.

## Prepare a measurement

**Stars:** press Download sky references, then Prepare. Allow camera, motion and
local location access. The app downloads Hipparcos, Earth-orientation data, the
leap-second list and Bulletin C from fixed public HTTPS URLs. It records source
hashes and conversion provenance locally and passes the result through the native
resource loader. Expired coverage, changed provenance and unsupported leap
announcements fail preparation. Review upstream terms before redistributing any
resource packages; none are included in this repository. Reference data needs a
refresh after the short validity window ends.

**Printed target:** use [target.svg](target.svg), also available through the app's
View / share target button. Print at 100% on A4, verify the 50 mm scale, and mount
it flat. Show several separated markers to all lenses. A modestly tilted view
avoids frontal pose ambiguity; leveling is unnecessary. Keep the board fixed throughout all phone poses. Wide and telephoto views
must both contain enough board area. Move the board before Start if needed; leave
the board fixed and move the phone only when prompted. The app preview helps placement.

After Start, mount the phone during the countdown. Leave it fixed during each
capture block. Follow the tilt prompts between blocks; tilting only about one axis
can leave the calibration unobservable. The app will not invent a missing rotation
or keep asking for moves after its time budget runs out. The app measures wide, ultrawide and telephoto devices that actually exist,
excluding virtual zoom devices and LiDAR. It never substitutes another lens.
Focus/exposure scouting is brief; settings are then locked and reused for that camera across poses.
Bayer RAW is preferred for stars. Unsupported RAW decoding uses a separate
processed video frame with its own timestamp and explicit processed-pixel label.
Target detection uses video frames and requires delivered intrinsics.

## Read the results

Camera pairs show a rotation vector in degrees. It maps vectors from the first
camera into the second camera; components are **not Euler angles**. With diverse poses, accelerometer rows show the three-axis alignment correction
relative to the documented nominal device-axis map. Bias and scale remain
uncalibrated. A one-pose star check gives only gravity mismatch with unresolved
twist; a one-pose target check has no accelerometer reference. Full target mode
jointly estimates the board-frame gravity direction; that is a calibration fit,
not an independent validation of gravity or absolute accuracy.

Details includes coordinate conventions, optical assumptions and repeatability.
FOV-derived optics, omitted distortion/refraction, unknown absolute clock accuracy
and processed pixels remain limitations. No confidence interval is inferred from
residual scatter. See the [alignment API guide](../../docs/ALIGNMENT.md).

Cancellation, backgrounding, camera interruption and serious/critical thermal
pressure stop further acquisition. The watchdog cancels native work and displays
partial evidence. OS scheduling and hardware callback latency are not real-time
guarantees; the app does not extend its budget to improve a result. Unprompted motion within a pose invalidates that pose; movement between guided
poses is expected. Missing IMU data,
insufficient features and ambiguous poses remain unavailable.

## Privacy and dependencies

Frames stay in memory, are released after processing, and are never saved to Photos
or an image file. No observation or location is included in network requests.
There is no analytics service. Public reference packages are cached locally and
excluded from device backup. Export result is an explicit local action with a
privacy warning; treat orientation/device information as potentially location-bearing.

The example adapter pins AprilTag 3.4.5 to SHA-256
`3749f48220db78c29c375a373861f4e1f4138e5dbae3c298584e96cfcc556515`.
Its [BSD-2-Clause license](Adapter/APRILTAG-LICENSE.txt) covers the detector and
family-derived target patterns. `prepare.py` verifies the archive before building
single-worker detection into a separate static framework. AprilTag is not a core
library dependency. Star propagation uses the core's pinned ERFA dependency.

No real-world measurements, calibration coefficients or physical qualification
results are included. The sample is experimental.
