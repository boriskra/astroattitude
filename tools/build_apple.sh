#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
for platform in macos ios simulator; do
 case "$platform" in
 macos) sdk=macosx; system=Darwin; deployment=13.0;;
 ios) sdk=iphoneos; system=iOS; deployment=16.0;;
 simulator) sdk=iphonesimulator; system=iOS; deployment=16.0;;
 esac
 arch=$(uname -m)
 [ "$platform" != ios ] || arch=arm64
 cmake -S . -B "build-$platform" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME="$system" -DCMAKE_OSX_SYSROOT="$(xcrun --sdk "$sdk" --show-sdk-path)" -DCMAKE_OSX_ARCHITECTURES="$arch" -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment" -DAA_BUILD_TESTS=OFF -DAA_BUILD_TOOLS=OFF
 cmake --build "build-$platform" --target astroattitude_c_static -j 12
 mkdir -p "build-$platform/framework-headers"
 cp include/astroattitude/astroattitude.h "build-$platform/framework-headers/"
 printf 'module CAstroattitude { header "astroattitude.h" export * }\n' > "build-$platform/framework-headers/module.modulemap"
 xcrun libtool -static -o "build-$platform/libCAstroattitude.a" "build-$platform/libastroattitude_c_static.a" "build-$platform/libastroattitude.a" "build-$platform/liberfa.a"
done
mkdir -p bindings/swift/Artifacts
# Only replace this script's generated artifact.
if [ -d bindings/swift/Artifacts/CAstroattitude.xcframework ]; then mv bindings/swift/Artifacts/CAstroattitude.xcframework "build/previous-xcframework-$(date +%s)"; fi
xcodebuild -create-xcframework -library build-macos/libCAstroattitude.a -headers build-macos/framework-headers -library build-ios/libCAstroattitude.a -headers build-ios/framework-headers -library build-simulator/libCAstroattitude.a -headers build-simulator/framework-headers -output bindings/swift/Artifacts/CAstroattitude.xcframework

mkdir -p bindings/swift/Artifacts/CAstroattitude.xcframework/Notices
cp notices/* bindings/swift/Artifacts/CAstroattitude.xcframework/Notices/

cp LICENSE bindings/swift/Artifacts/CAstroattitude.xcframework/Notices/ASTROATTITUDE-LICENSE.txt
