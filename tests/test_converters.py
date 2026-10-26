import importlib.util, math, unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('convert',Path(__file__).resolve().parents[1]/'tools/convert.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
def eop(mjd,pm='I',ut='I',b=False,blank=False):
    line=list(' '*185)
    def put(a,z,s): line[a:z]=str(s).rjust(z-a)
    put(7,15,f'{mjd:.2f}');put(16,17,pm);put(57,58,ut)
    if not blank:
        put(18,27,'.1');put(37,46,'.2');put(58,68,'.3')
    if b: put(134,144,'.4');put(144,154,'.5');put(154,165,'.6')
    return ''.join(line)
class Converters(unittest.TestCase):
    def test_eop(self):
        raw='\n'.join([eop(58848,b=True),eop(58849,ut='P'),eop(58850),eop(58851,blank=True)]).encode()
        rows,choice=module.finals(raw,1577750400,1577923200)
        self.assertEqual([r['status'] for r in rows],['observed','predicted','observed'])
        self.assertEqual(choice[0]['choice'],'B');self.assertAlmostEqual(rows[0]['ut1_minus_utc_s'],.6)
        with self.assertRaises(ValueError): module.finals(raw,1577750400,1578009600)
    def test_leaps(self):
        raw=b'# public domain synthetic\n#@ 3786998400\n3692217600 37\n'
        rows,expiry=module.leaps(raw,1577836800,1577923200)
        self.assertEqual(rows[0]['tai_minus_utc_s'],37)
        with self.assertRaises(ValueError): module.leaps(raw,1577836800,expiry+1)
    def test_interpolation_expiry(self):
        with self.assertRaises(ValueError): module.check_eop_leap_bracket([{'utc_s':1577923200}],1577923200)
        module.check_eop_leap_bracket([{'utc_s':1577923200}],1578009600)
    def test_epoch(self):
        lines=[]
        for i in range(6):
            f=['']*78
            for k,v in {0:'H',1:str(i+1),5:'4',8:str(100+i),9:'10',11:'10',12:'100',13:'20'}.items(): f[k]=v
            lines.append('|'.join(f))
        stars,rejected=module.hipparcos('\n'.join(lines).encode(),Path('build/aa_propagate').resolve())
        self.assertEqual(len(stars),6);self.assertEqual(len(rejected),0)
        self.assertGreater(stars[0]['ra_rad'],100*math.pi/180)
        self.assertEqual(stars[0]['radial_velocity_status'],'assumed')
        self.assertGreater(stars[0]['radial_velocity_km_s'],0)
if __name__=='__main__': unittest.main()
