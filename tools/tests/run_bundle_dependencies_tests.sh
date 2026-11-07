#!/bin/sh
# Actual C89 generator and GNU Make incremental dependency behavior.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) suffix=.exe ;; *) suffix= ;; esac
generator=${CORE_CODEGEN:-"$root/build/make/host/core-codegen$suffix"}
out="$root/build/tests/bundle-dependencies/fixture-$$"
mkdir -p "$out/libretro/data/patches" "$out/third_party/snappy/cmake"
cp "$root/third_party/snappy/CMakeLists.txt" "$root/third_party/snappy/snappy-stubs-public.h.in" "$out/third_party/snappy/"
cp "$root/third_party/snappy/cmake/config.h.in" "$out/third_party/snappy/cmake/"
printf '\n' > "$out/libretro/data/xenia_framerate_unlock.txt"
cd "$out"
patch='libretro/data/patches/Title #1 $variant;extra.patch.toml'
printf '# first\n' > "$patch"
cat > Makefile <<'EOF'
.DEFAULT_GOAL := all
.RECIPEPREFIX := >
.PHONY: all generated
all: bundle.o
-include generated/embedded_bundle_patches.d
generated:
>@"$(GENERATOR)" inputs generated
generated/embedded_bundle_patches.cc: | generated
>@test -f "$@"
bundle.o: generated/embedded_bundle_patches.cc
>@cp "$<" "$@"
>@printf 'rebuilt\n' >> rebuilds
EOF
build() { make -s -j1 "GENERATOR=$generator"; }
count() { test "$(wc -l < rebuilds | tr -d ' ')" = "$1"; }
build; count 1
cp bundle.o previous
printf '# changed\n' >> "$patch"
build; count 2
if cmp -s previous bundle.o; then echo 'stale edited patch bundle' >&2; exit 1; fi
rm "$patch"
build; count 3
printf '# added\n' > "$patch"
build; count 4
build; count 4
echo 'PASS: actual generator/Make cold, edit, removal, addition and no-op builds; spaces, hash, dollar and semicolon in filename'
