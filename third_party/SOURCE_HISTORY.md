# Source history

These directories contain ordinary, editable repository source files. Their
original source revisions are recorded here for attribution and comparison.
Original license and copyright notices are retained. Unused examples, tests
and nested sample dependencies were omitted.

| Directory | Original revision |
| --- | --- |
| `third_party/DirectX-Headers` | `33374754f65baac0500dda6187e371136357246f` |
| `third_party/FFmpeg` | `c4f44071adecd5e568470e58ab4bdf483598dbc8` |
| `third_party/FidelityFX-CAS` | `9fabcc9a2c45f958aff55ddfda337e74ef894b7f` |
| `third_party/FidelityFX-FSR` | `a21ffb8f6c13233ba336352bdff293894c706575` |
| `third_party/SPIRV-Headers` | `01e0577914a75a2569c846778c2f93aa8e6feddd` |
| `third_party/Vulkan-Headers` | `8864cdc896bbc2a9b6eb36b3218fc9ef57908d77` |
| `third_party/VulkanMemoryAllocator` | `1d8f600fd424278486eade7ed3e877c99f0846b1` |
| `third_party/aes_128` | `7e3ac3bb6b478187472b4ac6f1698eb203e8e90b` |
| `third_party/asio` | `55684d42ac00021b4c31ba8571aca414c863ead5` |
| `third_party/boost_context/context` | `d2142b69252150dafb5f6569b7eb9a42312a92a3` |
| `third_party/capstone` | `66b08439abd988e87b4542389b4937ce13bd0063` |
| `third_party/cxxopts` | `f029892dab60526184ee42ae63a7f6d19a6b0e49` |
| `third_party/date` | `f94b8f36c6180be0021876c4a397a054fe50c6f2` |
| `third_party/disruptorplus` | `bf9c70c0cc92b6435f7edec75e12751ea37e5c2f` |
| `third_party/fmt` | `407c905e45ad75fc29bf0f9bb7c5c2fd3475976f` |
| `third_party/imgui` | `4806a1924ff6181180bf5e4b8b79ab4394118875` |
| `third_party/libusb` | `a61afe5f75d969c4561a1d0ad753aa23cee6329a` |
| `third_party/mesa` | `7a1fc756809f3bdc9771b54e6036b156389dfc85` |
| `third_party/miniaudio` | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` |
| `third_party/pugixml` | `b2b466403084667c90a0f0cc4e960405cfc8117a` |
| `third_party/rapidjson` | `7c73dd7de7c4f14379b781418c6e947ad464c818` |
| `third_party/snappy` | `6af9287fbdb913f0794d0148c6aa43b58e63c8e3` |
| `third_party/tomlplusplus` | `708fff700f36ab3c2ab107b984ec9f3b8be5f055` |
| `third_party/utfcpp` | `65701fe00700577f9f4e9b96d682bd7cfcc0b76e` |
| `third_party/xbyak` | `9d8ff37306f39c6a71cf998078cbe880ce5dc224` |
| `third_party/xbyak_aarch64` | `59c6a7e7368cd2dbb07c46abe83239ace4d58849` |
| `third_party/xxhash` | `e626a72bc2321cd320e953a0ccf1584cad60f363` |
| `third_party/zarchive` | `b467f7aa342895e66a1a003377ed63b3c1be3de3` |
| `third_party/zlib-ng` | `860e4cff7917d93f54f5d7f0bc1d0e8b1a3cb988` |
| `third_party/zstd` | `20707e3718ee14250fb8a44b3bf023ea36bd88df` |
| `third_party/DirectXShaderCompiler` (Windows API header only) | `69e54e29086b7035acffb304cec57a350225f8b0` |

The core raw SPIR-V writer in `src/xenia/gpu/spirv` reuses binary-writer code
from glslang `a57276bf558f5cf94d3a9854ebdf5a2236849a5a`, with its notices
preserved. No glslang language frontend or compiler library is retained.

The patch data in `libretro/data/patches` comes from
https://github.com/xenia-canary/game-patches at
`fe957a050e37f5a1e3f485ca43c2e37ee2a9c92d`.
