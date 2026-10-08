# Packaging and release evidence

## Prepared artifact

Version **1.0.0**, author **Vorta**, built on 2026-10-08:
`out/build/windows-x64-release/Loadbar.exe`, **1,040,384 bytes**.

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
