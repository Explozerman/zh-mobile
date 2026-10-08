#!/bin/bash
# ZH Mobile: stage the files the APK carries in assets/gamedata/ — copied into
# <external files>/GameData by the launcher on first start / after updates:
#   fonts/       Liberation fonts (SIL OFL) renamed to the Windows names the game asks for
#   dxvk.conf    DXVK runtime settings
#   DefaultOptions.ini  first-run graphics settings (full detail)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DEST="${ROOT}/android/app/src/main/assets/gamedata"
LIB_VERSION="2.1.5"
LIB_SHA256="7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0"
TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

mkdir -p "${DEST}/fonts"
if [[ ! -f "${DEST}/fonts/arial.ttf" ]]; then
    echo "==> Downloading Liberation fonts ${LIB_VERSION}"
    curl -fL -o "${TMP}/liberation.tar.gz" \
        "https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-${LIB_VERSION}.tar.gz" ||
    curl -fL -o "${TMP}/liberation.tar.gz" \
        "https://github.com/liberationfonts/liberation-fonts/releases/download/${LIB_VERSION}/liberation-fonts-ttf-${LIB_VERSION}.tar.gz"
    echo "${LIB_SHA256}  ${TMP}/liberation.tar.gz" | sha256sum -c -
    tar -xzf "${TMP}/liberation.tar.gz" -C "${TMP}"
    SRC="$(find "${TMP}" -name "LiberationSans-Regular.ttf" -exec dirname {} \; | head -1)"
    cp "${SRC}/LiberationSans-Regular.ttf"   "${DEST}/fonts/arial.ttf"
    cp "${SRC}/LiberationSans-Bold.ttf"      "${DEST}/fonts/arialbold.ttf"
    cp "${SRC}/LiberationMono-Regular.ttf"   "${DEST}/fonts/couriernew.ttf"
    cp "${SRC}/LiberationSerif-Regular.ttf"  "${DEST}/fonts/timesnewroman.ttf"
    cp "${SRC}/LICENSE" "${DEST}/fonts/LICENSE-Liberation.txt" 2>/dev/null || true
fi
cp "${ROOT}/android/config/dxvk.conf" "${DEST}/dxvk.conf"
cp "${ROOT}/android/config/DefaultOptions.ini" "${DEST}/DefaultOptions.ini"
echo "==> Staged APK game-data assets:"; find "${DEST}" -type f | sed "s|${DEST}/|    |"
