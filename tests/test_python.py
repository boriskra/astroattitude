import ctypes as C
import json
import sys
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from astroattitude import *

NATIVE=NativeLibrary(sys.argv.pop(1));FIXTURES=Path(sys.argv.pop(1))
CAT=(FIXTURES/'catalog.json').read_bytes();DATA=(FIXTURES/'astrometry.json').read_bytes()
class Conformance(unittest.TestCase):
    def test_parser_limits(self):
        catalog=json.loads(CAT)
        variants=[b'\xef\xbb\xbf'+CAT,CAT+b'{}',b'{"a":1,"a":2}',b'\xff',CAT.replace(b'"schema":',b'"schema":"duplicate","schema":',1)]
        for raw in variants:
            with self.assertRaises(ResourceError) as ctx: Engine(NATIVE,raw,DATA)
            self.assertEqual(ctx.exception.error.code,ErrorCode.InvalidResource)
        for raw in [b' '*16777217,b'['*20+b'0'+b']'*20]:
            with self.assertRaises(ResourceError) as ctx: Engine(NATIVE,raw,DATA)
            self.assertEqual(ctx.exception.error.code,ErrorCode.ResourceLimitExceeded)
        catalog['provenance']['sources'][0]['attribution']='x'*1025
        with self.assertRaises(ResourceError): Engine(NATIVE,json.dumps(catalog).encode(),DATA)
    def test_parity_and_concurrent_close(self):
        q=json.loads((FIXTURES/'rays.json').read_bytes());obs=decode(RayObservation,q['observation']);ctx=decode(EarthContext,q['context']);opts=decode(SolveOptions,q['options'])
        expected=decode(AttitudeSolution,json.loads((FIXTURES/'expected.json').read_bytes())['solution'])
        for _ in range(10):
            engine=Engine(NATIVE,CAT,DATA)
            with ThreadPoolExecutor(4) as pool:
                jobs=[pool.submit(engine.solve_rays,obs,ctx,opts) for _ in range(3)]
                for job in jobs:
                    result=job.result();self.assertEqual(result.matches,expected.matches);self.assertEqual(result.models,expected.models)
                closing=pool.submit(engine.close);closing.result()
            engine.close()
            with self.assertRaises(RuntimeError):engine.solve_rays(obs,ctx,opts)
    def test_raw_abi(self):
        self.assertEqual(NATIVE.lib.aa_solve_v1(None,None,0,None,None),2)
        self.assertEqual(NATIVE.lib.aa_cancel_create_v1(None),2)
        NATIVE.lib.aa_engine_destroy_v1(None);NATIVE.lib.aa_message_destroy_v1(None);NATIVE.lib.aa_cancel_destroy_v1(None)
if __name__=='__main__':unittest.main()
