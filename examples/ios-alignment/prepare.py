#!/usr/bin/env python3
"""Fetch the pinned example-only detector; build the Apple adapter."""
from pathlib import Path
import hashlib, io, subprocess, tarfile, urllib.request, platform, shutil, plistlib
ROOT=Path(__file__).resolve().parents[2]
SOURCE='https://github.com/AprilRobotics/apriltag/archive/refs/tags/v3.4.5.tar.gz'
SHA='3749f48220db78c29c375a373861f4e1f4138e5dbae3c298584e96cfcc556515'
def run(*args):subprocess.run(args,cwd=ROOT,check=True)
def main():
 raw=urllib.request.urlopen(SOURCE,timeout=60).read()
 if hashlib.sha256(raw).hexdigest()!=SHA:raise ValueError('AprilTag checksum mismatch')
 target=ROOT/'.deps/apriltag';target.mkdir(parents=True,exist_ok=True)
 with tarfile.open(fileobj=io.BytesIO(raw),mode='r:gz') as archive:
  for member in archive.getmembers():
   parts=Path(member.name).parts[1:]
   if not parts:continue
   if '..' in parts or not (member.isdir() or member.isfile()):raise ValueError('Unsafe archive')
   dest=target.joinpath(*parts)
   if member.isdir():dest.mkdir(parents=True,exist_ok=True)
   else:dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(archive.extractfile(member).read())
 products=[]
 for name,sdk,system,arch,version in [('macos','macosx','Darwin',platform.machine(),'13.0'),('ios','iphoneos','iOS','arm64','16.0'),('simulator','iphonesimulator','iOS',platform.machine(),'16.0')]:
  build=ROOT/('build-alignment-'+name);sdkpath=subprocess.check_output(['xcrun','--sdk',sdk,'--show-sdk-path'],text=True).strip()
  run('cmake','-S','examples/ios-alignment/Adapter','-B',str(build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_SYSTEM_NAME='+system,'-DCMAKE_OSX_SYSROOT='+sdkpath,'-DCMAKE_OSX_ARCHITECTURES='+arch,'-DCMAKE_OSX_DEPLOYMENT_TARGET='+version)
  run('cmake','--build',str(build),'-j12')
  framework=build/'AlignmentAdapter.framework';headers=framework/'Headers';modules=framework/'Modules'
  headers.mkdir(parents=True,exist_ok=True);modules.mkdir(exist_ok=True)
  shutil.copy(ROOT/'examples/ios-alignment/Adapter/AlignmentAdapter.h',headers)
  (modules/'module.modulemap').write_text('framework module AlignmentAdapter { umbrella header "AlignmentAdapter.h" export * }\n')
  (framework/'Info.plist').write_bytes(plistlib.dumps({'CFBundleIdentifier':'org.astroattitude.AlignmentAdapter','CFBundleName':'AlignmentAdapter','CFBundleExecutable':'AlignmentAdapter','CFBundlePackageType':'FMWK','CFBundleShortVersionString':'1.0','CFBundleVersion':'1','MinimumOSVersion':version}))
  run('xcrun','libtool','-static','-o',str(framework/'AlignmentAdapter'),str(build/'libAlignmentAdapter.a'),str(build/'apriltag/libapriltag.a'))
  products+=['-framework',str(framework)]
 artifact=ROOT/'examples/ios-alignment/Adapter/Artifacts/AlignmentAdapter.xcframework'
 if artifact.exists():shutil.rmtree(artifact)
 artifact.parent.mkdir(exist_ok=True)
 run('xcodebuild','-create-xcframework',*products,'-output',str(artifact))
 shutil.copy(ROOT/'examples/ios-alignment/Adapter/APRILTAG-LICENSE.txt',artifact)
if __name__=='__main__':main()
