#!/bin/bash
# The Windows x64 core, cross-built on Linux for the libretro buildbot, whose
# own Windows runner has MSVC 2019 16.8 - too old for Xenia Edge, which the
# project's CI builds with MSVC on windows-2025. Here it is clang-cl and
# lld-link (LLVM 21, as the Linux jobs) against the MSVC CRT and Windows SDK
# that xwin downloads, so the result links against the same runtime as an
# MSVC build. Run from the repository root in ubuntu:24.04, as root:
#
#   BUILD_DIR=build/windows-x86_64 NUMPROC=8 .ci/build-windows-cross.sh
#
# leaves the core at $BUILD_DIR/parallel_xbox360_libretro.dll.

set -euo pipefail

BUILD_DIR=${BUILD_DIR:-build/windows-x86_64}
NUMPROC=${NUMPROC:-$(nproc)}
[ "$NUMPROC" -gt 0 ] || NUMPROC=1
TOOLS=${XE_CROSS_TOOLS:-/opt/xe-cross}
LLVM=/usr/lib/llvm-21/bin
XWIN_VERSION=0.10.0
DXC_URL=https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609
DXC_WINDOWS=dxc_2026_09_29.zip

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-install-recommends \
  ca-certificates wget gnupg lsb-release software-properties-common \
  git python3 python3-venv ninja-build make file unzip xz-utils
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key > /etc/apt/trusted.gpg.d/apt.llvm.org.asc
apt-add-repository -y --no-update "deb http://apt.llvm.org/noble/ llvm-toolchain-noble-21 main"
apt-get update -qq
apt-get install -y --no-install-recommends \
  clang-21 clang-tools-21 lld-21 llvm-21 libclang-rt-21-dev g++-14 liblz4-dev

# Meson and Mesa's Python modules; 24.04's pip wants a venv
if [ ! -x "$TOOLS/venv/bin/meson" ]; then
  python3 -m venv "$TOOLS/venv"
  "$TOOLS/venv/bin/pip" install -q --upgrade meson mako pyyaml packaging
fi
export PATH="$TOOLS/venv/bin:$PATH"

# The MSVC CRT and the Windows SDK. Clang's VFS resolves include spelling.
mkdir -p "$TOOLS"
TOOLS=$(cd "$TOOLS" && pwd -P)
# Fetched once for this xwin version; kept when TOOLS comes back from the
# buildbot's cache (.gitlab-ci.yml)
MSVC="$TOOLS/msvc"
if [ ! -f "$MSVC/.splat-$XWIN_VERSION" ]; then
  mkdir -p "$TOOLS"
  rm -rf "$MSVC"
  wget -q -O "$TOOLS/xwin.tar.gz" \
    "https://github.com/Jake-Shadle/xwin/releases/download/$XWIN_VERSION/xwin-$XWIN_VERSION-x86_64-unknown-linux-musl.tar.gz"
  tar -xzf "$TOOLS/xwin.tar.gz" -C "$TOOLS" --strip-components=1
  "$TOOLS/xwin" --accept-license --arch x86_64 --cache-dir "$TOOLS/xwin-cache" \
    splat --output "$MSVC"
  rm -rf "$TOOLS/xwin-cache" "$TOOLS/xwin.tar.gz"
  touch "$MSVC/.splat-$XWIN_VERSION"
fi

# The existing D3D12 backend still embeds the Windows DXIL validator.
if [ ! -f "$TOOLS/dxil.dll.$DXC_WINDOWS" ]; then
  wget -q -O "$TOOLS/dxc-windows.zip" "$DXC_URL/$DXC_WINDOWS"
  unzip -p "$TOOLS/dxc-windows.zip" 'bin?x64?dxil.dll' > "$TOOLS/dxil.dll"
  rm -f "$TOOLS/dxc-windows.zip"
  touch "$TOOLS/dxil.dll.$DXC_WINDOWS"
fi
export XENIA_LIBRETRO_DXIL="$TOOLS/dxil.dll"

# Native C89 tools embed the checked-in data and provide case-insensitive
# SDK lookup through Clang's VFS, without creating aliases in the SDK tree.
HOST_TOOLS="$BUILD_DIR/host"
make host-tools HOST_CC="$LLVM/clang" HOST_DIR="$HOST_TOOLS"
SDK_OVERLAY="$TOOLS/sdk-overlay.json"
"$HOST_TOOLS/core-codegen" sdk-overlay "$SDK_OVERLAY" "$MSVC"
export SDK_OVERLAY
# clang-cl against the downloaded headers and libraries. For meson (Mesa) a
# clang-cl that brings them along itself: its resource compiler step takes
# the compiler as one argument. Mesa's MSVC atomics also use MSVC's
# lower-case _interlocked* forms, which clang-cl's intrin.h does not have:
# the documented upper-case intrinsics in their place.
mkdir -p "$TOOLS/bin"
cat > "$TOOLS/bin/clang-cl" <<WRAPPER
#!/bin/sh
exec "$LLVM/clang-cl" --target=x86_64-pc-windows-msvc -fuse-ld=lld \\
  -Wno-unused-command-line-argument \\
  -vfsoverlay "$SDK_OVERLAY" \\
  /imsvc "$MSVC/crt/include" /imsvc "$MSVC/sdk/include/ucrt" \\
  /imsvc "$MSVC/sdk/include/um" /imsvc "$MSVC/sdk/include/shared" \\
  /imsvc "$MSVC/sdk/include/winrt" \\
  -D_interlockedexchange64=_InterlockedExchange64 \\
  -D_interlockedexchangeadd64=_InterlockedExchangeAdd64 \\
  -D_interlockedincrement64=_InterlockedIncrement64 \\
  -D_interlockeddecrement64=_InterlockedDecrement64 \\
  "-D_interlockedadd(a,v)=(_InterlockedExchangeAdd((a),(v))+(v))" \\
  "-D_interlockedadd64(a,v)=(_InterlockedExchangeAdd64((a),(v))+(v))" \\
  "\$@"
WRAPPER
chmod +x "$TOOLS/bin/clang-cl"
# and an llvm-rc that knows the SDK's headers (Mesa's zlib resource)
cat > "$TOOLS/bin/llvm-rc" <<WRAPPER
#!/bin/sh
exec "$LLVM/llvm-rc" /I "$MSVC/crt/include" /I "$MSVC/sdk/include/ucrt" \\
  /I "$MSVC/sdk/include/um" /I "$MSVC/sdk/include/shared" "\$@"
WRAPPER
chmod +x "$TOOLS/bin/llvm-rc"
# Mesa still uses its upstream Meson build; the core and native embedding
# tool are compiled directly by GNU Make.
MESA_CROSS_FILE="$TOOLS/mesa-windows-x86_64.ini"
cat > "$MESA_CROSS_FILE" <<EOF
[binaries]
c = '$TOOLS/bin/clang-cl'
cpp = '$TOOLS/bin/clang-cl'
windres = '$TOOLS/bin/llvm-rc'
ar = '$LLVM/llvm-lib'

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF
export PATH="$LLVM:$PATH"
export CC_FOR_BUILD=clang-21 CXX_FOR_BUILD=clang++-21
export LIB="$MSVC/crt/lib/x86_64;$MSVC/sdk/lib/um/x86_64;$MSVC/sdk/lib/ucrt/x86_64"
# The Make build has no configure step; CPU and shader sources are selected in
# Makefile.common. Built-in shader modules and game data are checked in.
make -j "$NUMPROC" platform=windows-x86_64 WINDOWS_ABI=msvc BUILD_DIR="$BUILD_DIR" \
  CC="$LLVM/clang" CXX="$LLVM/clang++" HOST_CC="$LLVM/clang" HOST_DIR="$HOST_TOOLS" \
  MSVC_ROOT="$MSVC" MESA_CROSS_FILE="$MESA_CROSS_FILE" \
  AR="$LLVM/llvm-ar" LLVM_RC="$TOOLS/bin/llvm-rc"
mv parallel_xbox360_libretro.dll "$BUILD_DIR/parallel_xbox360_libretro.dll"
file "$BUILD_DIR/parallel_xbox360_libretro.dll"
