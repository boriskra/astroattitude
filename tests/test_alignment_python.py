import json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'bindings/python'))
from astroattitude import NativeLibrary,decode,CancellationToken,ResourceError
from astroattitude.models import AlignmentRequest,PlanarPoseRequest
from astroattitude.workflow import measure_alignment,solve_planar_target
native=NativeLibrary(sys.argv[1]);root=Path(sys.argv[2])
request=decode(AlignmentRequest,json.loads((root/'alignment.json').read_text()))
r=measure_alignment(native,request)
assert r.pairs[0].status=='tilt_only' and r.pairs[0].R_to_from is None
assert abs(r.pairs[1].rotation_vector_rad.z+.1)<1e-9
p=solve_planar_target(native,decode(PlanarPoseRequest,json.loads((root/'target.json').read_text())))
assert p.rms_px<1e-6 and abs(p.translation_camera_m.z-.7)<1e-7
print('Python alignment and target parity passed')
for operation, value in [(measure_alignment, request),(solve_planar_target,decode(PlanarPoseRequest,json.loads((root/'target.json').read_text())))]:
    with CancellationToken(native) as token:
        token.request()
        try: operation(native,value,cancel=token)
        except ResourceError as e: assert e.error.code.value=='Cancelled'
        else: raise AssertionError('cancellation ignored')
from dataclasses import replace
request=replace(request,imu=None)
assert measure_alignment(native,request).pairs[0].status=='motion_evidence_unavailable'

from astroattitude.models import RigAlignmentRequest
from astroattitude.workflow import calibrate_rig_alignment
for name in ['rig.json','rig-target.json']:
    full=calibrate_rig_alignment(native,decode(RigAlignmentRequest,json.loads((root/name).read_text())))
    assert full.pairs[0].status=='rotation_estimated'
    assert abs(full.pairs[0].misalignment_vector_rad.x-.025)<.001
