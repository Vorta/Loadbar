# Toolchain

Selected and tested on 2026-10-07/08; no software installation was performed.

| Component | Tested selection |
| --- | --- |
| IDE | Visual Studio Community 2026 18.10.3, stable channel |
| MSVC tools | 14.51.36231, Hostx64/x64 |
| Compiler reported by `cl /Bv` | 19.51.36260.0 for x64 |
| Windows SDK | 10.0.28000.0; rc/mt product version 10.0.28000.2957 |
| CMake / generator / builder | 4.4.4 / Ninja / 1.13.2 |
| Actual C++ flag | `-std:c++23preview` (equivalent to MSVC's slash spelling) |
| clang-format / clang-tidy | 23.1.3 |
| Build/test host | Windows 11 Pro 26H2, build 26300.9550 |

The other detected SDK was not tested. The optional Visual Studio 18 2026 MSBuild generator
was detected but not validated; presets use Ninja. The intended minimum Windows 11 x64 build
is 22000, which remains untested. Compilation, native tests and provider smoke checks ran on
26300.9550; live AppBar behavior has not been qualified on either build.

## Selection and language mode

`scripts/enter-dev-shell.ps1` uses vswhere to locate stable VS and imports
`VsDevCmd.bat -arch=x64 -host_arch=x64 -winsdk=10.0.28000.0 -vcvars_ver=14.51.36231`.
Configure checks the compiler, SDK and CMake pins. Changes require an explicit pin update
and rebuild; ordinary builds never upgrade tools.

The compiler rejected `/std:c++23`. Its named preview mode compiled/linked the configure
probe for `std::expected`, `std::format`, `std::jthread`, stop tokens and interruptible waits.
This verifies required features, not full C++23 conformance. CMake 4.4.4 otherwise maps
standard 23 to `latest`; the version-gated `cmake/msvc_standard.cmake` override selects
the named mode before probing. Generated commands contain one preview flag.

The preview mode has an ABI-stability caveat: rebuild every object/static library with the
same toolchain and language mode. See Microsoft's
[language modes](https://learn.microsoft.com/en-us/cpp/build/reference/std-specify-language-standard-version?view=msvc-170)
and CMake's [standard property](https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD.html).

## Build and analysis

Static CRT selection precedes target creation: Debug uses /MTd, other presets /MT. Release
uses /O2, separate PDBs, non-incremental linking and the required security mitigations.
LTO remains disabled pending compatibility/performance validation.

ASan uses RelWithDebInfo with /fsanitize=address and /Zi across project objects. It avoids
Debug /RTC1, incremental linking and /ZI. Its development runtime DLL is not a release dependency.

PowerShell 7 runs the helpers; LLVM is required only for formatting/analysis. Format-check
does not modify sources. Tidy derives a database under the build directory, removing only
/Zc:preprocessor because Clang already uses its conforming preprocessor. MSVC analysis uses
/analyze with /WX and excludes SDK-header analysis; project diagnostics remain enabled.
The optional, module-free README fixture disables module scanning so tidy works before
that opt-in target is built.

## Resource dependencies

The icon, LICENSE and configured version header are explicit RC OBJECT_DEPENDS.
The manifest is a LINK_DEPENDS input for both application and Settings-test targets;
a /MANIFESTINPUT option alone does not trigger incremental linking.

The isolated regressions in [testing.md](testing.md#incremental-resource-regressions) verify
manifest-only and license-only rebuilds with Ninja. See CMake's
[LINK_DEPENDS](https://cmake.org/cmake/help/latest/prop_tgt/LINK_DEPENDS.html) and
[OBJECT_DEPENDS](https://cmake.org/cmake/help/latest/prop_sf/OBJECT_DEPENDS.html) for generator
limitations. No MSBuild incremental-resource result is claimed.
