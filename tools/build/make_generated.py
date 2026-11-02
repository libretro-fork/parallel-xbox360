#!/usr/bin/env python3
"""Generate build inputs for the GNU Make build (no build-system generator)."""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8", newline="\n")


def template(source, destination, values):
    text = source.read_text(encoding="utf-8")
    for key, value in values.items():
        text = text.replace("@" + key + "@", str(value))
        text = text.replace("${" + key + "}", str(value))
    write(destination, text)


def bytes_cpp(data):
    return ",".join(str(b) for b in data) + ",0"


def inputs(out, windows, arch):
    # Generate into an include overlay, never into a submodule. Different
    # architectures / operating systems can then build concurrently.
    zlib = ROOT / "third_party/zlib-ng"
    for name in ("zlib-ng.h", "zconf-ng.h", "zlib_name_mangling-ng.h", "gzread.c"):
        template(zlib / (name + ".in"), out / "third_party/zlib-ng" / name,
                 {"ZLIB_SYMBOL_PREFIX": ""})
    snappy = ROOT / "third_party/snappy"
    version = re.search(r"project\(Snappy VERSION (\d+)\.(\d+)\.(\d+)",
                        (snappy / "CMakeLists.txt").read_text()).groups()
    values = dict(zip(("PROJECT_VERSION_MAJOR", "PROJECT_VERSION_MINOR",
                       "PROJECT_VERSION_PATCH"), version))
    values["HAVE_SYS_UIO_H_01"] = int(not windows)
    template(snappy / "snappy-stubs-public.h.in",
             out / "snappy-stubs-public.h", values)
    config = (snappy / "cmake/config.h.in").read_text()
    enabled = {"HAVE_ATTRIBUTE_ALWAYS_INLINE", "HAVE_BUILTIN_CTZ",
               "HAVE_BUILTIN_EXPECT", "HAVE_BUILTIN_PREFETCH"}
    enabled.add("SNAPPY_HAVE_SSSE3" if arch == "x86_64" else "SNAPPY_HAVE_NEON")
    if arch == "x86_64":
        enabled.add("SNAPPY_HAVE_X86_CRC32")  # -mavx implies SSE4.2.
    if windows:
        enabled.add("HAVE_WINDOWS_H")
    else:
        enabled.update(("HAVE_FUNC_MMAP", "HAVE_FUNC_SYSCONF", "HAVE_SYS_MMAN_H",
                        "HAVE_SYS_RESOURCE_H", "HAVE_SYS_TIME_H", "HAVE_SYS_UIO_H",
                        "HAVE_UNISTD_H"))
    config = re.sub(r"#cmakedefine01 (\w+)",
                    lambda m: f"#define {m[1]} {int(m[1] in enabled)}", config)
    write(out / "snappy/config.h", config)
    glslang_header(out)

    header = "#pragma once\n#include <cstddef>\nnamespace xe::ui::embedded_font {\n"
    source = '#include "embedded_font.h"\nnamespace xe::ui::embedded_font {\n'
    header += "struct Asset { const char* name; const unsigned char* data; size_t size; };\n"
    entries = []
    for asset in sorted((ROOT / "assets/font").iterdir()):
        if not asset.is_file():
            continue
        symbol = re.sub(r"[^A-Za-z0-9_]", "_", asset.name)
        if symbol[0].isdigit():
            symbol = "_" + symbol
        data = asset.read_bytes()
        header += f"extern const unsigned char {symbol}_data[];\nextern const size_t {symbol}_size;\n"
        source += f"const unsigned char {symbol}_data[] = {{{bytes_cpp(data)}}};\n"
        source += f"const size_t {symbol}_size = {len(data)};\n"
        entries.append(f'{{"{asset.name}", {symbol}_data, {len(data)}}}')
    header += "extern const Asset kAll[];\nextern const size_t kAllCount;\n}\n"
    source += "const Asset kAll[] = {" + ",".join(entries) + "};\n"
    source += "const size_t kAllCount = sizeof(kAll) / sizeof(kAll[0]);\n}\n"
    write(out / "embedded_font.h", header)
    write(out / "embedded_font.cc", source)
    data = (ROOT / "libretro/data/xenia_framerate_unlock.txt").read_bytes()
    write(out / "libretro_gamedb_data.cc", "#include <cstddef>\n"
          f"extern const unsigned char xenia_libretro_framerate_unlock[] = {{{bytes_cpp(data)}}};\n"
          f"extern const size_t xenia_libretro_framerate_unlock_size = {len(data)};\n")
    patches = ROOT / "build/data_repos/game-patches/patches"
    if not patches.is_dir():
        raise SystemExit("Missing game patches: run ./xenia-build.py fetchdata before make")
    subprocess.run([sys.executable, str(ROOT / "tools/build/embed_bundle.py"),
                    str(patches),
                    str(out), "patches"], check=True)
    spec = importlib.util.spec_from_file_location("xb", ROOT / "xenia-build.py")
    xb = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(xb)
    xb.generate_version_h(str(out))


def glslang_header(out):
    glslang = ROOT / "third_party/glslang"
    version = re.search(r"#+ *(\d+)\.(\d+)\.(\d+)(?:-([a-zA-Z0-9]+))?",
                        (glslang / "CHANGES.md").read_text()).groups()
    template(glslang / "build_info.h.tmpl", out / "glslang/build_info.h",
             dict(zip(("major", "minor", "patch", "flavor"),
                      [v or "" for v in version])))


def spirv_tables(out):
    tools = ROOT / "third_party/SPIRV-Tools"
    grammar = ROOT / "third_party/SPIRV-Headers/include/spirv/unified1"
    out.mkdir(parents=True, exist_ok=True)
    glslang_header(out)

    def run(script, *args):
        subprocess.run([sys.executable, str(tools / "utils" / script), *map(str, args)], check=True)

    args = [f"--core-tables-body-output={out}/core_tables_body.inc",
            f"--core-tables-header-output={out}/core_tables_header.inc",
            f"--spirv-core-grammar={grammar}/spirv.core.grammar.json"]
    for prefix, name in (("", "glsl.std.450"), ("", "opencl.std.100"),
                         ("CLDEBUG100_", "opencl.debuginfo.100"),
                         ("SHDEBUG100_", "nonsemantic.shader.debuginfo.100"),
                         ("", "spv-amd-shader-explicit-vertex-parameter"),
                         ("", "spv-amd-shader-trinary-minmax"),
                         ("", "spv-amd-gcn-shader"), ("", "spv-amd-shader-ballot"),
                         ("", "debuginfo"), ("", "nonsemantic.clspvreflection"),
                         ("", "nonsemantic.vkspreflection"), ("TOSA_", "tosa.001000.1")):
        args.append(f"--extinst={prefix},{grammar}/extinst.{name}.grammar.json")
    run("ggt.py", *args)
    for name, filename in (("DebugInfo", "debuginfo"),
                           ("OpenCLDebugInfo100", "opencl.debuginfo.100"),
                           ("NonSemanticShaderDebugInfo100", "nonsemantic.shader.debuginfo.100")):
        run("generate_language_headers.py", f"--extinst-grammar={grammar}/extinst.{filename}.grammar.json",
            f"--extinst-output-path={out}/{name}.h")
    run("generate_registry_tables.py", f"--xml={grammar.parent}/spir-v.xml",
        f"--generator-output={out}/generators.inc")
    run("update_build_version.py", tools / "CHANGES", out / "build-version.inc")


def shader(source, destination, backend, compiler):
    destination.parent.mkdir(parents=True, exist_ok=True)
    args = [compiler]
    if source.suffix == ".slang":
        args.append("--slang-dxil" if backend == "dxil" else "--slang-spirv")
    args += ["--depfile", str(destination) + ".d", str(source), str(destination)]
    subprocess.run(args, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("inputs", "spirv-tables", "shader", "signature", "response"))
    parser.add_argument("output", type=Path)
    parser.add_argument("--windows", action="store_true")
    parser.add_argument("--arch", choices=("x86_64", "aarch64"), default="x86_64")
    parser.add_argument("--host", action="store_true")
    parser.add_argument("--source", type=Path)
    parser.add_argument("--backend", choices=("spirv", "dxil"), default="spirv")
    parser.add_argument("--compiler")
    args = parser.parse_args()
    if args.action == "inputs":
        inputs(args.output, args.windows, args.arch)
    elif args.action == "spirv-tables":
        spirv_tables(args.output)
    elif args.action == "signature":
        write(args.output, os.environ["HOST_BUILD_SIGNATURE" if args.host else "MAKE_BUILD_SIGNATURE"])
    elif args.action == "response":
        write(args.output, os.environ["MAKE_RESPONSE"] + "\n")
    else:
        shader(args.source, args.output, args.backend, args.compiler)


if __name__ == "__main__":
    main()
