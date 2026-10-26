#!/usr/bin/env python3
import subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
symbols=subprocess.check_output(['nm','-gU',str(root/'build/libastroattitude_c.dylib')],text=True)
assert all(row.split()[-1].startswith('_aa_') for row in symbols.splitlines() if row.strip()), symbols
for name in ['build/libastroattitude.a','build/libastroattitude_c.dylib']:
    all_symbols=subprocess.check_output(['nm',str(root/name)],text=True)
    assert not any(token in all_symbols for token in ['candidate_seen','release_candidate','aa_fail_after','aa_unexpected','main'])
print('Production exports limited to C ABI; diagnostic hooks excluded')
