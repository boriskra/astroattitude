#!/usr/bin/env python3
"""Create an offline adapter distribution directory with the exact native binary."""
import argparse,shutil,hashlib,json,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('native',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
if a.output.exists():p.error('Output already exists')
shutil.copytree(root/'bindings/python',a.output,ignore=shutil.ignore_patterns('__pycache__','*.egg-info'))
package=a.output/'astroattitude';(package/'native').mkdir();shutil.copy2(a.native,package/'native'/a.native.name);shutil.copytree(root/'notices',package/'notices')
(package/'native/manifest.json').write_text(json.dumps(dict(file=a.native.name,sha256=hashlib.sha256(a.native.read_bytes()).hexdigest(),python_minimum='3.9'),indent=2)+'\n')
print(a.output)
