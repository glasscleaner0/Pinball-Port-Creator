#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Portable orchestration for the newly authored Pinball source and pinned external SDK.
# Requires Linux (native, WSL, or the supplied container); it never installs host packages.
set -euo pipefail

PINBALL_SDK_REVISION=ade2c81d914634834d77394c5d9f8c4575f395bc
PINBALL_SDK_REPOSITORY=https://github.com/xenosworks/xsdk.git
PINBALL_SOURCE="${PINBALL_SOURCE:-/source}"
PINBALL_CACHE="${PINBALL_CACHE:-/cache}"
PINBALL_OUTPUT="${PINBALL_OUTPUT:-/output}"
PINBALL_JOBS="${PINBALL_JOBS:-2}"

fail() { printf 'Build stopped: %s\n' "$*" >&2; exit 1; }
[[ "$(uname -s)" = Linux ]] || fail 'Run this Linux helper through native Linux, WSL, Docker, or Podman.'
[[ "$PINBALL_JOBS" =~ ^[12]$ ]] || fail 'PINBALL_JOBS must be 1 or 2.'
for pinball_program in bash git curl make gcc g++ cmake ninja python3 sha256sum patch tar xz; do
    command -v "$pinball_program" >/dev/null || fail "Missing prerequisite: $pinball_program (see the supplied Dockerfile)."
done
[[ -f "$PINBALL_SOURCE/CMakeLists.txt" && -f "$PINBALL_SOURCE/src/rewrite/xbox360_main.cpp" ]] || fail 'PINBALL_SOURCE does not contain the supplied game source.'
mkdir -p -- "$PINBALL_CACHE" "$PINBALL_OUTPUT"
PINBALL_SOURCE="$(cd -- "$PINBALL_SOURCE" && pwd -P)"
PINBALL_CACHE="$(cd -- "$PINBALL_CACHE" && pwd -P)"
PINBALL_OUTPUT="$(cd -- "$PINBALL_OUTPUT" && pwd -P)"
[[ "$PINBALL_CACHE" != / && "$PINBALL_OUTPUT" != / ]] || fail 'Cache and output must be dedicated directories.'
[[ ! -e "$PINBALL_OUTPUT/default.xex" && ! -e "$PINBALL_OUTPUT/BUILD-TOOLCHAIN.json" ]] || fail 'Output already contains a build; choose a new output directory.'
PINBALL_LOCK="$PINBALL_CACHE/.pinball-build-lock"
mkdir -- "$PINBALL_LOCK" 2>/dev/null || fail "Cache is already in use, or has an interrupted-run lock: $PINBALL_LOCK. Check that no build is running before removing this empty lock directory."
trap 'rmdir -- "$PINBALL_LOCK" 2>/dev/null || true' EXIT
mkdir -p -- "$PINBALL_CACHE/logs"
exec > >(tee -a "$PINBALL_CACHE/logs/bootstrap.log") 2>&1
printf 'Pinball native build started %s; host=%s; jobs=%s\n' "$(date -u +%FT%TZ)" "$(uname -m)" "$PINBALL_JOBS"
printf 'Source: %s\nCache: %s\nOutput: %s\n' "$PINBALL_SOURCE" "$PINBALL_CACHE" "$PINBALL_OUTPUT"

pinball_explicit_sdk=0
if [[ -n "${PINBALL_SDK_ROOT:-}" ]]; then
    pinball_explicit_sdk=1
    [[ -d "$PINBALL_SDK_ROOT/.git" ]] || fail 'Explicit PINBALL_SDK_ROOT must be the pinned Git checkout.'
    PINBALL_SDK_ROOT="$(cd -- "$PINBALL_SDK_ROOT" && pwd -P)"
else
    PINBALL_SDK_ROOT="$PINBALL_CACHE/xsdk-$PINBALL_SDK_REVISION"
    if [[ ! -e "$PINBALL_SDK_ROOT" ]]; then
        pinball_sdk_partial="$PINBALL_SDK_ROOT.partial"
        [[ ! -e "$pinball_sdk_partial" ]] || fail "Interrupted SDK fetch preserved at $pinball_sdk_partial; inspect it before removing it and retrying."
        mkdir -- "$pinball_sdk_partial"
        git -C "$pinball_sdk_partial" init --quiet
        git -C "$pinball_sdk_partial" -c advice.detachedHead=false fetch --depth=1 "$PINBALL_SDK_REPOSITORY" "$PINBALL_SDK_REVISION"
        git -C "$pinball_sdk_partial" -c advice.detachedHead=false checkout --detach FETCH_HEAD --quiet
        [[ "$(git -C "$pinball_sdk_partial" rev-parse HEAD)" = "$PINBALL_SDK_REVISION" ]] || fail 'Fetched SDK revision differs from the pin.'
        mv -- "$pinball_sdk_partial" "$PINBALL_SDK_ROOT"
    fi
fi
[[ -d "$PINBALL_SDK_ROOT/.git" ]] || fail 'Cached SDK is not a Git checkout; no files were overwritten.'
[[ "$(git -c safe.directory="$PINBALL_SDK_ROOT" -C "$PINBALL_SDK_ROOT" rev-parse HEAD)" = "$PINBALL_SDK_REVISION" ]] || fail 'SDK revision differs from the reviewed pin.'
git -c safe.directory="$PINBALL_SDK_ROOT" -C "$PINBALL_SDK_ROOT" diff --quiet HEAD -- || fail 'SDK tracked files have changed; restore or select a clean pinned checkout.'
# Verify the reviewed execution/build surface independently before running the SDK.
(
    cd -- "$PINBALL_SDK_ROOT"
    sha256sum --check --strict <<'PINBALL_REVIEWED_FILES'
61fdeb70869ecc3b184278a4f1ad888ba9d3d4d7c8aa8971fdd69934a1709674  tools/toolchain/build.sh
fa32ed862d3bc4d1429dfc16aa8b44fcaae9458509e5f9be3ea705a5067b576f  tools/toolchain/binutils-xsdk.patch
90ecb946f7aaa6ba0add946af539851a9f854abbb98615c6b911c1f718b4b4a5  tools/toolchain/gcc-xsdk.patch
5f4ef6eb59756e46e3c009eead777d791f1821016d3fb39eb69f7d4e65c97f6b  tools/toolchain/newlib-xsdk.patch
0d8400cdfda366d1a6ab858d334a6796756a17328253703e6162027e8c0e8345  Makefile
4e9a156c9f013c7bb1549b98551494bf68817c1f2c59aac9217a01ff59acfd37  tools/gen_imports.py
5779dae22cc68fc9a9496fb50d312bee7073ceb72d5b0a1f80ba21f08824a888  cmake/xsdk.cmake
61f498176b451c424b17b2550bfa64800db695936e58e67dc05feff68fce69de  LICENSE
f92f9b39349dd6aad17351729a9d3b87782d545e47c50ea8a2bfedb6f762868f  THIRD_PARTY_NOTICES.md
PINBALL_REVIEWED_FILES
)

if [[ -n "${PINBALL_TOOLCHAIN:-}" ]]; then
    [[ -d "$PINBALL_TOOLCHAIN" ]] || fail 'Explicit PINBALL_TOOLCHAIN does not exist.'
    PINBALL_TOOLCHAIN="$(cd -- "$PINBALL_TOOLCHAIN" && pwd -P)"
else
    PINBALL_TOOLCHAIN="$PINBALL_CACHE/powerpc-xsdk"
    pinball_plan="$PINBALL_CACHE/toolchain-plan.txt"
    pinball_wanted_plan="revision=$PINBALL_SDK_REVISION;host=$(uname -m);gcc=15.3.0;binutils=2.47;newlib=4.6.0.20260123"
    if [[ -e "$pinball_plan" ]]; then
        [[ "$(cat -- "$pinball_plan")" = "$pinball_wanted_plan" ]] || fail 'Cached toolchain plan differs; choose a separate cache.'
    else
        [[ ! -e "$PINBALL_TOOLCHAIN" && ! -e "$PINBALL_CACHE/toolchain-work" ]] || fail 'Unidentified existing compiler cache; choose a new cache or explicitly supply a reviewed toolchain.'
        printf '%s\n' "$pinball_wanted_plan" > "$pinball_plan"
    fi
    export PREFIX="$PINBALL_TOOLCHAIN"
    export WORK="$PINBALL_CACHE/toolchain-work"
    export JOBS="$PINBALL_JOBS"
    # Upstream's reviewed script verifies the SHA-256 of every downloaded source archive.
    # It resumes its own stamped stages in this dedicated cache; first setup is substantial.
    if [[ ! -f "$PINBALL_CACHE/toolchain-complete.txt" ]]; then
        printf 'Building the pinned cross compiler. Downloads/builds may take a long time on the first run.\n'
        bash "$PINBALL_SDK_ROOT/tools/toolchain/build.sh"
        printf '%s\n' "$pinball_wanted_plan" > "$PINBALL_CACHE/toolchain-complete.txt"
    else
        [[ "$(cat -- "$PINBALL_CACHE/toolchain-complete.txt")" = "$pinball_wanted_plan" ]] || fail 'Compiler completion stamp differs; choose a separate cache.'
    fi
fi
pinball_compiler="$PINBALL_TOOLCHAIN/bin/powerpc-xsdk-elf-g++"
[[ -x "$pinball_compiler" ]] || fail 'PowerPC C++ compiler is missing.'
[[ "$("$pinball_compiler" -dumpmachine)" = powerpc-xsdk-elf ]] || fail 'Wrong compiler target.'
[[ "$("$pinball_compiler" -dumpfullversion)" = 15.3.0 ]] || fail 'Compiler version must be the pinned 15.3.0.'
"$pinball_compiler" --version | head -n 1
# An explicit existing SDK/library is reused without regenerating it. Otherwise make
# updates only its ignored build directory; tracked SDK source must stay unchanged.
if [[ "$pinball_explicit_sdk" = 0 || ! -s "$PINBALL_SDK_ROOT/build/libxsdk.a" ]]; then
    make -C "$PINBALL_SDK_ROOT" -j"$PINBALL_JOBS" TOOLCHAIN="$PINBALL_TOOLCHAIN/bin/powerpc-xsdk-elf-" build/libxsdk.a
fi
[[ -s "$PINBALL_SDK_ROOT/build/libxsdk.a" ]] || fail 'SDK runtime library is missing.'
git -c safe.directory="$PINBALL_SDK_ROOT" -C "$PINBALL_SDK_ROOT" diff --quiet HEAD -- || fail 'SDK build changed tracked source files.'
# Separate CMake trees for separate source locations and host architectures.
pinball_source_key="$(printf '%s\n%s\n' "$PINBALL_SOURCE" "$(uname -m)" | sha256sum | cut -c1-16)"
pinball_build="$PINBALL_CACHE/game-build-$pinball_source_key"
cmake -S "$PINBALL_SOURCE" -B "$pinball_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_TOOLCHAIN_FILE="$PINBALL_SDK_ROOT/cmake/xsdk.cmake" \
    -DXSDK_TOOLCHAIN="$PINBALL_TOOLCHAIN"
cmake --build "$pinball_build" --target pinball360 --parallel "$PINBALL_JOBS"
pinball_xex="$pinball_build/bin/pinball360.xex"
[[ -s "$pinball_xex" ]] || fail 'Build did not produce the Xbox executable.'
python3 - "$PINBALL_SDK_ROOT" "$PINBALL_TOOLCHAIN" "$pinball_build" "$PINBALL_OUTPUT" "$PINBALL_SDK_REVISION" <<'PINBALL_RECORD'
import datetime, hashlib, json, os, platform, shutil, subprocess, sys
from pathlib import Path
sdk, toolchain, build, output = (Path(p) for p in sys.argv[1:5])
revision = sys.argv[5]
xex = build / 'bin/pinball360.xex'
if xex.read_bytes()[:4] != b'XEX2':
    raise SystemExit('Build stopped: result is not an XEX2 executable.')
def digest(p):
    h = hashlib.sha256()
    with p.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()
record = {'schema': 1, 'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'sdk_repository': 'https://github.com/xenosworks/xsdk.git', 'sdk_revision': revision,
          'sdk_path': str(sdk), 'toolchain_path': str(toolchain), 'build_path': str(build),
          'host_architecture': platform.machine(), 'target': 'powerpc-xsdk-elf', 'gcc_version': '15.3.0',
          'sdk_runtime_sha256': digest(sdk / 'build/libxsdk.a'), 'xex_sha256': digest(xex),
          'assets_included_by_bootstrap': False, 'gameplay_validation': 'not performed by compilation'}
for candidate, key in [(build / 'src/rewrite/pinball360.map', 'link_map'), (build / 'bin/pinball360', 'elf')]:
    if candidate.is_file():
        record[key + '_path'] = str(candidate)
        record[key + '_sha256'] = digest(candidate)
# Exclusive creation prevents overwriting a previous accepted executable.
with (output / 'default.xex').open('xb') as target, xex.open('rb') as source:
    shutil.copyfileobj(source, target)
with (output / 'BUILD-TOOLCHAIN.json').open('x', encoding='utf-8') as target:
    json.dump(record, target, indent=2)
    target.write('\n')
print('Xbox executable: ' + str(output / 'default.xex'))
print('XEX SHA-256: ' + record['xex_sha256'])
PINBALL_RECORD
printf 'Native compilation completed %s. Assets, configuration and retained notices are packaged by the Python launcher.\n' "$(date -u +%FT%TZ)"
