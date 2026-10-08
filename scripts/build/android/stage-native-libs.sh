#!/bin/bash
# ZH Mobile: collect the native libraries the APK needs into android/app/jniLibs/arm64-v8a.
#
# Starts from libmain.so (the game) and libdxvk_d3d8.so (dlopen()ed by the engine, so
# not a DT_NEEDED of anything), then follows DT_NEEDED entries through the build tree.
# Every dependency must be either staged here or a library Android guarantees to apps —
# otherwise the script fails, because the APK would crash at load time ("library not found").
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD="${1:-${ROOT}/build/android-arm64}"
DEST="${ROOT}/android/app/jniLibs/arm64-v8a"
NDK="${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point to the NDK}"
READELF="${NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
STRIP="${NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
CXX_SHARED="${NDK}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"

# Public NDK libraries every Android 10+ device provides to apps.
SYSTEM_LIBS=" libc.so libm.so libdl.so liblog.so libandroid.so libvulkan.so libEGL.so libGLESv1_CM.so
 libGLESv2.so libGLESv3.so libOpenSLES.so libaaudio.so libz.so libjnigraphics.so libmediandk.so
 libnativewindow.so libamidi.so libcamera2ndk.so libsync.so libbinder_ndk.so "
SYSTEM_LIBS=" $(echo ${SYSTEM_LIBS}) "   # collapse newlines so " name " matching works

rm -rf "${DEST}"
mkdir -p "${DEST}"

find_lib() {
    local name="$1"
    if [[ "${name}" == "libc++_shared.so" ]]; then
        echo "${CXX_SHARED}"
        return
    fi
    # Prefer the top-level copies (dx8.cmake installs DXVK there), then anything in the tree.
    if [[ -f "${BUILD}/${name}" ]]; then
        echo "${BUILD}/${name}"
        return
    fi
    find "${BUILD}" -name "${name}" -type f -not -path "*/CMakeFiles/*" 2>/dev/null | head -1
}

declare -A seen
queue=(libmain.so libdxvk_d3d8.so libdxvk_d3d9.so)
missing=()
while ((${#queue[@]})); do
    lib="${queue[0]}"
    queue=("${queue[@]:1}")
    [[ -n "${seen[${lib}]:-}" ]] && continue
    seen[${lib}]=1
    if [[ "${SYSTEM_LIBS}" == *" ${lib} "* ]]; then
        continue
    fi
    path="$(find_lib "${lib}")"
    if [[ -z "${path}" ]]; then
        missing+=("${lib}")
        continue
    fi
    cp -L "${path}" "${DEST}/${lib}"
    echo "  staged ${lib}  <- ${path#${ROOT}/}"
    while read -r needed; do
        queue+=("${needed}")
    done < <("${READELF}" -d "${DEST}/${lib}" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
done

if ((${#missing[@]})); then
    echo "ERROR: unresolved native libraries: ${missing[*]}" >&2
    exit 1
fi

# Debug info stays in the CI build artifacts; the APK gets stripped libraries.
if [[ "${ZH_KEEP_SYMBOLS:-0}" != "1" ]]; then
    "${STRIP}" --strip-unneeded "${DEST}"/*.so
fi

# Sanity: libmain must export SDL_main for SDLActivity.nativeRunMain().
# (Capture first: with pipefail, "grep -q" closing the pipe early makes readelf fail.)
DYNSYMS="$("${READELF}" --dyn-syms --wide "${DEST}/libmain.so")"
if ! grep -qE "[[:space:]]SDL_main$" <<< "${DYNSYMS}"; then
    echo "ERROR: libmain.so does not export SDL_main" >&2
    exit 1
fi
# Sanity: DXVK must have been built with the SDL3 WSI (see playbook: silent SDL2 fallback).
D3D9_STRINGS="$(strings "${DEST}/libdxvk_d3d9.so")"
if ! grep -q "Sdl3WsiDriver\|SDL3 WSI" <<< "${D3D9_STRINGS}"; then
    echo "ERROR: libdxvk_d3d9.so was built without the SDL3 WSI" >&2
    exit 1
fi
echo "==> $(ls "${DEST}" | wc -l) libraries, $(du -sh "${DEST}" | cut -f1) staged in ${DEST#${ROOT}/}"
