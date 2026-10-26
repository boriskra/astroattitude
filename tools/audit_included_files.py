#!/usr/bin/env python3
"""Inventory adopted source/header bytes and preserve included-file notices."""
import hashlib,json,re,subprocess
from collections import defaultdict
from pathlib import Path
root=Path(__file__).resolve().parents[1];deps=root/'.deps';paths=set()
for build in [root/'build',root/'build-android/arm64-v8a',root/'build-android/x86_64']:
    if not (build/'build.ninja').exists():continue
    output=subprocess.check_output(['ninja','-C',str(build),'-t','deps'],text=True)
    for line in output.splitlines():
        p=Path(line.strip())
        if p.is_file() and str(p).startswith(str(deps)) and p.suffix in ('.h','.hpp',''):paths.add(p)
paths.update(p for p in (deps/'erfa/src').glob('*.c') if not p.name.startswith('t_erfa_c'))
records=[];notices=defaultdict(list)
for p in sorted(paths):
    relative=str(p.relative_to(deps));source=p.read_text(errors='strict')
    records.append({'path':relative,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    for comment in re.findall(r'/\*.*?\*/|(?://[^\n]*\n)+',source,re.S):
        if re.search(r'copyright|license|redistribution|mozilla',comment,re.I):notices[comment].append(relative)
(root/'notices/INCLUDED_FILES.json').write_text(json.dumps(records,indent=2)+'\n')
(root/'notices/INCLUDED_COPYRIGHTS.txt').write_text('\n\n'.join('Files:\n'+'\n'.join(files)+'\n\n'+comment for comment,files in notices.items())+'\n')
print(len(records),'source/header records;',len(notices),'distinct notice blocks')
