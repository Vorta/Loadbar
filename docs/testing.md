# Testing and verification

## Recorded validation

The 1.0.0 preparation on 2026-10-08 used the pinned [toolchain](toolchain.md).
All four configurations built successfully and passed all four CTest suites:

| Preset | CTest | Test duration |
| --- | --- | --- |
| windows-x64-debug | 4/4 passed | 14.21 s |
| windows-x64-release | 4/4 passed | 3.41 s |
| windows-x64-relwithdebinfo | 4/4 passed | 3.62 s |
| windows-x64-asan | 4/4 passed; no reported ASan errors | 26.93 s |

Format-check, full clang-tidy and MSVC analysis passed. The optional README renderer was
also built and analyzed. These are build/test results, not application-overhead measurements.
Release import, resource and package inspection is recorded in [packaging.md](packaging.md).

Documentation cleanup was rechecked on the same date with `cmake --build --preset <preset>`
and `ctest --preset <preset> --output-on-failure` for Debug and Release: both incremental
builds passed, with 4/4 tests in 14.95 s and 3.53 s respectively. Debug `format-check` passed.
README, contract, source, tests, build configuration and artwork matched the pre-cleanup hashes;
only documentation and an obsolete icon generator changed. All 43 local Markdown links resolved.
ASan, full analysis and live checks were not repeated for this documentation-only cleanup.

Earlier opt-in, read-only provider smoke runs exercised CPU/RAM, both discovered disks,
GPU and the selected network interface after priming. A disk idle counter occasionally
exceeded 100%, correctly producing an active-time error while read/write remained valid.
These checks demonstrate provider execution on the build host, not hardware accuracy or
support across other systems. No load was generated or AppBar registered by those runs.

**Pending:** live Shell/fullscreen behavior, hardware accuracy, accessibility acceptance,
performance/soak and clean-machine packaging. Native hidden-window tests and offscreen
images do not establish these outcomes.

## Reproduce automated checks

Run in PowerShell 7 from the repository root:

```powershell
. .\scripts\enter-dev-shell.ps1
foreach ($preset in @('windows-x64-debug','windows-x64-release','windows-x64-relwithdebinfo','windows-x64-asan')) {
    cmake --preset $preset
    cmake --build --preset $preset
    ctest --preset $preset --output-on-failure
}
cmake --build --preset windows-x64-debug --target format-check
cmake --build --preset windows-x64-debug --target tidy
cmake --build --preset windows-x64-debug --target analyze
```

CTest uses a small native runner with checks active in Release. Ordinary suites need no
specific hardware and never register a live AppBar:

| Suite | Coverage |
| --- | --- |
| loadbar-unit | Rates/statuses, settings migration, topology/identity parsing, GPU aggregation, bounded delivery, discovery failures, continuity and session peaks |
| loadbar-resources | Embedded icon, manifest, exact license bytes and version/author metadata |
| loadbar-rendering | Production Direct2D rendering into WIC bitmaps, layout/scale, 0/50/100% colors/fills, high contrast, hover and resource failure/recovery |
| loadbar-settings | Hidden native controls, draft/apply/cancel, keyboard/scrolling layout, monitor refresh, tray flags and resize invalidation |

Deterministic cases include irregular/zero intervals, reset/reconnect, non-finite data, 64-bit
counters, multiple processor groups, heterogeneous/SMT cores, invalid/duplicate/reordered
PDH instances, separate GPU engines/adapters, and incomplete discovery versus confirmed removal.
Geometry covers all edges, negative origins, mixed DPI, Shell-adjusted rectangles, bounded
renegotiation, readable minimums and complete core visibility. Lifecycle tests cover balanced
registration, reentrancy, partial startup, worker replacement and late callbacks.

Synthetic render fixtures are written to `out/build/<preset>/render-fixtures/`. Hidden
resize tests observe the production invalidation request because invisible HWNDs do not
retain an observable paint region. They do not prove compositor behavior.

### Incremental resource regressions

```powershell
. .\scripts\enter-dev-shell.ps1
& .\scripts\check-manifest-incremental.ps1
& .\scripts\check-manifest-incremental.ps1 -Resource License
```

Both modes passed during implementation with the pinned Ninja generator. They copy sources
under `out/build/`, build, alter only the copied input and verify the resulting embedded
resource after an incremental rebuild. License mode first confirms the stale artifact fails
the exact-byte check. Manifest mode tests both the app and Settings test executable.
Neither launches Loadbar. Icon/version/license dependency checks also remain in CMake/tests.

### Optional real-provider smoke check

```powershell
.\out\build\windows-x64-release\loadbar_tests.exe --live-providers
```

This explicitly reads real device counters and is not part of CTest. Run only when hardware
access is intended; it does not create a bar or generate workload. Compare scope/interval
separately before treating any reading as accurate.

## First manual test

1. Use the Release executable and default 1 Hz sampling. Record Windows build, monitor/DPI,
   selected devices and executable hash.
2. Confirm one bar, every core/disk, device identities, warm-up zero and truthful unavailable
   states. Hover for exact values; check raw status and retained age in Settings.
3. Try Settings and tray with keyboard only. Apply/Cancel should be disabled for an unchanged
   form. Resize Settings and confirm the readings area grows and the footer stays reachable.
4. Exit through the tray/bar menu or Settings **Exit Loadbar**. Confirm work-area restoration
   and no lingering tray icon before proceeding to fault tests.

### Interactive matrix — pending

| Procedure | Expected observation |
| --- | --- |
| Maximize/Snap on three displays with taskbar/another AppBar | Only the selected monitor reserves the negotiated strip; no drift/overlap |
| Every edge, portrait rotation and thickness 40 → 60 → 80 → 40 | Full edge and all widgets remain visible; graphics scale until capped; explicit edge survives rotation |
| Another AppBar shortens/lengthens the edge with unchanged readings | Immediate reflow and recomputed minimum independent of new telemetry |
| 100/125/150/200% DPI, text scale, light/dark high contrast | Consistent reservation/content; distinguishable CPU load; controls and tooltips readable |
| Preferred monitor disconnect, primary change, reconnect | Single primary fallback, retained preference, automatic return; open Settings choices refresh without losing edits |
| Explorer restart | Tray and AppBar restored once; repeated failure leaves Settings/Exit |
| Second launch | Existing-instance report; no additional reservation |
| Lock/unlock and suspend/resume, including resume while locked | No catch-up bursts; re-primed counters; sampling paused until unlocked |
| Exclusive/borderless fullscreen on selected/other monitors | No focus/topmost fight; retained reservation and restoration without activation |
| Normal Exit; separately forced termination in a disposable session | Normal cleanup releases work area; Shell/next-launch recovery after forced termination |
| NIC/disk disconnect, counter failure, rediscovery failure | Last-valid values retain original age; no identity swap; removal only after completed discovery |
| GPU reset/render-target loss | Resource recovery with other providers responsive |
| Malformed settings or denied registry writes in a disposable profile | Defaults for invalid data; save failure visible with active session retained |
| Narrow/tall Settings, large text, Tab/Shift+Tab and Narrator | Readings resize; footer/scrolling controls reachable; stable accessible details |
| Tray icon hover and keyboard focus | Standard Loadbar tooltip, usable menu and Exit |
| English/non-English Windows; integrated/discrete/multiple GPUs | Language-neutral counters and correctly scoped device/memory selection |

AppBar launch, display changes, Explorer restart, fault injection and load generation are
opt-in manual operations, not automatic tests.

## Hardware accuracy — pending

Compare CPU with matching Processor Information logical instances and intervals. Compare each
disk's read/write and `100 - % Idle Time` against that exact PhysicalDisk instance. Compare
NIC octet deltas over the same elapsed interval and selected identity. Match GPU LUID, physical
node, engine type, interval and memory scope; exercise multiple processes and reconnects.

Use Task Manager only as a sanity reference. Valid idle requires actual valid observations;
record anomalous raw values before display clipping. Performance/soak procedures are in
[performance.md](performance.md); clean-machine checks are in [packaging.md](packaging.md).

## Documentation media

The optional fixture and encoding commands are in [animation.md](animation.md). The final
normal-mode fixture built and passed targeted clang-tidy/MSVC analysis. Its GIF was decoded
as 24 one-second frames at 1400 × 60, looping; the still matched its source frame exactly.
Media is synthetic and is not a desktop recording. GitHub rendering remains unverified.
