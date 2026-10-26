#!/usr/bin/env python3
"""Run the shared Swift controls on a temporary iOS simulator, never a phone."""
import json
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import uuid

ROOT=Path(__file__).resolve().parents[1]
def call(*args, **kw):return subprocess.check_output(args,text=True,**kw).strip()
def run(*args):subprocess.run(args,check=True,cwd=ROOT)
def main():
    runtime=sorted((r for r in json.loads(call('xcrun','simctl','list','runtimes','-j'))['runtimes'] if r['isAvailable'] and r['identifier'].startswith('com.apple.CoreSimulator.SimRuntime.iOS-')),key=lambda r:tuple(map(int,r['version'].split('.'))))[-1]
    devices=json.loads(call('xcrun','simctl','list','devices','available','-j'))['devices'][runtime['identifier']]
    device=next(d for d in devices if 'iPhone' in d['name'])
    work=ROOT/'build/ios-check';work.mkdir(parents=True,exist_ok=True)
    sdk=call('xcrun','--sdk','iphonesimulator','--show-sdk-path')
    common=['-target',platform.machine()+'-apple-ios16.0-simulator','-sdk',sdk,'-I',str(ROOT/'build-simulator/framework-headers')]
    run('xcrun','swiftc',*common,'-emit-library','-static','-emit-module','-module-name','Astroattitude',*[str(p) for p in sorted((ROOT/'bindings/swift/Sources/Astroattitude').glob('*.swift'))],'-emit-module-path',str(work/'Astroattitude.swiftmodule'),'-o',str(work/'libAstroattitude.a'))
    app=work/'Synthetic.app';app.mkdir(exist_ok=True)
    bundle='org.astroattitude.syntheticcheck'
    (app/'Info.plist').write_bytes(plistlib.dumps({'CFBundleIdentifier':bundle,'CFBundleExecutable':'Synthetic','CFBundleName':'Synthetic','CFBundlePackageType':'APPL','CFBundleVersion':'1','CFBundleShortVersionString':'1.0','MinimumOSVersion':'16.0','CFBundleSupportedPlatforms':['iPhoneSimulator']}))
    run('xcrun','swiftc',*common,'-I',str(work),'-L',str(work),'-lAstroattitude',str(ROOT/'bindings/swift/Sources/SyntheticExample/main.swift'),str(ROOT/'build-simulator/libCAstroattitude.a'),'-lc++','-o',str(app/'Synthetic'))
    shutil.copytree(ROOT/'build/fixtures',app/'fixtures',dirs_exist_ok=True)
    run('codesign','--force','--sign','-',str(app))
    identifier=call('xcrun','simctl','create','Astroattitude synthetic '+uuid.uuid4().hex[:8],device['deviceTypeIdentifier'],runtime['identifier'])
    try:
        run('xcrun','simctl','boot',identifier)
        run('xcrun','simctl','bootstatus',identifier,'-b')
        run('xcrun','simctl','install',identifier,str(app))
        installed=call('xcrun','simctl','get_app_container',identifier,bundle,'app')
        output=call('xcrun','simctl','launch','--console',identifier,bundle,installed+'/fixtures')
        (work/'result.txt').write_text(output+'\n')
        if 'Swift native workflow and extraction parity passed' not in output:raise RuntimeError('Simulator did not report successful parity')
        print('iOS Simulator: synthetic solve, extraction and workflow parity passed')
    finally:
        subprocess.run(['xcrun','simctl','shutdown',identifier],check=False,stdout=subprocess.DEVNULL)
        subprocess.run(['xcrun','simctl','delete',identifier],check=False,stdout=subprocess.DEVNULL)
if __name__=='__main__':main()
