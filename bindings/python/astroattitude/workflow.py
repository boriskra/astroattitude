"""Experimental native extraction/calibration. No decoder or numerical fitting here."""
import ctypes as C
import json
import threading
from . import _Handle, _wire, decode, AbiError, ResourceError
from .models import *


def _configure(native):
    signatures = {
        'aa_extract_v1': (C.c_int32, [C.POINTER(C.c_float), C.c_uint64, C.c_char_p, C.c_uint64, C.c_char_p, C.c_uint64, C.c_void_p, C.POINTER(C.c_void_p)]),
        'aa_calibration_create_v1': (C.c_int32, [C.c_char_p,C.c_uint64,C.c_char_p,C.c_uint64,C.c_char_p,C.c_uint64,C.POINTER(C.c_void_p),C.POINTER(C.c_void_p)]),
        'aa_calibration_restore_v1': (C.c_int32, [C.c_char_p,C.c_uint64,C.c_char_p,C.c_uint64,C.c_char_p,C.c_uint64,C.c_void_p,C.POINTER(C.c_void_p),C.POINTER(C.c_void_p)]),
        'aa_calibration_call_v1': (C.c_int32,[C.c_void_p,C.c_char_p,C.c_uint64,C.c_void_p,C.c_void_p,C.POINTER(C.c_void_p),C.POINTER(C.c_void_p)]),
        'aa_calibration_destroy_v1': (None,[C.c_void_p]),
        'aa_calibration_artifact_destroy_v1': (None,[C.c_void_p])}
    for name,(restype,args) in signatures.items():
        fn=getattr(native.lib,name);fn.restype=restype;fn.argtypes=args


def _response(native, status, pointer):
    if status > 1:
        if pointer: native.lib.aa_message_destroy_v1(pointer)
        raise AbiError(status)
    try:
        size=C.c_uint64();p=native.lib.aa_message_bytes_v1(pointer,C.byref(size))
        if not p or size.value>134217728:raise AbiError(4)
        result=json.loads(C.string_at(p,size.value))
    finally:native.lib.aa_message_destroy_v1(pointer)
    if status==1:raise ResourceError(decode(SolverError,result['error']))
    if result.get('schema')!='astroattitude.workflow-response/1':raise AbiError(4)
    return result['value']


def extract_stars(native, pixels, metadata: ExtractionMetadata, mask=None, cancel=None):
    """Input levels and CFA refer to the unrotated, cropped raster. Integer pixel centers."""
    _configure(native)
    if len(pixels)>24000000:raise ValueError('Image sample limit')
    try:
        view=memoryview(pixels)
        if view.nbytes > 24000000 * 4: raise ValueError('Image sample limit')
        if view.format != 'f' or not view.c_contiguous: raise ValueError('Expected contiguous float32 buffer')
        data=(C.c_float*(view.nbytes//4)).from_buffer_copy(view)
    except TypeError:
        data=(C.c_float*len(pixels))(*pixels)
    if mask is not None:
        try: mask_size=memoryview(mask).nbytes
        except TypeError: mask_size=len(mask)
        if mask_size>24000000:raise ValueError('Mask limit')
    mask=b'' if mask is None else bytes(mask)
    if len(mask)>24000000:raise ValueError('Mask limit')
    request={'schema':'astroattitude.extract/1',**_wire(metadata)}
    encoded=json.dumps(request,allow_nan=False).encode();out=C.c_void_p()
    def call(token):return native.lib.aa_extract_v1(data,len(data),mask,len(mask),encoded,len(encoded),token,C.byref(out))
    if cancel is None:status=call(None)
    else:
        with cancel._lease() as token:status=call(token)
    return decode(ExtractionResult,_response(native,status,out))


class CalibrationArtifact(_Handle):
    """Immutable native candidate/report; data is a detached typed value snapshot."""
    def __init__(self,native,pointer,data):
        super().__init__(native,pointer,native.lib.aa_calibration_artifact_destroy_v1)
        self.data=data


class CalibrationSession(_Handle):
    def __init__(self,native,catalog: bytes,astrometry: bytes,configuration: CalibrationConfiguration):
        _configure(native)
        prepared=isinstance(configuration,CalibrationPreparation)
        request=json.dumps({'schema':'astroattitude.calibration-prepare/1' if prepared else 'astroattitude.calibration-create/1','preparation' if prepared else 'configuration':_wire(configuration)},allow_nan=False).encode()
        pointer=C.c_void_p();error=C.c_void_p()
        status=native.lib.aa_calibration_create_v1(catalog,len(catalog),astrometry,len(astrometry),request,len(request),C.byref(pointer),C.byref(error))
        if status:_response(native,status,error)
        super().__init__(native,pointer,native.lib.aa_calibration_destroy_v1);self._serial=threading.RLock()

    @classmethod
    def restore(cls,native,catalog: bytes,astrometry: bytes,journal: str,cancel=None):
        _configure(native);request=journal.encode();pointer=C.c_void_p();error=C.c_void_p()
        if len(request)>134217728:raise ValueError('Journal limit')
        def call(token):return native.lib.aa_calibration_restore_v1(catalog,len(catalog),astrometry,len(astrometry),request,len(request),token,C.byref(pointer),C.byref(error))
        if cancel is None:status=call(None)
        else:
            with cancel._lease() as token:status=call(token)
        if status:_response(native,status,error)
        self=cls.__new__(cls);_Handle.__init__(self,native,pointer,native.lib.aa_calibration_destroy_v1);self._serial=threading.RLock();return self

    def _call(self,operation,value=None,candidate=None,cancel=None,result_type=None):
        request=json.dumps({'schema':'astroattitude.calibration-call/1','operation':operation,'value':_wire(value)},allow_nan=False).encode()
        out=C.c_void_p();artifact=C.c_void_p()
        with self._serial,self._lease() as session:
            def invoke(cand,token):return self._native.lib.aa_calibration_call_v1(session,request,len(request),cand,token,C.byref(artifact),C.byref(out))
            def with_token(cand):
                if cancel is None:return invoke(cand,None)
                with cancel._lease() as token:return invoke(cand,token)
            if candidate is None:status=with_token(None)
            else:
                if candidate._native is not self._native:raise ValueError('Native library mismatch')
                with candidate._lease() as cand:status=with_token(cand)
            try:
                result=_response(self._native,status,out)
                if result_type:result=decode(result_type,result)
                if artifact:return CalibrationArtifact(self._native,artifact,result)
                return result
            except BaseException:
                if artifact:self._native.lib.aa_calibration_artifact_destroy_v1(artifact)
                raise
    def add_frame(self,frame,cancel=None):return self._call('add_frame',frame,cancel=cancel,result_type=IngestReceipt)
    def finish_block(self,block):return self._call('finish_block',block,result_type=BlockReceipt)
    def progress(self):return self._call('progress',result_type=CalibrationProgress)
    def next_action(self):return self._call('next_action',result_type=CalibrationAction)
    def model_diagnostics(self):return self._call('model_diagnostics',result_type=list[OpticalModelDiagnostic])
    def fit(self,cancel=None):return self._call('fit',cancel=cancel,result_type=CandidateData)
    def propose_validation(self,request,cancel=None):return self._call('propose_validation',request,cancel=cancel,result_type=ValidationPlan)
    def freeze(self,candidate,plan):return self._call('freeze',plan,candidate=candidate)
    def validate(self,cancel=None):return self._call('validate',cancel=cancel,result_type=ValidationReport)
    def export_validated(self,cancel=None):return self._call('export_validated',cancel=cancel,result_type=CalibrationPackage)
    def journal(self):return self._call('journal')
    def pause(self):return CalibrationStage(self._call('pause'))
    def resume(self):return CalibrationStage(self._call('resume'))
    def end(self):return CalibrationStage(self._call('end'))


def _alignment_call(native, value, schema, function, result_type, cancel):
    request=json.dumps({'schema':schema,'value':_wire(value)},allow_nan=False).encode()
    if len(request)>6*1048576:raise ValueError('Alignment request budget')
    fn=getattr(native.lib,function)
    fn.restype=C.c_int32;fn.argtypes=[C.c_char_p,C.c_uint64,C.c_void_p,C.POINTER(C.c_void_p)]
    out=C.c_void_p()
    if cancel is None:status=fn(request,len(request),None,C.byref(out))
    else:
        if cancel._native is not native:raise ValueError("Token belongs to another native library")
        with cancel._lease() as token:status=fn(request,len(request),token,C.byref(out))
    return decode(result_type,_response(native,status,out))


def measure_alignment(native, request: AlignmentRequest, cancel=None):
    return _alignment_call(native,request,'astroattitude.alignment/1','aa_alignment_measure_v1',AlignmentReport,cancel)


def solve_planar_target(native, request: PlanarPoseRequest, cancel=None):
    return _alignment_call(native,request,'astroattitude.planar-pose/1','aa_planar_pose_v1',PlanarPose,cancel)


def calibrate_rig_alignment(native, request: RigAlignmentRequest, cancel=None):
    return _alignment_call(native,request,'astroattitude.rig-alignment/1','aa_rig_alignment_v1',AlignmentReport,cancel)
