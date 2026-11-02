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
DXC_LINUX=linux_dxc_2026_09_28.x86_x64.tar.gz
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

# The MSVC CRT and the Windows SDK, laid out with lower-case links so the
# case-sensitive file system finds <Windows.h> as well as <windows.h>
mkdir -p "$TOOLS"
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

# DXC: on Linux for slangc, which compiles the D3D12 shaders to DXIL with
# it, and dxil.dll from the Windows release, which the core carries inside
# (as the project's CI does)
if [ ! -f "$TOOLS/dxc-linux/.$DXC_LINUX" ]; then
  rm -rf "$TOOLS/dxc-linux"
  mkdir -p "$TOOLS/dxc-linux"
  wget -q -O "$TOOLS/dxc-linux.tar.gz" "$DXC_URL/$DXC_LINUX"
  tar -xzf "$TOOLS/dxc-linux.tar.gz" -C "$TOOLS/dxc-linux"
  rm -f "$TOOLS/dxc-linux.tar.gz"
  touch "$TOOLS/dxc-linux/.$DXC_LINUX"
fi
export LD_LIBRARY_PATH="$TOOLS/dxc-linux/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [ ! -f "$TOOLS/dxil.dll.$DXC_WINDOWS" ]; then
  wget -q -O "$TOOLS/dxc-windows.zip" "$DXC_URL/$DXC_WINDOWS"
  python3 -c "import sys, zipfile; open(sys.argv[2], 'wb').write(zipfile.ZipFile(sys.argv[1]).read(r'bin\x64\dxil.dll'))" \
    "$TOOLS/dxc-windows.zip" "$TOOLS/dxil.dll"
  rm -f "$TOOLS/dxc-windows.zip"
  touch "$TOOLS/dxil.dll.$DXC_WINDOWS"
fi
export XENIA_LIBRETRO_DXIL="$TOOLS/dxil.dll"

# The submodules the Windows build uses, as the project's CI takes them
git submodule update --init --depth=1 --jobs="$NUMPROC" \
  $(grep -oP '(?<=path = )(?!third_party/(MoltenVK|SPIRV-Cross)$).+' .gitmodules)
# Headers the sources name in another case than the SDK's files (<ObjBase.h>
# for objbase.h): a link of that spelling next to each
python3 - "$MSVC" <<'PY'
import os, re, sys
root = sys.argv[1]
dirs = [os.path.join(root, d) for d in ("crt/include", "sdk/include/ucrt",
        "sdk/include/um", "sdk/include/shared", "sdk/include/winrt")]
files = {}
for d in dirs:
    for name in os.listdir(d):
        files.setdefault(name.lower(), (d, name))
wanted = set()
pattern = re.compile(rb'#\s*include\s*[<"]([A-Za-z0-9_]+\.[hH])[>"]')
for top in ("src", "libretro", "third_party"):
    for base, _, names in os.walk(top):
        for n in names:
            if n.endswith((".h", ".hpp", ".c", ".cc", ".cpp", ".inl")):
                try:
                    data = open(os.path.join(base, n), "rb").read()
                except OSError:
                    continue
                wanted.update(m.decode() for m in pattern.findall(data))
made = 0
for name in wanted:
    hit = files.get(name.lower())
    if hit and hit[1] != name and not os.path.exists(os.path.join(hit[0], name)):
        os.symlink(hit[1], os.path.join(hit[0], name))
        made += 1
print(f"case links: {made}")
PY
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
# Mesa still uses its upstream Meson build; the core and host shader compiler
# are compiled directly by GNU Make.
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
# Makefile.common. Slang and the game patch data are preparation tools only.
./xenia-build.py slang
./xenia-build.py fetchdata
make -j "$NUMPROC" platform=windows-x86_64 BUILD_DIR="$BUILD_DIR" \
  CC="$LLVM/clang" CXX="$LLVM/clang++" HOST_CXX="$LLVM/clang++" \
  MSVC_ROOT="$MSVC" MESA_CROSS_FILE="$MESA_CROSS_FILE" \
  AR="$LLVM/llvm-ar" LLVM_RC="$TOOLS/bin/llvm-rc"
mv parallel_xbox360_libretro.dll "$BUILD_DIR/parallel_xbox360_libretro.dll"
file "$BUILD_DIR/parallel_xbox360_libretro.dll"
