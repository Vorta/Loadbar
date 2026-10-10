# Packaging and release evidence

## 1.3.1 release artifact

Built on 2026-10-10 in the isolated `out/release-source-1.3.1` checkout with
`pwsh -NoProfile -File scripts/package-release.ps1`. Release configure/build and all four
CTest suites passed (6.15 s). The executable is 1,383,936 bytes and reports product/file
version 1.3.1 and author Vorta. The package script verified that the ZIP contains only the
same executable, byte-for-byte. PDBs and research diagnostics are not shipped.
The three assets were copied to the primary workspace's `out/release/1.3.1/` and their hashes
compared with the tested checkout's files.

| Asset | SHA-256 |
| --- | --- |
| Loadbar.exe | `97e78dd446a76fab838a3aadac2ec4518fe66b4fbe25bb4e2e516f22569dd4c5` |
| Loadbar-1.3.1-windows-x64.zip | `25ef10e31f2ad0b644658825be4f70bd60c4e9bb3d13a58c51d619d139556ab1` |

`dumpbin /dependents` lists only Windows components, with no application/CRT/ASan DLL import.
`dumpbin /headers` confirms x64 GUI, CFG, ASLR, DEP, high-entropy VA and an empty delay-import
directory. Release commands retain `/O2`, static `/MT` and the named C++23-preview mode.
The dynamic-load audit finds only the unchanged System32-restricted installed-driver loader.
Logs are `out/v131-{package,dependents,headers}.log` within the isolated checkout.

The owner explicitly requested this wording-only patch release after the existing validation
gaps were disclosed. No additional thermal provider is included. The running AppBar was not
restarted or changed; clean-machine execution and the full interactive/performance/hardware
gates remain pending. See [testing](testing.md) and [release notes](releases/1.3.1.md).

## 1.3.0 release artifact

The final `out/build/windows-x64-release/Loadbar.exe` built on 2026-10-09 reports file/product
version 1.3.0 and author Vorta. It is 1,383,936 bytes; SHA-256 is
`ddf03d8fcb1d3518df2cddef02bfe56aa319a925954e4c8e737456cac87fc443`.
The final `pwsh -NoProfile -File scripts/package-release.ps1` configure/build and Release
CTest run passed all four suites (5.89 s). It staged the identical executable and verified
that the ZIP contains only that executable, byte-for-byte. Packaging log: `out/v130-package.log`.

| Asset | SHA-256 |
| --- | --- |
| Loadbar.exe | `ddf03d8fcb1d3518df2cddef02bfe56aa319a925954e4c8e737456cac87fc443` |
| Loadbar-1.3.0-windows-x64.zip | `e491ab99c65507a224bba88e6816417706e06c63a10270979d41eac668107201` |

`dumpbin /dependents` lists only Windows
components, with no application/CRT/ASan DLL dependency. `/headers` confirms x64 GUI,
CFG, ASLR, DEP, high-entropy VA and an empty delay-import directory. These checks were repeated
on the staged executable; logs are `out/v130-{dependents,headers}.log`. The dynamic-load audit
still finds only the existing System32-restricted installed-driver temperature loader.

The owner approved the implementation and requested 1.3.0 publication after the remaining
validation gaps were disclosed. The live AppBar was not launched or changed by the agent;
the existing, hash-matched process was passively observed for ten minutes. Clean-machine
execution and the full interactive/performance/hardware gates remain pending. See
[performance evidence](performance-review-1.3.0.md), [testing](testing.md) and
[release notes](releases/1.3.0.md). Publication does not establish that these gates passed.

## 1.2.0 release artifact

Packaged on 2026-10-09 with `pwsh -NoProfile -File scripts/package-release.ps1` after the
temperature-toggle review fix. Release configure/build and all four CTest suites passed
(4.02 s). The staged `out/release/1.2.0/Loadbar.exe` is 1,366,016 bytes, with product/file
version 1.2.0 and author Vorta. The ZIP contains only the same executable, verified byte-for-byte.

| Asset | SHA-256 |
| --- | --- |
| Loadbar.exe | `48b6e7b865e74f3e80aad073c6a18815b34b31218c397dd861730fb79163b107` |
| Loadbar-1.2.0-windows-x64.zip | `0289ae2eca7d3fb78b8e31ed71644da6047c5d0284ea1290d739d565eca33d87` |

`dumpbin /dependents` and `/headers` were repeated on the staged executable, confirming the
properties below. Logs are `out/v120-{dependents,headers}.log`; packaging output is
`out/v120-final-package.log`. The dynamic-load audit found only the restricted vendor loader.

The Release executable built on 2026-10-09 embeds Geist Mono, its complete OFL notice and a
native Licenses dialog. Resource tests compare the embedded font and notice byte-for-byte
with their repository files. Touching only `resources/fonts/GeistMono.ttf`, and then only
`resources/fonts/OFL.txt`, separately rebuilt `fonts.rc.res` and relinked `Loadbar.exe`.
The original files on `D:` are no longer build/runtime inputs.

`dumpbin /dependents out/build/windows-x64-release/Loadbar.exe` reported only Windows DLLs
and API sets; no application DLL, VC++ redistributable or ASan runtime. `/headers` confirmed
x64 GUI, CFG, ASLR, DEP, high-entropy VA and an empty delay-import directory.
The graphics temperature calls resolve through Windows GDI32; private fonts use DirectWrite.
The new optional vendor fallbacks dynamically load only System32 `nvapi64.dll`,
`amdadlx64.dll`, or `ControlLib.dll` supplied by installed drivers. They are not shipped or
mandatory imports. Their transitive driver dependencies need clean-machine verification;
static import inspection alone cannot establish this. Vendor notices are embedded and
resource-tested against their source bytes alongside the font notice.
Ninja's `fonts.rc.res` dependency query also lists all three vendor notices explicitly.

Clean-machine execution, minimum-build compatibility, the full interactive matrix, hardware
accuracy and performance/soak remain separate pending gates. The executable path is
`out/build/windows-x64-release/Loadbar.exe`. After initially keeping the candidate staged,
the owner reported successful manual use and requested [1.2.0](releases/1.2.0.md) publication
with the remaining validation gaps disclosed. Publication does not establish that those gates passed.

## 1.1.5 release artifact

Built on 2026-10-09 using `pwsh -NoProfile -File scripts/package-release.ps1`.
Release configure/build and all four CTest suites passed (3.87 s). The staged executable
is 1,082,880 bytes, with product/file version 1.1.5 and author Vorta. The packaging script
verified that the ZIP contains only the same executable, byte-for-byte; PDBs are not shipped.

| Asset | SHA-256 |
| --- | --- |
| Loadbar.exe | `5e3c0b45aaa6dab99aa492ba9fde0a7259b5c4b53f78d0b5312e1b3302ba94aa` |
| Loadbar-1.1.5-windows-x64.zip | `d3646f19af7f7e581218560de7d21f920d0dadf0ed54f02c7da769edce2dd9aa` |

`dumpbin /dependents out/release/1.1.5/Loadbar.exe` lists only Windows components, without
VC++ runtime DLL imports. Header inspection confirms x64, CFG, ASLR, DEP, high-entropy VA
and an empty delay-import directory. The source/CMake search found no explicit dynamic loads.
Resource tests verify version, author, manifest, icon and exact license.

The owner approved the tooltip changes and requested [1.1.5](releases/1.1.5.md) publication
with the existing performance, full interactive-Windows and clean-machine checks still pending.

## 1.1.4 release artifact

Built on 2026-10-09 using `pwsh -NoProfile -File scripts/package-release.ps1`.
Release configure/build and all four CTest suites passed (5.25 s). Embedded product/file
version is 1.1.4, author Vorta. The staged executable is 1,081,856 bytes. The ZIP contains
only the same executable, verified byte-for-byte; PDBs are not shipped.

| Asset | SHA-256 |
| --- | --- |
| Loadbar.exe | `d2c1d47ecbe83bcd788c7da5d8421f60a7aee4693d100d557818a4d0ef91a4b5` |
| Loadbar-1.1.4-windows-x64.zip | `55fd32889a40e41598a02d772ee1f5ef39a384363d1b2f48fa7116d81683bfc3` |

`dumpbin /dependents out/release/1.1.4/Loadbar.exe` lists only Windows components, without
VC++ runtime DLL imports. `dumpbin /headers out/release/1.1.4/Loadbar.exe` confirms x64, CFG,
ASLR, DEP, high-entropy VA and an empty delay-import directory. The source/CMake dynamic-load
search found no explicit loads. Resource tests verify version, author, manifest, icon and
exact license. Clean-machine execution remains pending.

Release notes: [1.1.4](releases/1.1.4.md). Publication was requested after clean adversarial
and performance reviews; the disclosed live/performance/standalone gates remain pending.

## 1.1.3 release artifact

Built on 2026-10-08 using `pwsh -NoProfile -File scripts/package-release.ps1`.
Release configure/build and all four CTest suites passed (2.84 s). Embedded product/file
version is 1.1.3, author Vorta. The staged executable is 1,080,832 bytes; the packaging
script verified that the ZIP contains only the same executable, byte-for-byte. PDBs are
not shipped.

| Asset | SHA-256 |
| --- | --- |
| Loadbar.exe | `caf318f7fc523b3e1feffbda1825a97ae7a0494d8bd115b5c7044b83ebbcf7c6` |
| Loadbar-1.1.3-windows-x64.zip | `cf207127df8afa6b81ca5994e9e4c4c76c439f12ae9bc23deedd55a38bb6f491` |

`dumpbin /dependents out/release/1.1.3/Loadbar.exe` lists only Windows components, without
VC++ runtime DLL imports. `dumpbin /headers out/release/1.1.3/Loadbar.exe` confirms x64, CFG,
ASLR, DEP, high-entropy VA and an empty delay-import directory. A source/CMake search for
`LoadLibrary`, `LoadPackagedLibrary`, `GetProcAddress` and `DELAYLOAD` found no explicit loads.
Resource tests verified the embedded version, author, manifest, icon and exact license.

Release notes: [1.1.3](releases/1.1.3.md). The owner reported successful manual use and
explicitly requested publication after the unmeasured performance and remaining manual
checks were disclosed. Those checks remain pending; publication is not evidence they passed.

## 1.1.1 release artifact

Built on 2026-10-08 using `pwsh -NoProfile -File scripts/package-release.ps1`.
Release configure/build and all four CTest suites passed (3.20 s). Embedded product/file
version is 1.1.1, author Vorta. `out/build/windows-x64-release/Loadbar.exe` and the staged
`out/release/1.1.1/Loadbar.exe` are 1,068,032 bytes. The ZIP contains only that executable,
verified byte-for-byte by the packaging script. PDBs are not shipped.

| Asset | SHA-256 |
| --- | --- |
| Loadbar.exe | `39619f7778cf4da4577a4bbdbc2f7bb2e3a990ee8fb032082fee529646f012e2` |
| Loadbar-1.1.1-windows-x64.zip | `333f8c331b2a0a684cf98a6df5ee014839fc8da5ceba5129c513c20ddaf55b21` |

`dumpbin /dependents out/release/1.1.1/Loadbar.exe` lists only Windows components and no
VC++ runtime DLL. `dumpbin /headers out/release/1.1.1/Loadbar.exe` confirms x64, CFG,
ASLR, DEP, high-entropy VA and an empty delay-import directory. A source/CMake search for
`LoadLibrary`, `LoadPackagedLibrary`, `GetProcAddress` and `DELAYLOAD` found no explicit loads.
Resource tests verified the embedded version, author, manifest, icon and license.

Release notes: [1.1.1](releases/1.1.1.md). Publication was explicitly requested after the
pending live/performance/clean-machine checks were disclosed; these checks remain pending.

## Published 1.1.0 artifact

`out/build/windows-x64-release/Loadbar.exe` was built on 2026-10-08 with embedded version
1.1.0 and author Vorta (1,071,616 bytes), including drive visibility. `dumpbin /dependents`
lists only Windows components, with no VC++ runtime DLL dependency. A source/build search for `LoadLibrary`,
`LoadPackagedLibrary`, `GetProcAddress` and `DELAYLOAD` found no explicit dynamic/delayed loads.
SHA-256: `B22AD2C4D7ED83B48F99AB02A2DBC310F0F2FC2AF59D10AB30B109A8BE008ECB`.
Header inspection confirms x64, CFG, ASLR, DEP, high-entropy VA and no delay-import entries.
The release package was staged using `pwsh -NoProfile -File scripts/package-release.ps1`:
Release configure/build passed, all four CTest suites passed in 3.57 s, and the ZIP's sole
`Loadbar.exe` entry matched the loose executable byte-for-byte. ZIP SHA-256:
`0FFCE78D275CF6D4A38E990DF79A06CC1FA60ADE9A94ECEA344BA028FF6E2197`.

Version 1.1.0 is published at [Vorta/Loadbar](https://github.com/Vorta/Loadbar/releases/tag/v1.1.0).
Release notes are in [releases/1.1.0.md](releases/1.1.0.md). Publication was explicitly requested;
clean-machine packaging and the live/performance release gates remain pending.

## Published 1.0.0 artifact

Version **1.0.0**, author **Vorta**, built on 2026-10-08:
The build output at publication was **1,040,384 bytes**; the current local build path
now contains 1.1.5.

SHA-256: `7FB28EBFDBBA2F206DFD8B7A1327F0367C1D251402A9B1F4BCD5671E1DE4BB7B`.

This identifies the inspected artifact, not a reproducible-build guarantee. PDBs remain
separate. Defaults, icon, manifest, version and MIT notice are embedded.

## Inspection results

The following commands were run from the pinned developer environment:

```powershell
dumpbin /dependents out/build/windows-x64-release/Loadbar.exe
dumpbin /headers out/build/windows-x64-release/Loadbar.exe
mt.exe '-inputresource:out/build/windows-x64-release/Loadbar.exe;#1' '-out:out/release-1.0.0.manifest'
Get-FileHash out/build/windows-x64-release/Loadbar.exe -Algorithm SHA256
```

Imports were Windows DLLs/API sets: WTSAPI32, COMCTL32, PDH, SETUPAPI, IPHLPAPI, OLE32,
DXGI, DXCore, USER32, SHELL32, ADVAPI32, D2D1, DWrite, KERNEL32, GDI32, scaling, WinRT and
synchronization. No application DLL, VC++ Redistributable or ASan runtime was imported.
The delay-import directory was empty. PE inspection confirmed x64 Windows GUI, CFG, ASLR,
DEP and high-entropy VA; the manifest contained asInvoker and PerMonitorV2.

Resource tests validate version/author, manifest/icon presence and exact license bytes.
A separate preparation-time inspection confirmed all seven embedded icon payloads matched
the supplied ICO. Release compilation commands used the named C++23-preview mode and /MT.

No explicit application LoadLibrary/GetProcAddress or plugin path was found. Windows graphics
and WinRT APIs may load OS/driver components internally; import inspection does not establish
clean-machine or driver compatibility.

## Create a local package

```powershell
pwsh -NoProfile -File scripts/package-release.ps1
```

The script configures/builds/tests Release and stages the executable, a single-entry
`Loadbar-<version>-windows-x64.zip`, and SHA256SUMS.txt under `out/release/<version>/`.
It reads tested executable version metadata and verifies that the ZIP's Loadbar.exe bytes
match the loose executable. It performs no Git or publishing operations.

Version 1.0.0 is the initial public release at
[Vorta/Loadbar](https://github.com/Vorta/Loadbar/releases/tag/v1.0.0), using one root commit
and tag `v1.0.0`. Release notes are in [releases/1.0.0.md](releases/1.0.0.md).

## Pending release gates

Copy only Loadbar.exe to a standard-user directory on clean Windows 11 x64 without Visual
Studio, developer PATH entries or a separately installed VC++ Redistributable. Verify launch,
resources, devices, settings and Exit with no elevation or missing DLL. Repeat on intended
minimum build 22000. Record artifact hash, OS, actually loaded dependencies and results.

Clean-VM packaging and physical-GPU accuracy are separate checks. Interactive Windows,
hardware accuracy, resource/fault recovery, [performance and soak](performance.md) remain
pending. Publication was explicitly authorized after README/media approval; it does not
establish that these verification gates have passed.
