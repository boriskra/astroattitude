#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${ANDROID_HOME:?Set ANDROID_HOME to the installed Android SDK}"
for abi in arm64-v8a x86_64; do
 cmake -S . -B "build-android/$abi" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$ANDROID_HOME/ndk/27.0.12077973/build/cmake/android.toolchain.cmake" -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DAA_BUILD_TESTS=OFF -DAA_BUILD_TOOLS=OFF
 cmake --build "build-android/$abi" --target astroattitude_jni -j 12
 mkdir -p "build-android/jniLibs/$abi"
 cp "build-android/$abi/libastroattitude_jni.so" "build-android/jniLibs/$abi/"
done
