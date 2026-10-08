# Packaging and release evidence

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
now contains 1.1.1.

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
