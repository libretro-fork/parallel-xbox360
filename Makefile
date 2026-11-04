# GNU Make build for the libretro core. No CMake configure/generate step.
# A POSIX shell, a C89 host compiler and a C++20 target compiler are required.
SHELL := /bin/sh
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

TARGET_NAME := parallel_xbox360
HOST_SYSTEM := $(shell uname -s)
HOST_WINDOWS := $(filter MINGW% MSYS% CYGWIN%,$(HOST_SYSTEM))
HOST_EXEEXT := $(if $(HOST_WINDOWS),.exe)
platform ?= unix
ifeq ($(platform),unix)
ifneq ($(HOST_WINDOWS),)
override platform := windows-x86_64
else ifeq ($(HOST_SYSTEM),Darwin)
override platform := macos-arm64
else
override platform := linux-$(shell uname -m)
endif
endif
ifeq ($(platform),unix-arm64)
override platform := linux-aarch64
endif
ifeq ($(platform),windows)
override platform := windows-x86_64
endif
SUPPORTED_PLATFORMS := linux-x86_64 linux-aarch64 windows-x86_64 android-arm64-v8a android-x86_64 macos-arm64
ifeq ($(filter $(platform),$(SUPPORTED_PLATFORMS)),)
$(error Unsupported platform '$(platform)'; choose $(SUPPORTED_PLATFORMS))
endif
OS := linux
ARCH := x86_64
ifneq ($(filter linux-aarch64 android-arm64-v8a macos-arm64,$(platform)),)
ARCH := aarch64
endif
ifneq ($(filter android-%,$(platform)),)
OS := android
endif
ifeq ($(platform),windows-x86_64)
OS := windows
endif
ifeq ($(platform),macos-arm64)
OS := macos
endif
BUILD_DIR ?= build/make/$(platform)
GEN := $(BUILD_DIR)/generated
HOST_DIR ?= build/make/host
HOST_CC ?= cc
HOST_CFLAGS ?= -O2
CODEGEN := $(HOST_DIR)/core-codegen$(HOST_EXEEXT)
export CODEGEN_BRANCH := $(shell git symbolic-ref --short -q HEAD 2>/dev/null || echo tarball)
export CODEGEN_COMMIT := $(shell git rev-parse HEAD 2>/dev/null || echo unknown)
export CODEGEN_COMMIT_SHORT := $(shell git rev-parse --short=7 HEAD 2>/dev/null || echo unknown)
API_LEVEL ?= 28
NDK_ROOT ?= $(ANDROID_NDK_HOME)
NDK_HOST_TAG ?= linux-x86_64
MSVC_ROOT ?=
WINDOWS_ABI ?= gnu
DEBUG ?= 0
LTO ?= 0

ifeq ($(origin CC),default)
CC := $(CROSS_COMPILE)gcc
endif
ifeq ($(origin CXX),default)
CXX := $(CROSS_COMPILE)g++
endif
ifeq ($(origin AR),default)
AR := $(CROSS_COMPILE)ar
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
ifeq ($(OS),macos)
TARGET := $(TARGET_NAME)_libretro.dylib
ifneq ($(origin AR),command line)
AR := ar
endif
endif

include Makefile.common

DEFINES := -DXENIA_LIBRETRO=1 -DVULKAN_HPP_NO_TO_STRING \
 -DUSE_CPP17 -D_LIB \
 -DASIO_STANDALONE -DASIO_NO_DEPRECATED -DXBYAK_NO_OP_NAMES -DXBYAK_ENABLE_OMITTED_OPERAND
VERSION := $(shell head -1 upstream.version)
DEFINES += -DHAVE_THREADS -DHAVE_THREAD_STORAGE
ifeq ($(WINDOWS_ABI),msvc)
DEFINES += -DRETRO_ATOMIC_FORCE_MSVC
else
DEFINES += -DRETRO_ATOMIC_FORCE_GCC_NEW
endif
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
TARGET_IS_CLANG := $(findstring clang,$(shell $(CC) --version 2>/dev/null))
ifeq ($(TARGET_IS_CLANG),)
WARNINGS := -Wno-switch -Wno-attributes
endif
ifeq ($(LTO),1)
ifeq ($(TARGET_IS_CLANG),)
BASE_FLAGS += -flto
LDFLAGS += -flto
else
BASE_FLAGS += -flto=thin
LDFLAGS += -flto=thin -fuse-ld=lld
ifeq ($(OS),macos)
LDFLAGS := $(filter-out -fuse-ld=lld,$(LDFLAGS))
endif
endif
endif
CPPFLAGS += $(addprefix -I,$(INCLUDE_DIRS)) $(DEFINES)
CFLAGS += -std=c17
CXXFLAGS += -std=c++20 $(WARNINGS)
ifeq ($(OS),macos)
BASE_FLAGS += -mmacosx-version-min=15.0
LDFLAGS += -dynamiclib -mmacosx-version-min=15.0 -Wl,-exported_symbols_list,libretro/exports-macos.T
LDLIBS += -framework CoreFoundation -framework Foundation -framework IOKit -ldl -lpthread
else
LDFLAGS += -shared
endif
ifeq ($(OS),linux)
# Match local binding at link time and permit GCC to inline PM4 helpers.
BASE_FLAGS += -fno-semantic-interposition
LDLIBS += -lstdc++fs -ldl -lm -lpthread -lrt
LDFLAGS += -Wl,-Bsymbolic -Wl,--no-undefined -Wl,--version-script=libretro/link.T -Wl,--exclude-libs,ALL
endif
ifeq ($(OS),android)
LDFLAGS += -static-libstdc++ -Wl,-Bsymbolic -Wl,-z,max-page-size=16384
LDLIBS += -landroid -llog -ldl -lm
endif
ifeq ($(OS),windows)
BASE_FLAGS := $(filter-out -fPIC,$(BASE_FLAGS))
INCLUDE_DIRS += third_party/DirectX-Headers/include/directx
DEFINES += -DWIN32 -D_WIN64=1 -D_AMD64=1 -DUNICODE -D_UNICODE \
 -D_CRT_NONSTDC_NO_DEPRECATE -D_CRT_SECURE_NO_WARNINGS -D_WIN32_WINNT=0x0A00
ifeq ($(WINDOWS_ABI),msvc)
BASE_FLAGS += --target=x86_64-pc-windows-msvc
BASE_FLAGS += -D_MT -D_DLL -Xclang --dependent-lib=msvcrt
ifneq ($(MSVC_ROOT),)
BASE_FLAGS += $(addprefix -isystem ,$(addprefix $(MSVC_ROOT)/,crt/include sdk/include/ucrt sdk/include/um sdk/include/shared sdk/include/winrt))
LDFLAGS += $(addprefix -L,$(addprefix $(MSVC_ROOT)/,crt/lib/x86_64 sdk/lib/um/x86_64 sdk/lib/ucrt/x86_64))
endif
ifneq ($(SDK_OVERLAY),)
BASE_FLAGS += -ivfsoverlay "$(SDK_OVERLAY)"
endif
CXXFLAGS += -fexceptions -fcxx-exceptions
LDFLAGS += --target=x86_64-pc-windows-msvc -fuse-ld=lld -Wl,/manifest:no,/DEBUG,/OPT:REF,/OPT:ICF,/INCREMENTAL:NO
LDLIBS += ntdll.lib wsock32.lib ws2_32.lib xinput.lib comctl32.lib shcore.lib \
 shlwapi.lib dxguid.lib bcrypt.lib d3d12.lib dxgi.lib synchronization.lib version.lib
else
DEFINES += -D__REQUIRED_RPCNDR_H_VERSION__=475
CXXFLAGS += -fexceptions
LDFLAGS += -static -static-libgcc -static-libstdc++ -Wl,--no-undefined,--exclude-all-symbols
ifneq ($(DEBUG),1)
LDFLAGS += -Wl,--strip-debug
endif
LDLIBS += -lntdll -lwsock32 -lws2_32 -lxinput -lcomctl32 -lshcore -lshlwapi -ldxguid -lbcrypt -ld3d12 -ldxgi -lsynchronization -lversion -lole32 -luuid -lshell32 -luser32 -lgdi32 -limm32 -lwinmm -ladvapi32 -lsetupapi -lcfgmgr32 -lhid -ldwmapi -lcomdlg32 -loleaut32
endif
INCLUDE_DIRS += third_party/libusb/libusb
endif

OBJECTS := $(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(SOURCES_C) $(SOURCES_CXX) $(SOURCES_ASM) $(SOURCES_OBJCXX)))
FRONTEND_OBJECTS := $(filter $(BUILD_DIR)/obj/libretro/%,$(OBJECTS))
CORE_OBJECTS := $(filter-out $(FRONTEND_OBJECTS),$(OBJECTS))
CORE_ARCHIVE := $(BUILD_DIR)/core.a
CONFIG_INPUTS := tools/build/core_codegen.c tools/build/core_deflate.c \
 upstream.version $(wildcard libretro/data/*) \
 third_party/snappy/snappy-stubs-public.h.in third_party/snappy/cmake/config.h.in

.PHONY: all clean generated shaders host-tools help print-config FORCE $(TARGET)
all: $(TARGET)
$(TARGET): $(BUILD_DIR)/$(TARGET)
	cp "$<" "$@"

# Run the content-preserving generator each invocation: patch filenames may
# contain spaces, and additions/removals must also update the embedded bundle.
generated: $(CONFIG_INPUTS) $(CODEGEN) | check-target-tools
	@mkdir -p "$(GEN)"
	$(CODEGEN) inputs "$(GEN)" $(if $(filter windows,$(OS)),windows) $(if $(filter aarch64,$(ARCH)),aarch64)

$(GEN)/embedded_bundle_patches.cc $(GEN)/libretro_gamedb_data.cc: | generated
	@test -f "$@"

export MAKE_BUILD_SIGNATURE := $(CC)|$(CXX)|$(BASE_FLAGS)|$(CPPFLAGS)|$(CFLAGS)|$(CXXFLAGS)|$(LDFLAGS)|$(LDLIBS)
$(BUILD_DIR)/config: FORCE $(CODEGEN)
	@$(CODEGEN) signature "$@"
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
$(BUILD_DIR)/obj/%.mm.o: %.mm
	@mkdir -p "$(@D)"
	$(CXX) $(BASE_FLAGS) $(CPPFLAGS) $(CXXFLAGS) -c "$<" -o "$@"
$(BUILD_DIR)/obj/%.asm.o: %.asm
	@mkdir -p "$(@D)"
	$(CC) $(BASE_FLAGS) $(CPPFLAGS) -x assembler-with-cpp -c "$<" -o "$@"
$(BUILD_DIR)/obj/%.S.o: %.S
	@mkdir -p "$(@D)"
	$(CC) $(BASE_FLAGS) $(CPPFLAGS) -c "$<" -o "$@"

$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter third_party/%,$(SOURCES_C) $(SOURCES_CXX)))): CPPFLAGS += -w
ifeq ($(OS),linux)
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter third_party/libretro-common/%,$(SOURCES_C)))): CPPFLAGS += -D_GNU_SOURCE
endif
ifneq ($(OS),windows)
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter third_party/libretro-common/%,$(SOURCES_C)))): CPPFLAGS += -DHAVE_MMAP
endif
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter third_party/capstone/%,$(SOURCES_C)))): CPPFLAGS += $(CAPSTONE_DEFS) -DCAPSTONE_USE_SYS_DYN_MEM -Ithird_party/capstone
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter src/xenia/cpu/backend/%,$(SOURCES_CXX)))): CPPFLAGS += $(CAPSTONE_DEFS) -DCAPSTONE_USE_SYS_DYN_MEM
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter third_party/mspack/% third_party/snappy/%,$(SOURCES_C) $(SOURCES_CXX)))): CPPFLAGS += -DHAVE_CONFIG_H
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter third_party/snappy/%,$(SOURCES_CXX)))): CPPFLAGS := -I$(GEN)/snappy $(CPPFLAGS) -w -DHAVE_CONFIG_H
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter third_party/libusb/%,$(SOURCES_C)))): CPPFLAGS := -Ithird_party/libusb/$(if $(filter msvc,$(WINDOWS_ABI)),msvc,mingw) $(CPPFLAGS) -w
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(FFMPEG_SOURCES))): CPPFLAGS += -DHAVE_AV_CONFIG_H -D_USE_MATH_DEFINES -Ithird_party/FFmpeg/libavcodec
ifneq ($(OS),windows)
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(FFMPEG_SOURCES))): CPPFLAGS += -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Ithird_party/FFmpeg/compat/atomics/gcc
else
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(FFMPEG_SOURCES))): CPPFLAGS += -Ithird_party/FFmpeg/compat/atomics/win32
endif

$(CORE_ARCHIVE): export MAKE_RESPONSE := $(foreach obj,$(CORE_OBJECTS),"$(obj)")
$(CORE_ARCHIVE): $(CORE_OBJECTS) $(CODEGEN)
	@$(CODEGEN) response "$(BUILD_DIR)/archive.rsp"
	rm -f "$@"
ifeq ($(OS),macos)
	@$(CODEGEN) filelist "$(BUILD_DIR)/archive.files"
	libtool -static -o "$@" -filelist "$(BUILD_DIR)/archive.files"
else
	$(AR) qcs "$@" @$(BUILD_DIR)/archive.rsp
endif

# Keep the frontend's exported entry points as ordinary objects. Archive
# extraction pulls in only the emulator/dependency members they actually use,
# just as the existing build's static libraries do.
$(BUILD_DIR)/$(TARGET): export MAKE_RESPONSE := $(foreach obj,$(FRONTEND_OBJECTS) $(CORE_ARCHIVE),"$(obj)")
$(BUILD_DIR)/$(TARGET): $(FRONTEND_OBJECTS) $(CORE_ARCHIVE) libretro/link.T
	@$(CODEGEN) response "$(BUILD_DIR)/objects.rsp"
	$(CXX) $(LDFLAGS) -o "$@" @$(BUILD_DIR)/objects.rsp $(LDLIBS)

export HOST_BUILD_SIGNATURE := $(HOST_CC)|$(HOST_CFLAGS)
$(HOST_DIR)/config: FORCE
	@mkdir -p "$(@D)"
	@temporary="$@.tmp.$$$$"; printf '%s\n' "$$HOST_BUILD_SIGNATURE" > "$$temporary"; \
	if cmp -s "$$temporary" "$@"; then rm -f "$$temporary"; else mv "$$temporary" "$@"; fi
$(CODEGEN): tools/build/core_codegen.c tools/build/core_deflate.c $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c89 tools/build/core_codegen.c tools/build/core_deflate.c -o "$@"
host-tools: $(CODEGEN)

# Development check for the raw DXIL writer, independent of the core build.
# dxv is Microsoft's validator; normal builds do not require it.
DXIL_VALIDATOR ?= dxv
DXIL_BYTECODE_DIR := src/xenia/gpu/dxil/bytecode
DXIL_BYTECODE_SOURCES := $(wildcard $(DXIL_BYTECODE_DIR)/*.c)
$(HOST_DIR)/dxil-writer-test$(HOST_EXEEXT): tools/tests/dxil_writer_test.c $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_writer_test.c $(DXIL_BYTECODE_SOURCES) -o "$@"
.PHONY: check-dxil-writer
check-dxil-writer: $(HOST_DIR)/dxil-writer-test$(HOST_EXEEXT)
	$< "$(HOST_DIR)/dxil-writer-test.dxil"
	$(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-writer-test.dxil" -o="$(HOST_DIR)/dxil-writer-test-validated.dxil"
	cmp "$(HOST_DIR)/dxil-writer-test.dxil" "$(HOST_DIR)/dxil-writer-test-validated.dxil"
	$< "$(HOST_DIR)/dxil-writer-test-vs.dxil" vs
	$(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-writer-test-vs.dxil" -o="$(HOST_DIR)/dxil-writer-test-vs-validated.dxil"
	cmp "$(HOST_DIR)/dxil-writer-test-vs.dxil" "$(HOST_DIR)/dxil-writer-test-vs-validated.dxil"
	$< "$(HOST_DIR)/dxil-writer-test-ps.dxil" ps
	$(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-writer-test-ps.dxil" -o="$(HOST_DIR)/dxil-writer-test-ps-validated.dxil"
	cmp "$(HOST_DIR)/dxil-writer-test-ps.dxil" "$(HOST_DIR)/dxil-writer-test-ps-validated.dxil"

DXIL_ALU_DIR := src/xenia/gpu/dxil
DXIL_ALU_SOURCES := $(DXIL_ALU_DIR)/guest_alu.c $(DXIL_ALU_DIR)/guest_alu_instruction.c $(DXIL_ALU_DIR)/guest_flow.c $(DXIL_ALU_DIR)/guest_vertex.c $(DXIL_ALU_DIR)/guest_resources.c $(DXIL_ALU_DIR)/guest_texture.c $(DXIL_ALU_DIR)/shader_module.c $(DXIL_ALU_DIR)/guest_geometry.c $(DXIL_ALU_DIR)/edram_format.c $(DXIL_ALU_DIR)/edram_dump.c $(DXIL_ALU_DIR)/edram_transfer.c $(DXIL_ALU_DIR)/depth_only.c $(DXIL_ALU_DIR)/edram_depth_stencil.c $(DXIL_ALU_DIR)/rov_depth.c $(DXIL_ALU_DIR)/guest_program.c $(DXIL_ALU_DIR)/guest_vertex_stage.c $(DXIL_ALU_DIR)/guest_pixel_stage.c $(DXIL_ALU_DIR)/guest_rop.c $(DXIL_ALU_DIR)/guest_texture_stage.c $(DXIL_ALU_DIR)/guest_memexport.c $(DXIL_ALU_DIR)/guest_tessellation.c
$(HOST_DIR)/dxil-alu-test$(HOST_EXEEXT): tools/tests/dxil_writer_test.c tools/tests/dxil_alu_test.c tools/tests/dxil_flow_test.c tools/tests/dxil_vertex_test.c tools/tests/dxil_resources_test.c tools/tests/dxil_texture_test.c tools/tests/dxil_edram_format_test.c tools/tests/dxil_test_storage.h $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -DXE_DXIL_ALU_TEST -DXE_DXIL_FLOW_TEST -DXE_DXIL_VERTEX_TEST -DXE_DXIL_RESOURCES_TEST -DXE_DXIL_TEXTURE_TEST -DXE_DXIL_EDRAM_TEST -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_writer_test.c tools/tests/dxil_alu_test.c tools/tests/dxil_flow_test.c tools/tests/dxil_vertex_test.c tools/tests/dxil_resources_test.c tools/tests/dxil_texture_test.c tools/tests/dxil_edram_format_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -o "$@"
.PHONY: check-dxil-alu check-dxil-alu-c89
check-dxil-alu-c89:
	$(HOST_CC) $(HOST_CFLAGS) -std=c89 -pedantic-errors -Wall -Wextra -Werror -DXE_DXIL_ALU_TEST -DXE_DXIL_FLOW_TEST -DXE_DXIL_VERTEX_TEST -DXE_DXIL_RESOURCES_TEST -DXE_DXIL_TEXTURE_TEST -DXE_DXIL_EDRAM_TEST -I$(DXIL_ALU_DIR) -isystem $(DXIL_BYTECODE_DIR) -fsyntax-only $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_DIR)/dxil_blocks.c tools/tests/dxil_alu_test.c tools/tests/dxil_flow_test.c tools/tests/dxil_vertex_test.c tools/tests/dxil_resources_test.c tools/tests/dxil_texture_test.c tools/tests/dxil_edram_format_test.c tools/tests/dxil_geometry_test.c tools/tests/dxil_edram_dump_test.c tools/tests/dxil_pixel_interface_test.c tools/tests/dxil_edram_transfer_test.c tools/tests/dxil_depth_only_test.c tools/tests/dxil_rov_test.c tools/tests/dxil_rov_depth_test.c tools/tests/dxil_program_test.c tools/tests/dxil_guest_vertex_test.c tools/tests/dxil_guest_pixel_test.c tools/tests/dxil_render_color_test.c tools/tests/dxil_guest_rop_reference_test.c tools/tests/dxil_bindless_test.c tools/tests/dxil_texture_weights_test.c tools/tests/dxil_texture_stage_test.c tools/tests/dxil_texture_cube_stage_test.c tools/tests/dxil_texture_volume_stage_test.c tools/tests/dxil_writer_test.c
check-dxil-alu: $(HOST_DIR)/dxil-alu-test$(HOST_EXEEXT) check-dxil-alu-c89
	$< "$(HOST_DIR)/dxil-alu-test.dxil" alu
	$(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-alu-test.dxil" -o="$(HOST_DIR)/dxil-alu-test-validated.dxil"
	cmp "$(HOST_DIR)/dxil-alu-test.dxil" "$(HOST_DIR)/dxil-alu-test-validated.dxil"

define shader-rule
$(call shader-output,$(1)): $(1) $(CODEGEN) | check-target-tools
	$$(CODEGEN) shader "$$@" "$$<" "$(notdir $(basename $(1)))" $(if $(filter %.spv,$(1)),spirv,dxil)
endef
$(foreach s,$(SHADER_SOURCES),$(eval $(call shader-rule,$(s))))
shaders: $(SHADER_HEADERS)

# Check target tools only for core builds. Data generation and host-only checks
# remain usable without a target toolchain. Order-only edges gate parallel work.
.PHONY: check-target-tools
ifneq ($(filter-out clean help print-config host-tools generated shaders check-dxil-% check-core-events,$(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)),)
check-target-tools:
	@$(CXX) --version >/dev/null 2>&1 || { echo "Missing target C++ compiler: $(CXX). Install a C++20 compiler, or set CXX/CROSS_COMPILE explicitly." >&2; exit 1; }
	@$(CC) --version >/dev/null 2>&1 || { echo "Missing target C compiler: $(CC). Install a C compiler, or set CC/CROSS_COMPILE explicitly." >&2; exit 1; }
ifeq ($(WINDOWS_ABI),msvc)
	@case "$$($(CXX) --version 2>/dev/null)" in *clang*) ;; *) echo "WINDOWS_ABI=msvc selects the optional Clang/MSVC cross build. Use the default WINDOWS_ABI=gnu for MinGW GCC." >&2; exit 1 ;; esac
else ifeq ($(OS),windows)
	@case "$$($(CC) -dumpmachine 2>/dev/null)" in *mingw*|*windows*) ;; *) echo "Use a MinGW Windows compiler on PATH, or set CROSS_COMPILE=x86_64-w64-mingw32-." >&2; exit 1 ;; esac
endif
else
check-target-tools:
endif

print-config:
	@echo "platform=$(platform) OS=$(OS) ARCH=$(ARCH) TARGET=$(TARGET)"
	@echo "CC=$(CC) CXX=$(CXX) HOST_CC=$(HOST_CC)"
	@echo "BUILD_DIR=$(BUILD_DIR) HOST_DIR=$(HOST_DIR)"
help:
	@echo "make platform=<$(SUPPORTED_PLATFORMS)> [-jN] [DEBUG=1] [LTO=1]"
	@echo "Android: NDK_ROOT=/path/to/ndk [API_LEVEL=28]"
	@echo "Windows: MinGW GCC/binutils; Linux cross build: .ci/build-windows-cross.sh"
clean:
	@root=$$(pwd -P); \
	if test -d "$(BUILD_DIR)"; then \
	  build_dir=$$(cd "$(BUILD_DIR)" && pwd -P) || exit 1; \
	  case "$$build_dir" in \
	    "$$root/build") echo "Unsafe BUILD_DIR: $$build_dir" >&2; exit 1 ;; \
	    "$$root"/*) ;; \
	    *) echo "Unsafe BUILD_DIR: $$build_dir" >&2; exit 1 ;; \
	  esac; \
	  rm -rf -- "$$build_dir"; \
	elif test -e "$(BUILD_DIR)"; then \
	  echo "BUILD_DIR is not a directory: $(BUILD_DIR)" >&2; exit 1; \
	fi
	rm -f "$(TARGET)"

-include $(OBJECTS:.o=.d) $(addsuffix .d,$(SHADER_HEADERS))

.PHONY: check-dxil-flow
check-dxil-flow: $(HOST_DIR)/dxil-alu-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for case in 0 1 2 3 4; do \
	  $< "$(HOST_DIR)/dxil-flow-$$case.dxil" flow $$case; \
	  $(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-flow-$$case.dxil" -o="$(HOST_DIR)/dxil-flow-$$case-validated.dxil"; \
	  cmp "$(HOST_DIR)/dxil-flow-$$case.dxil" "$(HOST_DIR)/dxil-flow-$$case-validated.dxil"; \
	done
	@set -e; for case in 5 6 7 8 9 10; do \
	  if $< "$(HOST_DIR)/dxil-flow-invalid.dxil" flow $$case; then \
	    echo "invalid flow case $$case unexpectedly succeeded"; exit 1; \
	  fi; \
	done
.PHONY: check-dxil-vertex
check-dxil-vertex: $(HOST_DIR)/dxil-alu-test$(HOST_EXEEXT) check-dxil-alu-c89
	$< "$(HOST_DIR)/dxil-vertex-test.dxil" vertex
	$(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-vertex-test.dxil" -o="$(HOST_DIR)/dxil-vertex-test-validated.dxil"
	cmp "$(HOST_DIR)/dxil-vertex-test.dxil" "$(HOST_DIR)/dxil-vertex-test-validated.dxil"
.PHONY: check-dxil-resources
check-dxil-resources: $(HOST_DIR)/dxil-alu-test$(HOST_EXEEXT) check-dxil-alu-c89
	$< "$(HOST_DIR)/dxil-resources-test.dxil" resources
	$(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-resources-test.dxil" -o="$(HOST_DIR)/dxil-resources-test-validated.dxil"
	cmp "$(HOST_DIR)/dxil-resources-test.dxil" "$(HOST_DIR)/dxil-resources-test-validated.dxil"
.PHONY: check-dxil-texture
check-dxil-texture: $(HOST_DIR)/dxil-alu-test$(HOST_EXEEXT) check-dxil-alu-c89
	$< "$(HOST_DIR)/dxil-texture-test.dxil" texture
	$(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-texture-test.dxil" -o="$(HOST_DIR)/dxil-texture-test-validated.dxil"
	cmp "$(HOST_DIR)/dxil-texture-test.dxil" "$(HOST_DIR)/dxil-texture-test-validated.dxil"
	$< "$(HOST_DIR)/dxil-texture-ps-test.dxil" texture-ps
	$(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-texture-ps-test.dxil" -o="$(HOST_DIR)/dxil-texture-ps-test-validated.dxil"
	cmp "$(HOST_DIR)/dxil-texture-ps-test.dxil" "$(HOST_DIR)/dxil-texture-ps-test-validated.dxil"
.PHONY: check-core-events
check-core-events:
	@mkdir -p "$(HOST_DIR)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c89 -pedantic-errors -Wall -Wextra -Werror -Ilibretro libretro/core_events.c tools/tests/core_events_test.c -o "$(HOST_DIR)/core-events-test$(HOST_EXEEXT)"
	"$(HOST_DIR)/core-events-test$(HOST_EXEEXT)"

$(HOST_DIR)/dxil-geometry-test$(HOST_EXEEXT): tools/tests/dxil_geometry_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_geometry_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -o "$@"
.PHONY: check-dxil-geometry
check-dxil-geometry: $(HOST_DIR)/dxil-geometry-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for type in 1 2 3 4; do for variant in 0 1 2; do \
	  $< "$(HOST_DIR)/dxil-geometry-$$type-$$variant.dxil" $$type $$variant; \
	  $(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-geometry-$$type-$$variant.dxil" -o="$(HOST_DIR)/dxil-geometry-$$type-$$variant-validated.dxil"; \
	  cmp "$(HOST_DIR)/dxil-geometry-$$type-$$variant.dxil" "$(HOST_DIR)/dxil-geometry-$$type-$$variant-validated.dxil"; \
	done; done
$(addprefix $(BUILD_DIR)/obj/,$(addsuffix .o,$(filter src/xenia/gpu/dxil/%,$(SOURCES_C)))): CPPFLAGS := -Isrc/xenia/gpu/dxil/bytecode $(CPPFLAGS)
.PHONY: check-dxil-edram
check-dxil-edram: $(HOST_DIR)/dxil-alu-test$(HOST_EXEEXT) check-dxil-alu-c89
	$< "$(HOST_DIR)/dxil-edram-test.dxil" edram
	$(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-edram-test.dxil" -o="$(HOST_DIR)/dxil-edram-test-validated.dxil"
	cmp "$(HOST_DIR)/dxil-edram-test.dxil" "$(HOST_DIR)/dxil-edram-test-validated.dxil"
$(HOST_DIR)/dxil-edram-dump-test$(HOST_EXEEXT): tools/tests/dxil_edram_dump_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_edram_dump_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -o "$@"
.PHONY: check-dxil-edram-dump
check-dxil-edram-dump: $(HOST_DIR)/dxil-edram-dump-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for depth in 0 1; do \
	  if test $$depth = 0; then formats="0 1 2 3 4 5 6 7 10 12 14 15"; else formats="0 1"; fi; \
	  for format in $$formats; do for msaa in 0 1 2; do for config in 0 1 2 3; do for resolve in 0 1; do \
	    path="$(HOST_DIR)/dxil-dump-$$depth-$$format-$$msaa-$$config-$$resolve.dxil"; \
	    $< "$$path" $$format $$depth $$msaa $$config $$resolve; \
	    $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; \
	    cmp "$$path" "$$path.validated"; \
	  done; done; done; done; \
	done
$(HOST_DIR)/dxil-pixel-interface-test$(HOST_EXEEXT): tools/tests/dxil_pixel_interface_test.c $(DXIL_ALU_DIR)/shader_module.c $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_pixel_interface_test.c $(DXIL_ALU_DIR)/shader_module.c $(DXIL_BYTECODE_SOURCES) -o "$@"
.PHONY: check-dxil-pixel-interface
check-dxil-pixel-interface: $(HOST_DIR)/dxil-pixel-interface-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for mode in 0 1 2 3 4; do \
	  path="$(HOST_DIR)/dxil-pixel-interface-$$mode.dxil"; \
	  $< "$$path" $$mode; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; \
	  cmp "$$path" "$$path.validated"; \
	done

$(HOST_DIR)/dxil-edram-transfer-test$(HOST_EXEEXT): tools/tests/dxil_edram_transfer_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_edram_transfer_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -o "$@"

.PHONY: check-dxil-edram-transfer
check-dxil-edram-transfer: $(HOST_DIR)/dxil-edram-transfer-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for mode in 0 1 2 3 4 5 6 7 8 9; do \
	  for variant in $$(seq 0 35); do \
	    path="$(HOST_DIR)/dxil-transfer-$$mode-$$variant.dxil"; \
	    $< "$$path" $$mode $$variant; \
	    $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; \
	    cmp "$$path" "$$path.validated"; \
	  done; \
	done
	@set -e; for source in 0 1 2 3 4 5 6 7 10 12 14 15; do \
	  for dest in 0 1 2 3 4 5 6 7 10 12 14 15; do \
	    path="$(HOST_DIR)/dxil-transfer-format-$$source-$$dest.dxil"; \
	    $< "$$path" 1 5 $$source $$dest; \
	    $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; \
	    cmp "$$path" "$$path.validated"; \
	  done; \
	done
	@set -e; for mode in 0 2 3 4 5 6 7 8 9; do \
	  case $$mode in 0|4|6|8) sources="0 1 2 3 4 5 6 7 10 12 14 15";; *) sources="0 1";; esac; \
	  case $$mode in 3) destinations="0 1 2 3 4 5 6 7 10 12 14 15";; *) destinations="0 1";; esac; \
	  for source in $$sources; do for dest in $$destinations; do \
	    path="$(HOST_DIR)/dxil-transfer-depth-$$mode-$$source-$$dest.dxil"; \
	    $< "$$path" $$mode 15 $$source $$dest; \
	    $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; \
	    cmp "$$path" "$$path.validated"; \
	  done; done; \
	done
	@set -e; for parameters in "4 15 0 0 1" "5 15 1 1 1" "1 15 3 0 2" "1 15 12 0 2" "2 15 1 1 4" "7 15 1 1 4"; do \
	  path="$(HOST_DIR)/dxil-transfer-flags.dxil"; \
	  $< "$$path" $$parameters; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; \
	  cmp "$$path" "$$path.validated"; \
	done

$(HOST_DIR)/dxil-depth-only-test$(HOST_EXEEXT): tools/tests/dxil_depth_only_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_depth_only_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -o "$@"
.PHONY: check-dxil-depth-only
check-dxil-depth-only: $(HOST_DIR)/dxil-depth-only-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for mode in 0 1 2; do for count in 0 1; do \
	  path="$(HOST_DIR)/dxil-depth-$$mode-$$count.dxil"; \
	  $< "$$path" $$mode $$count; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; \
	  cmp "$$path" "$$path.validated"; \
	done; done

$(HOST_DIR)/dxil-rov-test$(HOST_EXEEXT): tools/tests/dxil_rov_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_rov_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -o "$@"
$(HOST_DIR)/dxil-rov-depth-test$(HOST_EXEEXT): tools/tests/dxil_rov_depth_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_rov_depth_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -o "$@"

.PHONY: check-dxil-rov check-dxil-rov-depth
check-dxil-rov: $(HOST_DIR)/dxil-rov-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for mode in 0 1; do \
	  path="$(HOST_DIR)/dxil-rov-$$mode.dxil"; \
	  $< "$$path" $$mode; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; \
	  cmp "$$path" "$$path.validated"; \
	done
check-dxil-rov-depth: $(HOST_DIR)/dxil-rov-depth-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for full in 0 1; do for msaa in 0 1 2; do for config in 0 1; do for survey in 0 1; do \
	  path="$(HOST_DIR)/dxil-rov-depth-$$full-$$msaa-$$config-$$survey.dxil"; \
	  $< "$$path" $$msaa $$config $$survey $$full; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; \
	  cmp "$$path" "$$path.validated"; \
	done; done; done; done

$(HOST_DIR)/dxil-program-test$(HOST_EXEEXT): tools/tests/dxil_program_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_program_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -o "$@"
.PHONY: check-dxil-program
check-dxil-program: $(HOST_DIR)/dxil-program-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for mode in 0 1 2 3 4 5 6 7 8; do \
	  path="$(HOST_DIR)/dxil-program-$$mode.dxil"; \
	  $< "$$path" $$mode; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; \
	  cmp "$$path" "$$path.validated"; \
	done

$(HOST_DIR)/dxil-guest-vertex-test$(HOST_EXEEXT): tools/tests/dxil_guest_vertex_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_guest_vertex_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -o "$@"
.PHONY: check-dxil-guest-vertex
check-dxil-guest-vertex: $(HOST_DIR)/dxil-guest-vertex-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for config in 0 1 2 3 4 5; do \
	  path="$(HOST_DIR)/dxil-guest-vertex-$$config.dxil"; \
	  $< "$$path" $$config; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; \
	  cmp "$$path" "$$path.validated"; \
	done

$(HOST_DIR)/dxil-guest-pixel-test$(HOST_EXEEXT): tools/tests/dxil_guest_pixel_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_guest_pixel_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -o "$@"
.PHONY: check-dxil-guest-pixel
check-dxil-guest-pixel: $(HOST_DIR)/dxil-guest-pixel-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for variant in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do \
	for config in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27; do \
	  mode=$$((variant*28+config)); \
	  path="$(HOST_DIR)/dxil-guest-pixel-$$mode.dxil"; \
	  $< "$$path" $$mode; \
	  for suffix in '' .vs $$(test $$variant = 3 || printf .guest.vs); do \
	    $(DXIL_VALIDATOR) "$$path$$suffix" -o="$$path$$suffix.validated"; \
	    cmp "$$path$$suffix" "$$path$$suffix.validated"; \
	  done; \
	done; done

$(HOST_DIR)/dxil-render-color-test$(HOST_EXEEXT): tools/tests/dxil_render_color_test.c tools/tests/dxil_render_color_reference.h $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_render_color_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -lm -o "$@"
.PHONY: check-dxil-render-color
check-dxil-render-color: $(HOST_DIR)/dxil-render-color-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for format in 0 1 2 3 4 5 6 7 10 12 14 15; do \
	  path="$(HOST_DIR)/dxil-render-color-$$format.dxil"; \
	  $< "$$path" $$format; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; \
	  cmp "$$path" "$$path.validated"; \
	done



$(HOST_DIR)/dxil-guest-rop-reference-test$(HOST_EXEEXT): tools/tests/dxil_guest_rop_reference_test.c tools/tests/dxil_render_color_reference.h $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c89 -pedantic-errors -Wall -Wextra -Werror tools/tests/dxil_guest_rop_reference_test.c -lm -o "$@"
.PHONY: check-dxil-guest-rop
check-dxil-guest-rop: $(HOST_DIR)/dxil-guest-pixel-test$(HOST_EXEEXT) $(HOST_DIR)/dxil-guest-rop-reference-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for base in 0 4 8; do for msaa in 0 1 2; do for scale in 0 4; do \
	  for no_blend in 0 128; do for counters in 0 256; do \
	    config=$$((base*8+msaa+scale+no_blend+counters)); \
	    for mode in 0 14 28 56 112 168 182; do \
	      path="$(HOST_DIR)/dxil-guest-rop-$$config-$$mode.dxil"; \
	      $< "$$path" $$mode $$config; \
	      $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; \
	      cmp "$$path" "$$path.validated"; \
	    done; \
	  done; done; \
	done; done; done

$(HOST_DIR)/dxil-bindless-test$(HOST_EXEEXT): tools/tests/dxil_bindless_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_bindless_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -o "$@"
.PHONY: check-dxil-bindless
check-dxil-bindless: $(HOST_DIR)/dxil-bindless-test$(HOST_EXEEXT) check-dxil-alu-c89
	$< "$(HOST_DIR)/dxil-bindless-test.dxil"
	$(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-bindless-test.dxil" -o="$(HOST_DIR)/dxil-bindless-test.dxil.validated"
	cmp "$(HOST_DIR)/dxil-bindless-test.dxil" "$(HOST_DIR)/dxil-bindless-test.dxil.validated"


$(HOST_DIR)/dxil-texture-weights-test$(HOST_EXEEXT): tools/tests/dxil_texture_weights_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_texture_weights_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -lm -o "$@"
.PHONY: check-dxil-texture-weights check-dxil-guest-texture-query
check-dxil-texture-weights: $(HOST_DIR)/dxil-texture-weights-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for pattern in 0 1 2 3; do \
	  path="$(HOST_DIR)/dxil-texture-weights-$$pattern.dxil"; $< "$$path" $$pattern; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; cmp "$$path" "$$path.validated"; \
	done
check-dxil-guest-texture-query: $(HOST_DIR)/dxil-guest-pixel-test$(HOST_EXEEXT) $(HOST_DIR)/dxil-guest-vertex-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; mode=476; while test $$mode -le 559; do \
	  path="$(HOST_DIR)/dxil-guest-query-$$mode.dxil"; $< "$$path" $$mode; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; cmp "$$path" "$$path.validated"; \
	  mode=$$((mode+1)); \
	done
	$< "$(HOST_DIR)/dxil-guest-query-unsupported.dxil" 1064
	$< "$(HOST_DIR)/dxil-guest-query-unsupported.dxil" 1065
	$< "$(HOST_DIR)/dxil-guest-query-unsupported.dxil" 1066
	$(HOST_DIR)/dxil-guest-vertex-test$(HOST_EXEEXT) "$(HOST_DIR)/dxil-vertex-query-unsupported.dxil" 8
	@set -e; for config in 6 7; do \
	  path="$(HOST_DIR)/dxil-vertex-query-$$config.dxil"; \
	  $(HOST_DIR)/dxil-guest-vertex-test$(HOST_EXEEXT) "$$path" $$config; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; cmp "$$path" "$$path.validated"; \
	done

.PHONY: check-dxil-bindless-array
check-dxil-bindless-array: $(HOST_DIR)/dxil-bindless-test$(HOST_EXEEXT) check-dxil-alu-c89
	$< "$(HOST_DIR)/dxil-bindless-array-test.dxil" array
	$(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-bindless-array-test.dxil" -o="$(HOST_DIR)/dxil-bindless-array-test.dxil.validated"
	cmp "$(HOST_DIR)/dxil-bindless-array-test.dxil" "$(HOST_DIR)/dxil-bindless-array-test.dxil.validated"

.PHONY: check-dxil-bindless-pixel
check-dxil-bindless-pixel: $(HOST_DIR)/dxil-bindless-test$(HOST_EXEEXT) check-dxil-alu-c89
	$< "$(HOST_DIR)/dxil-bindless-pixel-test.dxil" pixel
	$(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-bindless-pixel-test.dxil" -o="$(HOST_DIR)/dxil-bindless-pixel-test.dxil.validated"
	cmp "$(HOST_DIR)/dxil-bindless-pixel-test.dxil" "$(HOST_DIR)/dxil-bindless-pixel-test.dxil.validated"

$(HOST_DIR)/dxil-texture-stage-test$(HOST_EXEEXT): tools/tests/dxil_texture_stage_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_texture_stage_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -lm -o "$@"

.PHONY: check-dxil-guest-sampling
check-dxil-guest-sampling: $(HOST_DIR)/dxil-texture-stage-test$(HOST_EXEEXT) check-dxil-texture-stage-c89 $(HOST_DIR)/dxil-guest-pixel-test$(HOST_EXEEXT) $(HOST_DIR)/dxil-guest-vertex-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; mode=0; while test $$mode -le 61; do \
	  path="$(HOST_DIR)/dxil-sampling-policy-$$mode.dxil"; $< "$$path" $$mode; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; cmp "$$path" "$$path.validated"; \
	  mode=$$((mode+1)); \
	done
	@set -e; mode=560; while test $$mode -le 1063; do \
	  path="$(HOST_DIR)/dxil-guest-sampling-$$mode.dxil"; $(HOST_DIR)/dxil-guest-pixel-test$(HOST_EXEEXT) "$$path" $$mode; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; cmp "$$path" "$$path.validated"; \
	  mode=$$((mode+1)); \
	done
	@set -e; for config in 9 10 11 12 13 14 15 16 17; do \
	  path="$(HOST_DIR)/dxil-vertex-sampling-$$config.dxil"; $(HOST_DIR)/dxil-guest-vertex-test$(HOST_EXEEXT) "$$path" $$config; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; cmp "$$path" "$$path.validated"; \
	done

.PHONY: check-dxil-guest-sampling-rov
check-dxil-guest-sampling-rov: $(HOST_DIR)/dxil-guest-pixel-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for mode in 560 588 616 896; do \
	  format=0; while test $$format -lt 12; do \
	    for msaa in 0 1 2; do \
	      config=$$((format*8+msaa)); path="$(HOST_DIR)/dxil-sampled-rov-$$mode-$$config.dxil"; \
	      $< "$$path" $$mode $$config; \
	      $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; cmp "$$path" "$$path.validated"; \
	    done; format=$$((format+1)); \
	  done; \
	done

$(HOST_DIR)/dxil-texture-cube-stage-test$(HOST_EXEEXT): tools/tests/dxil_texture_cube_stage_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_texture_cube_stage_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -lm -o "$@"

.PHONY: check-dxil-cube-sampling
check-dxil-cube-sampling: $(HOST_DIR)/dxil-texture-cube-stage-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; mode=0; while test $$mode -le 19; do \
	  path="$(HOST_DIR)/dxil-cube-policy-$$mode.dxil"; $< "$$path" $$mode; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; cmp "$$path" "$$path.validated"; \
	  mode=$$((mode+1)); \
	done

$(HOST_DIR)/dxil-texture-volume-stage-test$(HOST_EXEEXT): tools/tests/dxil_texture_volume_stage_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) tools/tests/dxil_texture_volume_stage_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -lm -o "$@"

.PHONY: check-dxil-volume-sampling
check-dxil-volume-sampling: $(HOST_DIR)/dxil-texture-volume-stage-test$(HOST_EXEEXT) check-dxil-texture-stage-c89 check-dxil-alu-c89
	@set -e; mode=0; while test $$mode -le 31; do \
	  path="$(HOST_DIR)/dxil-volume-policy-$$mode.dxil"; $< "$$path" $$mode; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; cmp "$$path" "$$path.validated"; \
	  mode=$$((mode+1)); \
	done

.PHONY: check-dxil-scaled-lod
check-dxil-scaled-lod: $(HOST_DIR)/dxil-guest-pixel-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for config in 4 5 6 12 13 14 60 61 62; do \
	  path="$(HOST_DIR)/dxil-scaled-lod-$$config.dxil"; $< "$$path" 896 $$config; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; cmp "$$path" "$$path.validated"; \
	done

.PHONY: check-dxil-texture-stage-c89
check-dxil-texture-stage-c89:
	@mkdir -p "$(HOST_DIR)"
	$(HOST_CC) $(HOST_CFLAGS) -O2 -std=c89 -pedantic-errors -Wall -Wextra -Werror -I$(DXIL_ALU_DIR) -isystem $(DXIL_BYTECODE_DIR) -c $(DXIL_ALU_DIR)/guest_texture_stage.c -o "$(HOST_DIR)/dxil-texture-stage-c89.o"

.PHONY: check-dxil-scaled-sampling
check-dxil-scaled-sampling: $(HOST_DIR)/dxil-guest-pixel-test$(HOST_EXEEXT) check-dxil-alu-c89
	@set -e; for resolved in 0 4096; do \
	  for provenance in 0 8192 24576 40960 73728 139264 155648 524288; do \
	    for format in 1 7 8; do for msaa in 0 1 2; do \
	      config=$$((resolved+provenance+format*8+msaa+4+2048)); \
	      path="$(HOST_DIR)/dxil-scaled-sampling-$$config.dxil"; $< "$$path" 560 $$config; \
	      $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; cmp "$$path" "$$path.validated"; \
	    done; done; \
	  done; \
	done

$(HOST_DIR)/dxil-memexport-test$(HOST_EXEEXT): tools/tests/dxil_memexport_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) $< $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -lm -o "$@"

.PHONY: check-dxil-memexport
check-dxil-memexport: $(HOST_DIR)/dxil-memexport-test$(HOST_EXEEXT) check-dxil-memexport-c89 check-dxil-alu-c89
	$< "$(HOST_DIR)/dxil-memexport.dxil" 6
	$(DXIL_VALIDATOR) "$(HOST_DIR)/dxil-memexport.dxil" -o="$(HOST_DIR)/dxil-memexport.dxil.validated"
	cmp "$(HOST_DIR)/dxil-memexport.dxil" "$(HOST_DIR)/dxil-memexport.dxil.validated"
	@set -e; for variant in 0 1 2 3 4 5 8; do \
	  path="$(HOST_DIR)/dxil-memexport-program-$$variant.dxil"; $< "$$path" 6 program $$variant; \
	  $(DXIL_VALIDATOR) "$$path" -o="$$path.validated"; cmp "$$path" "$$path.validated"; \
	done
	@set -e; for stage in vertex pixel; do for variant in 0 1 2 3 4 5 6 7 9 10; do \
	  path="$(HOST_DIR)/dxil-memexport-$$stage-$$variant"; $< "$$path" 6 $$stage $$variant; \
	  for suffix in vs ps; do \
	    $(DXIL_VALIDATOR) "$$path.$$suffix" -o="$$path.$$suffix.validated"; cmp "$$path.$$suffix" "$$path.$$suffix.validated"; \
	  done; \
	done; done

.PHONY: check-dxil-memexport-c89
check-dxil-memexport-c89:
	@mkdir -p "$(HOST_DIR)"
	$(HOST_CC) $(HOST_CFLAGS) -O2 -std=c89 -pedantic-errors -Wall -Wextra -Werror -I$(DXIL_ALU_DIR) -isystem $(DXIL_BYTECODE_DIR) -c $(DXIL_ALU_DIR)/guest_memexport.c -o "$(HOST_DIR)/dxil-memexport-c89.o"
	$(HOST_CC) $(HOST_CFLAGS) -O2 -std=c89 -pedantic-errors -Wall -Wextra -Werror -I$(DXIL_ALU_DIR) -isystem $(DXIL_BYTECODE_DIR) -c $(DXIL_ALU_DIR)/guest_program.c -o "$(HOST_DIR)/dxil-memexport-program-c89.o"
	$(HOST_CC) $(HOST_CFLAGS) -O2 -std=c89 -pedantic-errors -Wall -Wextra -Werror -I$(DXIL_ALU_DIR) -isystem $(DXIL_BYTECODE_DIR) -c tools/tests/dxil_memexport_test.c -o "$(HOST_DIR)/dxil-memexport-fixture-c89.o"

$(HOST_DIR)/dxil-guest-domain-test$(HOST_EXEEXT): tools/tests/dxil_guest_domain_test.c $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) $(wildcard $(DXIL_ALU_DIR)/*.h $(DXIL_BYTECODE_DIR)/*.h) $(HOST_DIR)/config
	@mkdir -p "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -std=c17 -I$(DXIL_ALU_DIR) -I$(DXIL_BYTECODE_DIR) $< $(DXIL_ALU_SOURCES) $(DXIL_BYTECODE_SOURCES) -o "$@"

.PHONY: check-dxil-guest-domain check-dxil-domain-c89
check-dxil-guest-domain: $(HOST_DIR)/dxil-guest-domain-test$(HOST_EXEEXT) check-dxil-domain-c89 check-dxil-alu-c89
	@set -e; for mode in 0 1 2 3 4 5 6 7 9 11; do for flags in 0 16 32 48 64 96 128 160 240; do \
	  config=$$((mode+flags)); path="$(HOST_DIR)/dxil-guest-domain-$$config"; $< "$$path" $$config; \
	  $(DXIL_VALIDATOR) "$$path.ds" -o="$$path.ds.validated"; cmp "$$path.ds" "$$path.ds.validated"; \
	done; done
check-dxil-domain-c89:
	@mkdir -p "$(HOST_DIR)"
	@set -e; for source in guest_vertex_stage guest_tessellation shader_module; do \
	  $(HOST_CC) $(HOST_CFLAGS) -O2 -std=c89 -pedantic-errors -Wall -Wextra -Werror -I$(DXIL_ALU_DIR) -isystem $(DXIL_BYTECODE_DIR) -c "$(DXIL_ALU_DIR)/$$source.c" -o "$(HOST_DIR)/dxil-domain-$$source.o"; \
	done
	$(HOST_CC) $(HOST_CFLAGS) -O2 -std=c89 -pedantic-errors -Wall -Wextra -Werror -I$(DXIL_ALU_DIR) -isystem $(DXIL_BYTECODE_DIR) -c tools/tests/dxil_guest_domain_test.c -o "$(HOST_DIR)/dxil-domain-fixture.o"
