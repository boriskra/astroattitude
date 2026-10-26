"""Shared native journal and linear-pixel controls through the Python/C adapter."""
import ctypes as C
import json
import math
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'bindings/python'))
from astroattitude import *
from astroattitude.workflow import *
from astroattitude import _wire
lib=NativeLibrary(sys.argv[1]);root=Path(sys.argv[2])
c=(root/'catalog.json').read_bytes();a=(root/'astrometry.json').read_bytes()
config=decode(CalibrationConfiguration,json.loads((root/'workflow-configuration.json').read_text()))
prep=CalibrationPreparation('prepared','camera','physical','integer-centers','locked',2048,2048,2*math.atan(2048/2000),0)
with CalibrationSession(lib,c,a,prep) as prepared:
    assert prepared.next_action().reason=='FixedViewNeedsMoreEvidence'
with CalibrationSession(lib,c,a,config) as session:
    assert session.progress().frames==0
    assert session.next_action().reason=='FixedViewNeedsMoreEvidence'
    assert session.pause()==CalibrationStage.paused
    assert session.resume()==CalibrationStage.mapping
    assert session.end()==CalibrationStage.ended
try:session.progress();raise AssertionError('closed session')
except RuntimeError:pass
with CalibrationSession.restore(lib,c,a,(root/'workflow-training.json').read_text()) as session:
    assert session.progress().admitted_frames==30
    with session.fit() as candidate:
        assert abs(candidate.data.calibration.model.fx_px-1000)<.01
        plan=session.propose_validation(ValidationRequest('guided','fixed','synthetic-integrated',UtcInstant(1577837000,0),UtcInstant(1577837010,0)))
        assert len(plan.source_panel)==4 and len(plan.slots)==15
        session.freeze(candidate,plan)
        with session.validate() as report:assert report.data.status=='incomplete'
with CalibrationSession.restore(lib,c,a,(root/'workflow-complete.json').read_text()) as session:
    with session.validate() as report:
        expected=json.loads((root/'workflow-report.json').read_text())
        assert report.data.candidate_id==expected['candidate_id']
        assert report.data.status=='pass' and report.data.max_pairwise_change_rad<1e-6
        assert not report.data.physical_accuracy_qualified
with CancellationToken(lib) as token:
    token.request()
    try:CalibrationSession.restore(lib,c,a,(root/'workflow-complete.json').read_text(),token);raise AssertionError('cancel')
    except ResourceError as e:assert e.error.code==ErrorCode.Cancelled
w=h=64;cx,cy=30.25,32.65
pixels=[]
for y in range(h):
    for x in range(w):
        value=100+.02*x+.01*y
        for i in range(12):
            for j in range(12):value+=500*math.exp(-.5*(((x+(i+.5)/12-.5-cx)/1.3)**2+((y+(j+.5)/12-.5-cy)/1.8)**2))/144
        pixels.append(value)
options=ExtractionOptions(5,3,128,64,60,16777216,536870912,2000000000)
metadata=ExtractionMetadata(w,h,w,SampleLayout.rggb,[0]*4,[65535]*4,options)
r=extract_stars(lib,pixels,metadata)
assert len(r.stars)==1 and math.hypot(r.stars[0].pixel.x-cx,r.stars[0].pixel.y-cy)<.01
assert r.stars[0].centroid_covariance_px2 is None
fixture={'metadata':_wire(metadata),'pixels':pixels,'expected':_wire(r)}
(root/'extraction.json').write_text(json.dumps(fixture))
try:extract_stars(lib,pixels[:-1],metadata);raise AssertionError('short buffer')
except ResourceError as e:assert e.error.code==ErrorCode.InvalidInput
with CancellationToken(lib) as token:
    token.request()
    try:extract_stars(lib,pixels,metadata,cancel=token);raise AssertionError('cancel')
    except ResourceError as e:assert e.error.code==ErrorCode.Cancelled
# Overflowing uint32 options must not wrap into a valid small iteration limit.
request={'schema':'astroattitude.extract/1',**_wire(metadata)};request['options']['maximum_fit_iterations']=2**32+60
raw=json.dumps(request).encode();data=(C.c_float*len(pixels))(*pixels);out=C.c_void_p()
status=lib.lib.aa_extract_v1(data,len(data),None,0,raw,len(raw),None,C.byref(out));assert status==1;lib.lib.aa_message_destroy_v1(out)
print('Python/C extraction, workflow, journal, identities, lifetime and cancellation controls passed')
