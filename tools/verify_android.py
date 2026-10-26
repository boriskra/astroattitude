#!/usr/bin/env python3
"""Verify final native and consumer APK alignment, exports and AAR contents."""
import argparse,subprocess,zipfile,platform
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('sdk',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
host='darwin-x86_64' if platform.system()=='Darwin' else 'linux-x86_64'
llvm=a.sdk/'ndk/27.0.12077973/toolchains/llvm/prebuilt'/host/'bin'
for abi in ('arm64-v8a','x86_64'):
    library=root/'build-android/jniLibs'/abi/'libastroattitude_jni.so'
    headers=subprocess.check_output([str(llvm/'llvm-readelf'),'-Wl',str(library)],text=True)
    for row in headers.splitlines():
        f=row.split()
        if f and f[0]=='LOAD': assert int(f[-1],16)>=16384
        if f and f[0]=='GNU_RELRO': assert (int(f[2],16)+int(f[5],16))%16384==0
    symbols=subprocess.check_output([str(llvm/'llvm-nm'),'-D','--defined-only',str(library)],text=True)
    assert all('Java_org_astroattitude_Native_' in line for line in symbols.splitlines() if line.strip())
    print(abi,'LOAD/RELRO alignment and JNI-only exports: PASS')
with zipfile.ZipFile(root/'bindings/android/library/build/outputs/aar/library-release.aar') as aar:
    assert len([n for n in aar.namelist() if n.endswith('.so')])==2
    assert not any(n.endswith('catalog.json') for n in aar.namelist())
subprocess.run([str(a.sdk/'build-tools/36.0.0/zipalign'),'-c','-P','16','4',str(root/'bindings/android/example/build/outputs/apk/release/example-release.apk')],check=True)
print('AAR and APK package checks: PASS')
