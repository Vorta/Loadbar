# Dependencies and notices

## Bundled font

The user supplied **Geist Mono 1.701**, Copyright 2024 The Geist Project Authors,
under the [SIL Open Font License 1.1](../resources/fonts/OFL.txt).
Upstream: [vercel/geist-font](https://github.com/vercel/geist-font).

`resources/fonts/GeistMono.ttf` is the unmodified upright variable font, 173,204 bytes.
SHA-256: `e8acfc9d5331733427582e4c906174dde5fe57af8f82cdaf906e80d729ff2b18`.
It supplies Regular and Medium weights without synthetic styling. Italics and separate
static-weight files are not needed. No build or runtime download is performed.

The font replaces Consolas in DirectWrite bar text. A private in-memory font collection
is cached for the renderer lifetime and survives Direct2D target loss. Windows controls
retain their system font. The executable embeds the font and full OFL notice; **Licenses**
in the tray/bar menu displays MIT and OFL notices. This introduces no runtime DLL or font
installation. The OFL applies to the font; Loadbar's code remains MIT-licensed.

The two user-supplied temperature design references are preserved under `docs/media/`.
They are documentation assets, not application resources or runtime dependencies.

## Optional installed GPU-driver APIs

The temperature fallbacks need vendor ABI declarations; no vendor library is linked or
distributed. `LoadLibraryExW` searches System32 only for `nvapi64.dll`, `amdadlx64.dll`, or
`ControlLib.dll`. Their implementations and transitive dependencies belong to the installed
graphics driver. Missing components leave the provider unavailable; Loadbar installs nothing.

| Material | Pinned upstream revision | License/use |
| --- | --- | --- |
| NVIDIA NVAPI headers and interface IDs | `70d337db9186e968eab622f7e786de7e437faf3d` | MIT; unmodified header set from [NVIDIA/nvapi](https://github.com/NVIDIA/nvapi). Only five published entry points plus query bootstrap are used. No prebuilt import/static library. |
| AMD ADLX ABI subset | `32b5a740d42295c5dfe9026b9f52683da0f3af91` | C++ adaptation of the separately MIT-licensed [amd-adlx 0.1.0 bindings](https://github.com/GPUOpen-LibrariesAndSDKs/ADLX/tree/32b5a740d42295c5dfe9026b9f52683da0f3af91/Samples/rust/amd-adlx); initialization version 1.5.0.124. No Rust dependency. |
| Intel IGCL header | `b6c462933502e13d1537dd5024949a51be30e63d` | Unmodified `igcl_api.h` from [Intel](https://github.com/intel/drivers.gpu.control-library), API 1.1. Its Intel license permits redistribution with notices for Intel-platform use; this code is invoked only for an Intel GPU. The header retains its own license, not Loadbar's MIT license. |

AMD's generic SDK PDF has different restrictions. Only the explicitly MIT-licensed binding
subdirectory is adapted; the generic SDK headers/helper implementation are not distributed.
The C++ ABI prefix preserves unused vtable slots without callable control functions and
checks read-method offsets at compile time. Full upstream bindings remain development
reference only under ignored `out/research/`.

`third_party/SHA256SUMS` records the checked-in material's integrity. The ordinary build is
offline; no CMake fetch occurs. Notices are embedded and shown by **Licenses**, with exact-byte
resource tests and explicit incremental dependencies. Header files add no runtime payload
beyond compiled adapters; notices do add embedded bytes. Vendor initialization may allocate
memory/create internal threads, so actual driver-dependent overhead requires measurement.
