# GNU Make build for the libretro core. No CMake configure/generate step.
# A POSIX shell, Python 3 and clang 21 (GCC 14 libstdc++ on Linux) are required.
SHELL := /bin/sh
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

TARGET_NAME := parallel_xbox360
platform ?= unix
ifeq ($(platform),unix)
override platform := linux-$(shell uname -m)
endif
ifeq ($(platform),unix-arm64)
override platform := linux-aarch64
endif
ifeq ($(platform),windows)
override platform := windows-x86_64
endif
SUPPORTED_PLATFORMS := linux-x86_64 linux-aarch64 windows-x86_64 android-arm64-v8a android-x86_64
ifeq ($(filter $(platform),$(SUPPORTED_PLATFORMS)),)
$(error Unsupported platform '$(platform)'; choose $(SUPPORTED_PLATFORMS))
endif
OS := linux
ARCH := x86_64
ifneq ($(filter linux-aarch64 android-arm64-v8a,$(platform)),)
ARCH := aarch64
endif
ifneq ($(filter android-%,$(platform)),)
OS := android
endif
ifeq ($(platform),windows-x86_64)
OS := windows
endif
BUILD_DIR ?= build/make/$(platform)
GEN := $(BUILD_DIR)/generated
HOST_DIR ?= build/make/host
PYTHON ?= python3
HOST_CXX ?= clang++
HOST_CXXFLAGS ?= -O2
HOST_LDFLAGS ?=
HOST_SHADER_CC ?= $(HOST_DIR)/xenia-shader-cc
SLANGC_PATH ?= $(firstword $(wildcard .slang/*/bin/slangc .slang/*/slangc))
export SLANGC_PATH
API_LEVEL ?= 28
NDK_ROOT ?= $(ANDROID_NDK_HOME)
NDK_HOST_TAG ?= linux-x86_64
MSVC_ROOT ?=
MESA_BUILD_DIR ?= $(BUILD_DIR)/mesa
MESA_CROSS_FILE ?=
XENIA_LIBRETRO_DXIL ?=
LLVM_RC ?= llvm-rc
DEBUG ?= 0
LTO ?= 0

ifeq ($(origin CC),default)
CC := clang
endif
ifeq ($(origin CXX),default)
CXX := clang++
endif
ifeq ($(origin AR),default)
AR := llvm-ar
endif
ifeq ($(OS),android)
NDK_BIN := $(NDK_ROOT)/toolchains/llvm/prebuilt/$(NDK_HOST_TAG)/bin
ifeq ($(ARCH),aarch64)
NDK_TRIPLE := aarch64-linux-android
else
NDK_TRIPLE := x86_64-linux-android
endif
ifneq ($(origin CC),command line)
CC := $(NDK_BIN)/$(NDK_TRIPLE)$(API_LEVEL)-clang
endif
ifneq ($(origin CXX),command line)
CXX := $(NDK_BIN)/$(NDK_TRIPLE)$(API_LEVEL)-clang++
endif
ifneq ($(origin AR),command line)
AR := $(NDK_BIN)/llvm-ar
endif
endif

TARGET := $(TARGET_NAME)_libretro.so
ifeq ($(OS),android)
TARGET := $(TARGET_NAME)_libretro_android.so
endif
ifeq ($(OS),windows)
TARGET := $(TARGET_NAME)_libretro.dll
endif

include Makefile.common

DEFINES := -DXENIA_LIBRETRO=1 -DVULKAN_HPP_NO_TO_STRING -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS \
 -DIMGUI_DISABLE_DEFAULT_FONT -DIMGUI_USE_STB_SPRINTF -DUSE_CPP17 -D_LIB \
 -DASIO_STANDALONE -DASIO_NO_DEPRECATED -DXBYAK_NO_OP_NAMES -DXBYAK_ENABLE_OMITTED_OPERAND
VERSION := $(shell head -1 upstream.version)
COMMIT := $(shell git rev-parse --short=7 HEAD 2>/dev/null || echo unknown)
DEFINES += -DXENIA_LIBRETRO_VERSION='"$(VERSION)"' -DXENIA_LIBRETRO_COMMIT='"$(COMMIT)"'
WARNINGS := -Wno-switch -Wno-attributes -Wno-deprecated-register -Wno-deprecated-volatile \
 -Wno-deprecated-enum-enum-conversion -Wno-absolute-value -Wno-deprecated-literal-operator \
 -Wno-nontrivial-memcall -Wno-character-conversion -Wno-unknown-warning-option
BASE_FLAGS := -fPIC -MMD -MP
ifeq ($(ARCH),x86_64)
BASE_FLAGS += -mavx
endif
ifeq ($(DEBUG),1)
BASE_FLAGS += -O0 -g
DEFINES += -DDEBUG -D_NO_DEBUG_HEAP=1
else
BASE_FLAGS += -O3 -g -finline-functions -funroll-loops -fomit-frame-pointer
DEFINES += -DNDEBUG -D_NO_DEBUG_HEAP=1
endif
ifeq ($(LTO),1)
BASE_FLAGS += -flto=thin
LDFLAGS += -flto=thin -fuse-ld=lld
endif
CPPFLAGS += $(addprefix -I,$(INCLUDE_DIRS)) $(DEFINES)
CFLAGS += -std=c17
CXXFLAGS += -std=c++20 $(WARNINGS)
LDFLAGS += -shared
ifeq ($(OS),linux)
LDLIBS += -lstdc++fs -ldl -lm -lpthread -lrt -lfontconfig
LDFLAGS += -Wl,-Bsymbolic -Wl,--no-undefined -Wl,--version-script=libretro/link.T -Wl,--exclude-libs,ALL
endif
ifeq ($(OS),android)
LDFLAGS += -static-libstdc++ -Wl,-Bsymbolic -Wl,-z,max-page-size=16384
LDLIBS += -landroid -llog -ldl -lm
endif
ifeq ($(OS),windows)
BASE_FLAGS := $(filter-out -fPIC,$(BASE_FLAGS)) --target=x86_64-pc-windows-msvc
BASE_FLAGS += -D_MT -D_DLL -Xclang --dependent-lib=msvcrt
DEFINES += -DWIN32 -D_WIN64=1 -D_AMD64=1 -DUNICODE -D_UNICODE \
 -D_CRT_NONSTDC_NO_DEPRECATE -D_CRT_SECURE_NO_WARNINGS -D_WIN32_WINNT=0x0A00
ifneq ($(MSVC_ROOT),)
BASE_FLAGS += $(addprefix -isystem ,$(addprefix $(MSVC_ROOT)/,crt/include sdk/include/ucrt sdk/include/um sdk/include/shared sdk/include/winrt))
LDFLAGS += $(addprefix -L,$(addprefix $(MSVC_ROOT)/,crt/lib/x86_64 sdk/lib/um/x86_64 sdk/lib/ucrt/x86_64))
endif
CXXFLAGS += -fexceptions -fcxx-exceptions
LDFLAGS += --target=x86_64-pc-windows-msvc -fuse-ld=lld -Wl,/manifest:no,/DEBUG,/OPT:REF,/OPT:ICF,/INCREMENTAL:NO
LDLIBS += ntdll.lib wsock32.lib ws2_32.lib xinput.lib comctl32.lib shcore.lib \
 shlwapi.lib dxguid.lib bcrypt.lib d3d12.lib dxgi.lib synchronization.lib version.lib
INCLUDE_DIRS += third_party/mesa/src/microsoft/spirv_to_dxil third_party/mesa/src/microsoft/compiler
INCLUDE_DIRS += third_party/libusb/libusb
MESA_LIBS := src/microsoft/spirv_to_dxil/libspirv_to_dxil.a \
 src/microsoft/compiler/libdxil_compiler.a src/compiler/spirv/libvtn.a \
 src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a \
 src/util/libmesa_util_simd.a src/c11/impl/libmesa_util_c11.a \
 src/util/blake3/libblake3.a subprojects/zlib-1.3.1/libz.a
MESA_ARCHIVES := $(addprefix $(MESA_BUILD_DIR)/,$(MESA_LIBS))
LDLIBS += $(MESA_ARCHIVES)
endif

OBJECTS := $(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(SOURCES_C) $(SOURCES_CXX) $(SOURCES_ASM)))
FRONTEND_OBJECTS := $(filter $(BUILD_DIR)/obj/libretro/%,$(OBJECTS))
CORE_OBJECTS := $(filter-out $(FRONTEND_OBJECTS),$(OBJECTS))
CORE_ARCHIVE := $(BUILD_DIR)/core.a
CONFIG_INPUTS := tools/build/make_generated.py tools/build/embed_bundle.py xenia-build.py \
 upstream.version $(wildcard assets/font/* libretro/data/*) \
 third_party/snappy/snappy-stubs-public.h.in third_party/snappy/cmake/config.h.in \
 $(wildcard third_party/zlib-ng/*-ng.h.in) third_party/zlib-ng/gzread.c.in \
 third_party/glslang/build_info.h.tmpl third_party/glslang/CHANGES.md

.PHONY: all clean generated shaders host-tools mesa help print-config FORCE $(TARGET)
all: $(TARGET)
$(TARGET): $(BUILD_DIR)/$(TARGET)
	cp "$<" "$@"

# Run the content-preserving generator each invocation: patch filenames may
# contain spaces, and additions/removals must also update the embedded bundle.
generated: $(CONFIG_INPUTS)
	@mkdir -p "$(GEN)"
	$(PYTHON) tools/build/make_generated.py inputs "$(GEN)" --arch $(ARCH) $(if $(filter windows,$(OS)),--windows)

$(GEN)/embedded_font.cc $(GEN)/embedded_bundle_patches.cc $(GEN)/libretro_gamedb_data.cc $(GEN)/third_party/zlib-ng/gzread.c: | generated
	@test -f "$@"

export MAKE_BUILD_SIGNATURE := $(CC)|$(CXX)|$(BASE_FLAGS)|$(CPPFLAGS)|$(CFLAGS)|$(CXXFLAGS)|$(LDFLAGS)|$(LDLIBS)|$(XENIA_LIBRETRO_DXIL)
$(BUILD_DIR)/config: FORCE
	@$(PYTHON) tools/build/make_generated.py signature "$@"
$(OBJECTS): Makefile Makefile.common $(BUILD_DIR)/config | generated shaders

$(BUILD_DIR)/obj/%.c.o: %.c
	@mkdir -p "$(@D)"
	$(CC) $(BASE_FLAGS) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"
$(BUILD_DIR)/obj/%.cc.o: %.cc
	@mkdir -p "$(@D)"
	$(CXX) $(BASE_FLAGS) $(CPPFLAGS) $(CXXFLAGS) -c "$<" -o "$@"
$(BUILD_DIR)/obj/%.cpp.o: %.cpp
	@mkdir -p "$(@D)"
	$(CXX) $(BASE_FLAGS) $(CPPFLAGS) $(CXXFLAGS) -c "$<" -o "$@"
$(BUILD_DIR)/obj/%.S.o: %.S
	@mkdir -p "$(@D)"
	$(CC) $(BASE_FLAGS) $(CPPFLAGS) -DBOOST_CONTEXT_EXPORT= -c "$<" -o "$@"

$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter third_party/%,$(SOURCES_C) $(SOURCES_CXX)))): CPPFLAGS += -w
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter third_party/capstone/%,$(SOURCES_C)))): CPPFLAGS += $(CAPSTONE_DEFS) -DCAPSTONE_USE_SYS_DYN_MEM -Ithird_party/capstone
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter src/xenia/cpu/backend/%,$(SOURCES_CXX)))): CPPFLAGS += $(CAPSTONE_DEFS) -DCAPSTONE_USE_SYS_DYN_MEM
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter third_party/mspack/% third_party/snappy/%,$(SOURCES_C) $(SOURCES_CXX)))): CPPFLAGS += -DHAVE_CONFIG_H
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter third_party/snappy/%,$(SOURCES_CXX)))): CPPFLAGS := -I$(GEN)/snappy $(CPPFLAGS) -w -DHAVE_CONFIG_H
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter third_party/libusb/%,$(SOURCES_C)))): CPPFLAGS := -Ithird_party/libusb/msvc $(CPPFLAGS) -w
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(ZSTD_SOURCES))): CPPFLAGS += -DXXH_NAMESPACE=ZSTD_ -DZSTD_LEGACY_SUPPORT=0 -DZSTD_DISABLE_ASM
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(ZLIB_SOURCES) $(GEN)/third_party/zlib-ng/gzread.c)): CPPFLAGS += -DHAVE_BUILTIN_CTZ -DHAVE_BUILTIN_CTZLL -DHAVE_ATTRIBUTE_ALIGNED -DWITH_GZFILEOP
ifneq ($(OS),windows)
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(ZLIB_SOURCES) $(GEN)/third_party/zlib-ng/gzread.c)): CPPFLAGS += -DHAVE_UNISTD_H
endif
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(FFMPEG_SOURCES))): CPPFLAGS += -DHAVE_AV_CONFIG_H -D_USE_MATH_DEFINES -Ithird_party/FFmpeg/libavcodec
ifneq ($(OS),windows)
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(FFMPEG_SOURCES))): CPPFLAGS += -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Ithird_party/FFmpeg/compat/atomics/gcc
else
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(FFMPEG_SOURCES))): CPPFLAGS += -Ithird_party/FFmpeg/compat/atomics/win32
endif

ifeq ($(OS),windows)
ifneq ($(XENIA_LIBRETRO_DXIL),)
RESOURCES := $(BUILD_DIR)/dxil.res
CPPFLAGS += -DXENIA_LIBRETRO_DXIL
$(BUILD_DIR)/dxil.rc: $(XENIA_LIBRETRO_DXIL) Makefile
	@mkdir -p "$(@D)"
	@printf 'XENIA_DXIL RCDATA "%s"\n' '$(abspath $(XENIA_LIBRETRO_DXIL))' > "$@"
$(BUILD_DIR)/dxil.res: $(BUILD_DIR)/dxil.rc $(XENIA_LIBRETRO_DXIL)
	$(LLVM_RC) /fo "$@" "$<"
endif
endif

$(CORE_ARCHIVE): export MAKE_RESPONSE := $(foreach obj,$(CORE_OBJECTS),"$(obj)")
$(CORE_ARCHIVE): $(CORE_OBJECTS)
	@$(PYTHON) tools/build/make_generated.py response "$(BUILD_DIR)/archive.rsp"
	rm -f "$@"
	$(AR) qcs "$@" @$(BUILD_DIR)/archive.rsp

# Keep the frontend's exported entry points as ordinary objects. Archive
# extraction pulls in only the emulator/dependency members they actually use,
# just as the existing build's static libraries do.
$(BUILD_DIR)/$(TARGET): export MAKE_RESPONSE := $(foreach obj,$(FRONTEND_OBJECTS) $(RESOURCES) $(CORE_ARCHIVE),"$(obj)")
$(BUILD_DIR)/$(TARGET): $(FRONTEND_OBJECTS) $(CORE_ARCHIVE) $(RESOURCES) libretro/link.T $(MESA_ARCHIVES)
	@$(PYTHON) tools/build/make_generated.py response "$(BUILD_DIR)/objects.rsp"
	$(CXX) $(LDFLAGS) -o "$@" @$(BUILD_DIR)/objects.rsp $(LDLIBS)

HOST_OBJECTS := $(addprefix $(HOST_DIR)/obj/,$(addsuffix .o,$(HOST_SOURCES)))
export HOST_BUILD_SIGNATURE := $(HOST_CXX)|$(HOST_CXXFLAGS)|$(HOST_LDFLAGS)|$(HOST_INCLUDE_DIRS)
$(HOST_DIR)/config: FORCE
	@$(PYTHON) tools/build/make_generated.py signature "$@" --host
$(HOST_OBJECTS): $(HOST_DIR)/config
HOST_TABLE_INPUTS := $(wildcard third_party/SPIRV-Tools/utils/*.py third_party/SPIRV-Headers/include/spirv/unified1/*.json) \
 third_party/glslang/build_info.h.tmpl third_party/glslang/CHANGES.md \
 third_party/SPIRV-Tools/CHANGES third_party/SPIRV-Headers/include/spirv/spir-v.xml tools/build/make_generated.py
$(HOST_DIR)/generated/tables.stamp: $(HOST_TABLE_INPUTS)
	$(PYTHON) tools/build/make_generated.py spirv-tables "$(HOST_DIR)/generated"
	@touch "$@"
$(HOST_DIR)/obj/%.cc.o: %.cc | $(HOST_DIR)/generated/tables.stamp
	@mkdir -p "$(@D)"
	$(HOST_CXX) $(HOST_CXXFLAGS) -std=c++20 -MMD -MP $(addprefix -I,$(HOST_INCLUDE_DIRS)) -DENABLE_OPT=1 -c "$<" -o "$@"
$(HOST_DIR)/obj/%.cpp.o: %.cpp | $(HOST_DIR)/generated/tables.stamp
	@mkdir -p "$(@D)"
	$(HOST_CXX) $(HOST_CXXFLAGS) -std=c++20 -MMD -MP $(addprefix -I,$(HOST_INCLUDE_DIRS)) -DENABLE_OPT=1 -c "$<" -o "$@"
$(HOST_DIR)/xenia-shader-cc: export MAKE_RESPONSE := $(foreach obj,$(HOST_OBJECTS),"$(obj)")
$(HOST_DIR)/xenia-shader-cc: $(HOST_OBJECTS)
	@$(PYTHON) tools/build/make_generated.py response "$(HOST_DIR)/objects.rsp"
	$(HOST_CXX) $(HOST_LDFLAGS) -o "$@" @$(HOST_DIR)/objects.rsp -lpthread
host-tools: $(HOST_SHADER_CC)

define shader-rule
$(call shader-output,$(1),$(2)): $(1) $(HOST_SHADER_CC) tools/build/make_generated.py
	$$(PYTHON) tools/build/make_generated.py shader "$$@" --source "$$<" --backend $(3) --compiler "$$(HOST_SHADER_CC)"
endef
$(foreach s,$(LEGACY_SHADERS) $(SPIRV_SLANG),$(eval $(call shader-rule,$(s),vulkan_spirv,spirv)))
ifeq ($(OS),windows)
$(foreach s,$(SLANG_SHADERS),$(eval $(call shader-rule,$(s),d3d12_dxil,dxil)))
endif
shaders: $(SHADER_HEADERS)

ifeq ($(OS),windows)
$(MESA_BUILD_DIR)/build.ninja:
	meson setup "$(MESA_BUILD_DIR)" third_party/mesa $(if $(MESA_CROSS_FILE),--cross-file "$(MESA_CROSS_FILE)") \
	 --default-library=static --buildtype=release -Db_vscrt=md -Dc_args=/wd4189 -Dcpp_args=/wd4189 \
	 -Dspirv-to-dxil=true -Dgallium-drivers= -Dvulkan-drivers= -Dplatforms= \
	 -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dopengl=false \
	 -Dgles1=disabled -Dgles2=disabled -Dllvm=disabled -Dvideo-codecs=
mesa: $(MESA_BUILD_DIR)/build.ninja
	ninja -C "$(MESA_BUILD_DIR)" $(MESA_LIBS)
$(MESA_ARCHIVES): | mesa
	@test -f "$@"
endif

print-config:
	@echo "platform=$(platform) OS=$(OS) ARCH=$(ARCH) TARGET=$(TARGET)"
	@echo "CC=$(CC) CXX=$(CXX) HOST_CXX=$(HOST_CXX)"
	@echo "BUILD_DIR=$(BUILD_DIR) HOST_DIR=$(HOST_DIR)"
help:
	@echo "make platform=<$(SUPPORTED_PLATFORMS)> [-jN] [DEBUG=1] [LTO=1]"
	@echo "Android: NDK_ROOT=/path/to/ndk [API_LEVEL=28]"
	@echo "Windows cross build: see .ci/build-windows-cross.sh"
clean:
	$(PYTHON) -c "import pathlib,shutil; p=pathlib.Path('$(BUILD_DIR)').resolve(); root=pathlib.Path.cwd().resolve(); assert root in p.parents and p != root / 'build', 'unsafe BUILD_DIR'; shutil.rmtree(p, ignore_errors=True)"
	rm -f "$(TARGET)"

-include $(OBJECTS:.o=.d) $(HOST_OBJECTS:.o=.d) $(addsuffix .d,$(SHADER_HEADERS))
