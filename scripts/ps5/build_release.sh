#!/usr/bin/env bash
# Reproducible incremental PS5 build and source release. Does not connect to PS5.
set -Eeuo pipefail
BASE="${BASE:-$HOME/prosperoweb-av1-build}"
REPO="$BASE/ProsperoWeb"
SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"
JOBS="${JOBS:-4}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE="${1:-all}"
log(){ printf '\n[av1-menu] %s\n' "$*"; }
[[ -f "$REPO/CMakeLists.txt" ]] || { echo "ProsperoWeb not at $REPO" >&2; exit 1; }
[[ -f "$REPO/build-dist/PRWB00001.zip" ]] || { echo 'Existing AV1 package missing. Preserve current build before this step.' >&2; exit 1; }
[[ -f "$BASE/build-ffmpeg/ffbuild/config.mak" ]] || { echo 'FFmpeg build config missing.' >&2; exit 1; }
grep -q '^CONFIG_LIBDAV1D_DECODER=yes' "$BASE/build-ffmpeg/ffbuild/config.mak" || { echo 'ERROR: built FFmpeg lacks dav1d'; exit 1; }
[[ -f "$REPO/third_party/ps5-prefix/lib/libdav1d.a" ]] || { echo 'libdav1d.a missing'; exit 1; }

stable="$BASE/av1-stable-before-popup"
mkdir -p "$stable"
if [[ ! -e "$stable/PRWB00001.zip" ]]; then
    log 'Snapshotting working AV1 package for rollback'
    cp -- "$REPO/build-dist/PRWB00001.zip" "$stable/PRWB00001.zip"
    (cd "$stable" && sha256sum PRWB00001.zip > SHA256SUMS)
fi

if [[ "$MODE" == 'all' || "$MODE" == 'apply' ]]; then
    log 'Applying native WebKit popup patch; existing PS5 untouched'
    python3 "$HERE/apply_native_popup.py" "$REPO"
    (cd "$REPO" && scripts/ps5/export-webkit-patches.sh)
    log 'Patch exported as webkit-port/patches/0022-webkit-playstation-native-select.patch'
fi

if [[ "$MODE" == 'all' || "$MODE" == 'build' ]]; then
    [[ -f "$REPO/src/frontend/ps5/ps5_popup_menu.cpp" ]] || { echo 'Run apply first' >&2; exit 1; }
    log 'Incremental WebKit UIProcess/WebKit rebuild; uses existing AV1 deps'
    (cd "$REPO" && PS5_PAYLOAD_SDK="$SDK" JOBS="$JOBS" scripts/ps5/build-webkit.sh webkit)
    (cd "$REPO" && PS5_PAYLOAD_SDK="$SDK" JOBS="$JOBS" scripts/ps5/build-webkit.sh package)
    log 'Relinking native and payload binaries'
    (cd "$REPO" && cmake -S . -B build-ps5 -G Ninja \
       -DCMAKE_TOOLCHAIN_FILE="$SDK/toolchain/prospero.cmake" \
       -DPROSPEROWEB_FRONTEND=ps5 -DPROSPEROWEB_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release)
    (cd "$REPO" && cmake --build build-ps5 -j "$JOBS" \
      --target prosperoweb prosperoweb_native prosperoweb_broker)
    log 'Repackaging complete native title'
    (cd "$REPO" && ADDONS_ALLOW_UNPINNED=1 PS5_PAYLOAD_SDK="$SDK" scripts/ps5/package.sh)
    (cd "$REPO" && PS5_PAYLOAD_SDK="$SDK" NATIVE_ALLOW_STUB=0 scripts/ps5/native/make-title.sh)
    (cd "$REPO" && unzip -t -q build-dist/PRWB00001.zip)
    log 'Output: build-dist/PRWB00001.zip'
fi

if [[ "$MODE" == 'all' || "$MODE" == 'bundle' ]]; then
    [[ -f "$REPO/webkit-port/patches/0022-webkit-playstation-native-select.patch" ]] || { echo 'Patch has not been exported'; exit 1; }
    [[ -f "$REPO/build-dist/PRWB00001.zip" ]] || exit 1
    OUT="$BASE/release-AV1-native-select-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$OUT"
    log "Creating end-user and source release at $OUT"
    cp -- "$REPO/build-dist/PRWB00001.zip" "$OUT/PRWB00001-AV1-native-select.zip"
    cp -- "$HERE/apply_native_popup.py" "$HERE/build_release.sh" "$HERE/README.md" "$HERE/GITHUB_DRAFT.md" "$OUT/"
    cp -- "$REPO/webkit-port/patches/0022-webkit-playstation-native-select.patch" "$OUT/"
    if [[ -f "$HOME/prosperoweb-av1-build.sh" ]]; then
        cp -- "$HOME/prosperoweb-av1-build.sh" "$OUT/"
    fi
    if [[ -d "$BASE/dav1d" && -d "$BASE/ffmpeg-7.0.1" ]]; then
        # Include cross-build recipe, ProsperoWeb project + PS5 WebKit patch set,
        # and exact dav1d/FFmpeg sources; do not ship multi-GB WebKit build trees.
        tar -cJf "$OUT/ProsperoWeb-AV1-native-select-sources.tar.xz" \
          --exclude-vcs \
          --exclude='ProsperoWeb/third_party/webkit' \
          --exclude='ProsperoWeb/third_party/ps5-prefix' \
          --exclude='ProsperoWeb/third_party/dl' \
          --exclude='ProsperoWeb/build-*' \
          --exclude='ProsperoWeb/dist' \
          --exclude='ProsperoWeb/*.log' \
          -C "$BASE" ProsperoWeb dav1d ffmpeg-7.0.1
    else
        echo 'WARNING: dav1d/FFmpeg source dirs missing; archive NOT created' >&2
    fi
    # Show newly created popup sources in the developer-facing patch as well.
    (cd "$REPO" && git add -N -- src/frontend/ps5/ps5_popup_menu.cpp \
        src/frontend/ps5/ps5_popup_menu.h && git diff --binary > "$OUT/ProsperoWeb-tracked-changes.patch")
    for manifest in prosperoweb-commit.txt dav1d-commit.txt ffmpeg-archive-sha256.txt; do
        [[ ! -f "$BASE/$manifest" ]] || cp -- "$BASE/$manifest" "$OUT/"
    done
    (cd "$OUT" && find . -maxdepth 1 -type f ! -name SHA256SUMS -print0 \
        | sort -z | xargs -0 sha256sum > SHA256SUMS)
    log 'Final files:'
    ls -lh "$OUT"
    log 'No files were uploaded to the PS5.'
fi

if [[ "$MODE" != all && "$MODE" != apply && "$MODE" != build && "$MODE" != bundle ]]; then
    echo 'Usage: build_release.sh [all|apply|build|bundle]' >&2; exit 2
fi
