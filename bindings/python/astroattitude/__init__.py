"""Typed, synchronous ctypes adapter. Caller supplies library and resource bytes."""
import ctypes as C
import json
import threading
from contextlib import contextmanager
from dataclasses import asdict, fields, is_dataclass
from typing import Union, get_args, get_origin
from .models import *

class AbiError(RuntimeError):
    def __init__(self, status):
        self.status = status
        super().__init__('Native ABI failure: %s' % status)

class ResourceError(ValueError):
    def __init__(self, error: SolverError):
        self.error = error
        super().__init__(error.detail)

def decode(cls, value):
    """Decode a documented value type (also useful for offline fixture files)."""
    origin = get_origin(cls)
    if origin is Union:
        return None if value is None else decode(get_args(cls)[0], value)
    if origin is list:
        return [decode(get_args(cls)[0], x) for x in value]
    if is_dataclass(cls):
        return cls(**{f.name: decode(f.type, value.get(f.name)) for f in fields(cls)})
    if isinstance(cls,type) and issubclass(cls,Enum): return cls(value)
    if cls is int:
        return int(value)
    return value

def _wire(value):
    if is_dataclass(value):
        result = {}
        for f in fields(value):
            v = getattr(value, f.name)
            if isinstance(value, Refraction) and f.name == 'parameters' and v is None:
                continue
            if f.name in ('id', 'detection_id') and isinstance(v, int):
                if not 0 <= v <= 2**64-1: raise ValueError('Detection ID outside uint64')
                v = str(v)
            elif f.name == 'unix_seconds':
                if not -(2**63) <= v < 2**63: raise ValueError('Time outside int64')
                v = str(v)
            else: v = _wire(v)
            result[f.name] = v
        return result
    if isinstance(value, (tuple, list)): return [_wire(v) for v in value]
    if isinstance(value, dict): return {k:_wire(v) for k,v in value.items()}
    return value

class NativeLibrary:
    def __init__(self, path):
        self.lib = C.CDLL(str(path))
        signatures = {
            'aa_abi_version': (C.c_uint32, []),
            'aa_engine_create_v1': (C.c_int32,[C.c_char_p,C.c_uint64,C.c_char_p,C.c_uint64,C.POINTER(C.c_void_p),C.POINTER(C.c_void_p)]),
            'aa_engine_destroy_v1': (None,[C.c_void_p]),
            'aa_solve_v1': (C.c_int32,[C.c_void_p,C.c_char_p,C.c_uint64,C.c_void_p,C.POINTER(C.c_void_p)]),
            'aa_message_bytes_v1': (C.c_void_p,[C.c_void_p,C.POINTER(C.c_uint64)]),
            'aa_message_destroy_v1': (None,[C.c_void_p]),
            'aa_cancel_create_v1': (C.c_int32,[C.POINTER(C.c_void_p)]),
            'aa_cancel_request_v1': (None,[C.c_void_p]),
            'aa_cancel_destroy_v1': (None,[C.c_void_p])}
        for name,(restype,args) in signatures.items():
            fn=getattr(self.lib,name);fn.restype=restype;fn.argtypes=args
        if self.lib.aa_abi_version() < 1: raise AbiError(2)
    def _message(self, handle):
        try:
            size=C.c_uint64();ptr=self.lib.aa_message_bytes_v1(handle,C.byref(size))
            if not ptr or size.value > 262144: raise AbiError(4)
            return json.loads(C.string_at(ptr,size.value))
        finally: self.lib.aa_message_destroy_v1(handle)

class _Handle:
    def __init__(self, native, pointer, destroy):
        self._native=native;self._pointer=pointer;self._destroy=destroy
        self._condition=threading.Condition();self._active=0;self._closing=False
    @contextmanager
    def _lease(self):
        with self._condition:
            if self._closing: raise RuntimeError('Handle is closed')
            self._active+=1
        try: yield self._pointer
        finally:
            with self._condition:
                self._active-=1;self._condition.notify_all()
    def close(self):
        with self._condition:
            self._closing=True
            while self._active: self._condition.wait()
            if self._pointer:
                self._destroy(self._pointer);self._pointer=None
    def __enter__(self): return self
    def __exit__(self,*_): self.close()
    def __del__(self):
        if hasattr(self,'_condition'): self.close()

class CancellationToken(_Handle):
    def __init__(self,native: NativeLibrary):
        pointer=C.c_void_p();status=native.lib.aa_cancel_create_v1(C.byref(pointer))
        if status: raise AbiError(status)
        super().__init__(native,pointer,native.lib.aa_cancel_destroy_v1)
    def request(self):
        with self._lease() as ptr: self._native.lib.aa_cancel_request_v1(ptr)

class Engine(_Handle):
    def __init__(self,native: NativeLibrary,catalog: bytes,astrometry: bytes):
        ptr=C.c_void_p();error=C.c_void_p()
        status=native.lib.aa_engine_create_v1(catalog,len(catalog),astrometry,len(astrometry),C.byref(ptr),C.byref(error))
        if status==1: raise ResourceError(decode(SolverError,native._message(error)['error']))
        if status: raise AbiError(status)
        super().__init__(native,ptr,native.lib.aa_engine_destroy_v1)
    def solve_rays(self,observation: RayObservation,context: EarthContext,options: SolveOptions,cancel=None) -> Union[AttitudeSolution,SolverError]:
        return self._solve('rays',observation,None,context,options,cancel)
    def solve_pixels(self,observation: PixelObservation,calibration: CameraCalibration,context: EarthContext,options: SolveOptions,cancel=None) -> Union[AttitudeSolution,SolverError]:
        return self._solve('pixels',observation,calibration,context,options,cancel)
    def _solve(self,kind,observation,calibration,context,options,cancel):
        request=json.dumps(_wire(dict(schema='astroattitude.solve/1',kind=kind,observation=observation,calibration=calibration,context=context,options=options)),allow_nan=False,separators=(',',':')).encode('utf-8')
        @contextmanager
        def token():
            if cancel is None: yield None
            else:
                if cancel._native is not self._native: raise ValueError('Token belongs to another native library')
                with cancel._lease() as p: yield p
        with self._lease() as engine,token() as cancellation:
            message=C.c_void_p();status=self._native.lib.aa_solve_v1(engine,request,len(request),cancellation,C.byref(message))
            if status not in (0,1): raise AbiError(status)
            response=self._native._message(message)
        return decode(AttitudeSolution,response['solution']) if status==0 else decode(SolverError,response['error'])
