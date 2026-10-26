"""Run after aa_tests generates the standalone synthetic fixture directory."""
import json, sys
from pathlib import Path
from astroattitude import *
native=NativeLibrary(sys.argv[1]);fixtures=Path(sys.argv[2])
with Engine(native,(fixtures/'catalog.json').read_bytes(),(fixtures/'astrometry.json').read_bytes()) as engine:
    for kind,cls in [('rays',RayObservation),('pixels',PixelObservation)]:
        request=json.loads((fixtures/(kind+'.json')).read_text())
        observation=decode(cls,request['observation']);context=decode(EarthContext,request['context']);options=decode(SolveOptions,request['options'])
        result=engine.solve_rays(observation,context,options) if kind=='rays' else engine.solve_pixels(observation,decode(CameraCalibration,request['calibration']),context,options)
        assert isinstance(result,AttitudeSolution),result
        assert result.matches[-1].detection_id==2**64-1
        print(kind,result.fit_rms_rad,result.models.limitations)
    with CancellationToken(native) as token:
        token.request()
        result=engine.solve_pixels(observation,decode(CameraCalibration,request['calibration']),context,options,token)
        assert isinstance(result,SolverError) and result.code=='Cancelled'
engine.close()
try: engine.solve_pixels(observation,decode(CameraCalibration,request['calibration']),context,options)
except RuntimeError: pass
else: raise AssertionError('Use after close succeeded')
