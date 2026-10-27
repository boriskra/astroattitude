#!/usr/bin/env python3
"""Build and run example tests in a disposable simulator, never a phone."""
import argparse
import json
import platform
from pathlib import Path
import subprocess
import uuid
ROOT=Path(__file__).resolve().parents[2]
def output(*args):return subprocess.check_output(args,text=True,cwd=ROOT).strip()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--only-testing',help='Optional XCTest target or target/class/method')
    args=parser.parse_args()
    runtimes=json.loads(output('xcrun','simctl','list','runtimes','-j'))['runtimes']
    runtime=sorted((r for r in runtimes if r['isAvailable'] and '.iOS-' in r['identifier']),key=lambda r:tuple(map(int,r['version'].split('.'))))[-1]
    types=json.loads(output('xcrun','simctl','list','devices','available','-j'))['devices'][runtime['identifier']]
    device=next(d for d in types if 'iPhone' in d['name'])
    identifier=output('xcrun','simctl','create','Alignment checks '+uuid.uuid4().hex[:8],device['deviceTypeIdentifier'],runtime['identifier'])
    try:
        subprocess.run(['xcrun','simctl','boot',identifier],check=True,timeout=60)
        subprocess.run(['xcrun','simctl','bootstatus',identifier,'-b'],check=True,timeout=180)
        subprocess.run(['xcodebuild','-project','examples/ios-alignment/Alignment.xcodeproj','-scheme','AlignmentDemo','-destination','platform=iOS Simulator,id='+identifier,'-derivedDataPath','build-alignment-app','-jobs','12','-parallel-testing-enabled','NO','-collect-test-diagnostics','never','ARCHS='+platform.machine(),'CODE_SIGNING_ALLOWED=NO','test']+(['-only-testing:'+args.only_testing] if args.only_testing else []),check=True,cwd=ROOT,timeout=600)
    finally:
        subprocess.run(['xcrun','simctl','shutdown',identifier],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        subprocess.run(['xcrun','simctl','delete',identifier],stdout=subprocess.DEVNULL,check=True)
if __name__=='__main__':main()
