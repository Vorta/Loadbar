# Testing and verification

## Temperature control and resource follow-up (1.2.0)

On 2026-10-09, final Debug, Release, RelWithDebInfo and ASan configure/builds passed all
four CTest suites each (10.80 s, 4.15 s, 4.25 s and 26.16 s). Logs are
`out/v120-final-<preset>-{configure,build,test}.log`. These include the review fix for
coalesced temperature toggles, not only the earlier provider/rendering changes.

Coverage includes schema 12 migration/defaults/validation; the fifth native checkbox's
background, keyboard order, draft/Cancel and responsive layout; disabled cold-start and
zero new sensor calls; pending cancellation and prompt re-enable; completed-buffer release;
last-valid ages; and no unrelated counter/peak reset. A real worker started paused verifies
that off/on transitions survive configuration coalescing without performing hardware reads.
Production-renderer tests cover all edges, hover/high contrast, no-temperature pixel
equivalence, cache reuse, font invalidation and failed-resource recovery. Disabled
temperatures do not schedule freshness/retention deadlines or trigger layout/paint changes.

Commands run from the pinned developer environment:

```powershell
. ./scripts/enter-dev-shell.ps1
foreach ($preset in @('windows-x64-debug', 'windows-x64-release', 'windows-x64-relwithdebinfo', 'windows-x64-asan')) {
    cmake --preset $preset
    cmake --build --preset $preset
    ctest --preset $preset --output-on-failure
}
cmake --build --preset windows-x64-debug --target format-check
cmake --build --preset windows-x64-debug --target tidy
cmake --build --preset windows-x64-debug --target analyze
pwsh -NoProfile -File scripts/package-release.ps1
dumpbin /dependents out/release/1.2.0/Loadbar.exe
dumpbin /headers out/release/1.2.0/Loadbar.exe
git -c core.safecrlf=false diff --check
```

The first full clang-tidy run flagged an optional access in a new test. Binding the optional
to a const reference before its checked use resolved it without suppressions or weakened
assertions. Final format-check, full clang-tidy and MSVC analysis passed with no actionable
project diagnostics. Logs are `out/v120-final-{format-check,tidy,analyze}.log`. Packaging
repeated Release tests successfully (4/4, 4.02 s), verified the single-entry ZIP, and staged
the exact inspected executable. Hashes and import/security evidence are in [packaging.md](packaging.md).

The first adversarial pass had no actionable findings and independently probed failed
cancellation, path rebinding, late completion after disable and cleanup. The performance
pass confirmed all three original resource improvements but found that coalesced off/on
settings could lose the temperature reset. The added temperature generation fixes that
path independently of the GPU/network/global counter generations; review passes restart
after this fix. Fake/offscreen counts are in [the performance review](performance-review-1.2.0.md).

The follow-up performance review found no actionable issues. Its isolated `/O2 /MT /W4 /WX`
production-code probe (`out/v120-performance-one/build-final.ps1`) passed the actual-worker
coalescing, Collector reset isolation, provider and renderer regressions, preserving the
previously measured reductions in vendor calls and hover-layout creation.

A different final reviewer then found no actionable correctness, lifecycle, settings,
rendering-cache or resource issues. Its independent three-drive probe exercised delayed
successful completion after failed cancellation and rapid off/on, alongside the production
temperature/vendor regressions. The production Release offscreen renderer passed 24
continuity/resize, 49 scaling and 65 fixture checks in its isolated output directory.
Evidence is under `out/review-v120-final-independent/`. This establishes two independent,
consecutive clean reviews after the coalescing fix, not completion of the manual release gates.

Independent probes (fake providers/offscreen rendering only) completed with exit code 0:

```powershell
./out/vendor-capability/run-tests.ps1
./out/v120-performance-one/build-final.ps1
pwsh -NoProfile -File out/review-v120-final-independent/run.ps1
./out/build/windows-x64-release/loadbar_render_tests.exe out/review-v120-final-independent/render
```

Manual follow-up: exit the current application, launch the candidate, open Settings and
apply Show temperatures off/on. Confirm readouts/effects disappear, ordinary metrics continue,
and available temperatures return; repeat on a vertical edge and after reopening Settings.
Close dismisses Settings; Exit Loadbar or tray/bar Exit ends monitoring. Live Shell/display
changes, hardware accuracy, controlled 1-Hz performance, one-hour soak and clean-machine
execution have not been performed by this change.
The owner initially kept v1.2.0 staged, then reported successful manual use and requested
publication with the remaining validation gaps disclosed. The agent did not launch or
reconfigure the live AppBar. The owner's report does not establish completion of the full
manual validation matrix or the performance and clean-machine gates.

Before publication, `cmake --preset windows-x64-release`, `cmake --build --preset
windows-x64-release` and `ctest --preset windows-x64-release --output-on-failure` passed
again (4/4, 4.03 s). SHA-256 verification confirmed the rebuilt executable, staged executable
and sole ZIP entry were identical; both assets matched `SHA256SUMS.txt`.
The staged full diff check reported whitespace in the byte-preserved upstream headers,
notices and third-party checksum list. The project-owned diff check passed with
`git diff --cached --check -- . ':(exclude)third_party/**' ':(exclude)resources/fonts/OFL.txt'`;
all vendor files matched their pinned SHA-256 values. Upstream files were not reformatted.

## Temperature tint and alignment refinement (1.2.0, unpublished)

On 2026-10-09, Debug, Release, RelWithDebInfo and ASan configure/builds succeeded and each
passed all four CTest suites (10.91 s, 5.77 s, 5.88 s and 26.07 s respectively).
The non-mutating format check, full clang-tidy and MSVC analysis passed with no actionable
project diagnostics. Analysis logs are `out/temperature-tint-tidy.log` and
`out/temperature-tint-analyze.log`.

New tests cover original icon colors through 60°C, a half blend at 75°C, final red at 90°C,
the 90–95°C glow ramp for all four thermal component types, and saturation above 95°C.
Production-renderer tests distinguish fractional tint/glow changes at the same displayed
integer, retain high-contrast behavior and verify that network is unaffected. Resource
creation counters confirm that fractional changes reuse text layouts/ink measurements and
glow masks. Existing retained/unavailable and resource-failure recovery tests also pass.

Geometry and embedded-font ink checks cover all edges, both CPU shape settings, 40/60/120-DIP
requests (with the readable minimum), Windows text scales 1/1.5/2.25 and 96/120/144/192 DPI.
Signed and three-digit readouts clear their icons and stay within hit/paint bounds. Horizontal
ink bottoms share the graphic bottom after pixel snapping. Initial test failures identified
two old expectations (16-DIP icons and unchanged fractional tint); these were updated to the
new behavior with separate coverage for unchanged text outside the ramps.

Commands run from the pinned developer environment:

```powershell
. ./scripts/enter-dev-shell.ps1
foreach ($preset in @('windows-x64-debug', 'windows-x64-release', 'windows-x64-relwithdebinfo', 'windows-x64-asan')) {
    cmake --preset $preset
    cmake --build --preset $preset
    ctest --preset $preset --output-on-failure
}
cmake --build --preset windows-x64-debug --target format-check
cmake --build --preset windows-x64-debug --target tidy
cmake --build --preset windows-x64-debug --target analyze
dumpbin /dependents out/build/windows-x64-release/Loadbar.exe
git -c core.safecrlf=false diff --check
```

Release `Loadbar.exe` is 1,360,384 bytes, reports 1.2.0, and imports only Windows components.
SHA-256: `A812BA480BDAFCF7E6DDBA00CEBBC0FCCB5B4D0D82F9CF4BE5B2E6D50F3AB665`.
Installed-driver optional loads remain unchanged. Fresh reference previews and five
`temperature-ramp-top-1400x60-2x-*.png` examples are in the Release `render-fixtures` folder.
Horizontal normal/maximum-glow and left/right previews were visually inspected. These are
synthetic readings, including RAM; no new hardware accuracy or live performance claim follows.

Manual check: launch the Release executable, select Top and 60 DIPs, inspect icon size and
number alignment, then check Left/Right with either CPU shape setting. Temperatures appear
only after valid provider readings; live RAM collection remains outside provider coverage.
Exit via the tray or Settings → Exit Loadbar. No AppBar was launched during this work;
interactive Windows, hardware accuracy, performance/soak and clean-machine packaging remain
pending. No commit, push or release was performed.

## Temperature design alignment (1.2.0, unpublished)

On 2026-10-09, the full strip was aligned to the original 1200 × 60-DIP component
export. Debug, Release, RelWithDebInfo and ASan configure/builds succeeded; each passed
all four CTest suites (13.21 s, 8.13 s, 8.55 s and 31.74 s respectively). Formatting,
full clang-tidy and MSVC analysis passed with no actionable project diagnostics. Logs are
`out/temperature-spacing-tidy.log` and `out/temperature-spacing-analyze.log`.

New deterministic tests assert the reference positions, margins, divider, icon/readout
centers, equal device widths, readable temperature text, and non-overlap on all four edges
at 40/60/120 DIPs and increased text scaling. DirectWrite tests check actual embedded-font
ink bounds for `64°C`, `100°C`, `250°C` and `-99°C`. Font-failure recovery now covers the
additional cached temperature format. The shortened-Shell-edge fixture was updated from
600 to 640 DIPs: under the new spacing, shortening to 560 DIPs raises the readable minimum
from 40 to 64 DIPs. The renegotiation and bounded-retry assertions remain intact.

The production renderer generated 24 normal/hot/critical comparison PNGs at 1×/2× density:
`temperature-reference-{top,left,right}-*-{1x,2x}-{normal,hot,critical}.png` in each build's
`render-fixtures` directory. Horizontal previews include 1200 × 60 and 1400 × 60 DIPs;
vertical previews are 60 × 1400 DIPs. Release horizontal and both vertical previews were
visually inspected, including comparison against the original 2× export. All example
readings are synthetic. Existing temperature availability/retention/high-contrast previews
were regenerated. Both local reference images match the supplied originals by SHA-256.

Commands run from the pinned developer environment:

```powershell
. ./scripts/enter-dev-shell.ps1
foreach ($preset in @('windows-x64-debug', 'windows-x64-release', 'windows-x64-relwithdebinfo', 'windows-x64-asan')) {
    cmake --preset $preset
    cmake --build --preset $preset
    ctest --preset $preset --output-on-failure
}
cmake --build --preset windows-x64-debug --target format-check
cmake --build --preset windows-x64-debug --target tidy
cmake --build --preset windows-x64-debug --target analyze
dumpbin /dependents out/build/windows-x64-release/Loadbar.exe
git -c core.safecrlf=false diff --check
```

The Release EXE is 1,356,288 bytes, reports 1.2.0, and imports only Windows components.
SHA-256: `4284D7FD9791D8E84ABB0B476155331D7E9F59DC4EE8C4943C99E61DB2752B8D`.
No live AppBar was launched or changed, no hardware provider was newly validated, and no
performance/soak or clean-machine packaging gate was run. For manual comparison, launch the
Release executable, select Top and 60 DIPs, then compare both CPU-shape settings and the
Left/Right edges. Exit via the tray menu or Settings → Exit Loadbar. Real temperatures
appear only when their provider has returned a valid reading; RAM collection remains
unfinished. This change was not committed, published or released.

## Temperatures and Geist Mono (1.2.0, unpublished)

Implemented and checked on 2026-10-09 with the unchanged pinned toolchain. Final complete
CTest runs passed **4/4** in Debug (11.22 s), Release (5.88 s), RelWithDebInfo (5.12 s), and
ASan (23.89 s). A subsequent test-only explicit-float correction passed the rendering suite
again in all four presets. Formatting, full clang-tidy and MSVC analysis passed after fixing
the optional-access, narrowing and cleanup-result findings. A final GPU error-reporting
correction passed targeted clang-tidy, unit/resource tests in all four presets and MSVC
analysis again. No diagnostics were disabled to obtain these results.

Coverage includes sensor selection/duplicate indices, malformed/truncated descriptors,
signed values, thresholds, no initial fabricated zero, identity-scoped retention and age,
one-Hz cadence, sleep/Hide/discovery failure, access errors, reconnect with unchanged identity,
asynchronous completion, cancellation and retry backoff. Offscreen production-renderer tests
cover all four edges, absent/normal/critical/retained/mixed readings, unchanged icons before
first observation, high contrast, 100/150/200% DPI/text scaling, font weights/cache/recovery,
and temperature-only repaint decisions. The repaint test uses a hidden HWND; it does not
exercise live Shell behavior. Larger test fixtures were moved to owned heap storage to
preserve the MSVC stack-usage check, without suppressing it.

Commands used from the developer shell, repeated after relevant fixes:

```powershell
. ./scripts/enter-dev-shell.ps1
foreach ($preset in @('windows-x64-debug', 'windows-x64-release', 'windows-x64-relwithdebinfo', 'windows-x64-asan')) {
    cmake --preset $preset
    cmake --build --preset $preset
    ctest --preset $preset --output-on-failure
}
cmake --build --preset windows-x64-debug --target format-check tidy analyze
cmake --build --preset windows-x64-debug --target analyze
clang-tidy -p out/build/windows-x64-debug/tidy-commands src/telemetry/temperature.cpp tests/settings_window.cpp tests/discovery.cpp tests/continuity.cpp
foreach ($preset in @('windows-x64-debug', 'windows-x64-release', 'windows-x64-relwithdebinfo', 'windows-x64-asan')) {
    cmake --build --preset $preset
    ctest --preset $preset -R loadbar-rendering --output-on-failure
}
cmake --build --preset windows-x64-debug --target format-check analyze
cmake --build --preset windows-x64-debug --target tidy
clang-tidy -p out/build/windows-x64-debug/tidy-commands src/telemetry/temperature.cpp
foreach ($preset in @('windows-x64-debug', 'windows-x64-release', 'windows-x64-relwithdebinfo', 'windows-x64-asan')) {
    cmake --build --preset $preset
    ctest --preset $preset -R 'loadbar-unit|loadbar-resources' --output-on-failure
}
cmake --build --preset windows-x64-debug --target format-check analyze
cmake --build --preset windows-x64-release --target loadbar_temperature_probe
& .\out\build\windows-x64-release\loadbar_temperature_probe.exe --read-only
foreach ($asset in @('GeistMono.ttf', 'OFL.txt')) {
    cmake -E touch "resources/fonts/$asset"
    cmake --build --preset windows-x64-release --target loadbar
}
dumpbin /dependents out/build/windows-x64-release/Loadbar.exe
dumpbin /headers out/build/windows-x64-release/Loadbar.exe
rg -n 'LoadLibrary|LoadPackagedLibrary|GetProcAddress|DELAYLOAD' src CMakeLists.txt cmake
git -c core.safecrlf=false diff --check
```

The explicitly invoked read-only probe ran without elevation and returned GPU **57.3°C**
(main sensor, physical adapter 0), drive 1 **59°C**, and drive 2 **41°C** (device sensor 0).
It opened no AppBar/window and generated no workload. These observations establish provider
availability on this host only, not sensor accuracy or coverage of other hardware. Ordinary
tests use fake providers. This initial check preceded the ASUS CPU/vendor GPU extension
recorded below. RAM temperature remains outside provider coverage. No release was published.

Release synthetic previews are in `out/build/windows-x64-release/render-fixtures/`:
`temperature-top-60-mixed.png` shows available GPU/one-drive temperatures; `temperature-*-60-normal.png`
and `temperature-*-60-critical.png` demonstrate every component, including synthetic CPU/RAM.
Both horizontal and vertical Release previews were visually inspected.

For the first manual test, exit the existing Loadbar through its tray/bar menu, then launch
`out/build/windows-x64-release/Loadbar.exe`. Check the first-reading icon transition, CPU, GPU and
both drive readouts, Settings sensor/status details, independent hover/tooltip toggles, Hide,
all edges and thicknesses, and the new **Licenses** dialog. Use **Exit Loadbar** in Settings
or **Exit** in the tray/bar menu for normal cleanup. Compare temperatures with the same sensor
scope in a trusted hardware tool; do not equate GPU hotspot and main temperatures.

Live AppBar/DPI/text-scale/accessibility checks, actual sensor-failure retention, sleep/resume,
removable-drive cancellation, integrated/linked GPUs, storage bridges, HDD sleep behavior,
clean-machine/minimum-Windows execution and controlled performance/soak remain pending.

### ASUS CPU and vendor GPU extension

On 2026-10-09, the final incremental builds passed all four CTest suites in Debug
(12.83 s), Release (4.63 s), RelWithDebInfo (5.60 s) and ASan (25.08 s), with the
same pinned toolchain. Format-check, full clang-tidy and MSVC analysis passed. The vendor regression
tests inject API tables: LUID/physical-member matching, sensor filtering, missing exports,
failed initialization, malformed enumeration, non-finite readings, cached bindings,
exception cleanup, fallback, backoff, Hide and retained source/age. ASUS tests cover the
exact read-only request, parser bounds, cadence, pending I/O, timeout, reset and shutdown.
These fakes do not establish real driver ABI compatibility or sensor accuracy.

Additional commands (the four-preset build/CTest loop above was repeated):

```powershell
cmake --build --preset windows-x64-debug --target format-check
cmake --build --preset windows-x64-debug --target analyze
cmake --build --preset windows-x64-debug --target tidy
cmake --build --preset windows-x64-release --target loadbar_temperature_probe
& ./out/build/windows-x64-release/loadbar_temperature_probe.exe --read-only --vendors
dumpbin /dependents out/build/windows-x64-release/Loadbar.exe
dumpbin /headers out/build/windows-x64-release/Loadbar.exe
ninja -C out/build/windows-x64-release -t query CMakeFiles/loadbar.dir/resources/fonts.rc.res
rg -n 'LoadLibrary|LoadPackagedLibrary|GetProcAddress|DELAYLOAD' src CMakeLists.txt cmake
git -c core.safecrlf=false diff --check
```

The Release probe ran with a non-elevated token and returned ASUS firmware CPU **97°C**,
Windows GPU **59.6°C**, NVIDIA NVAPI GPU core **59°C**, and drives **61/45°C**. These are
transient observations during development checks, not an idle baseline or cross-sensor
accuracy comparison. IGCL initialized but reported no temperature sensors on this host;
AMD hardware/runtime was unavailable for a real ADLX check. NVIDIA first bind/read took
13.48 ms; Intel initialization/enumeration took 259.79 ms. Neither is steady-state overhead.

An earlier Debug vendor probe exposed a CFG failure when invoking a cached
`GetProcAddress` pointer after loading the NVIDIA shim. Keeping the import call inside
an application lambda fixed the reproduction; subsequent Debug and Release probes exited
successfully with CFG still enabled. Static-analysis findings were fixed without disabling
checks. The Release import table contains Windows components only; PE headers retain
x64, ASLR, DEP, high-entropy VA and CFG. Optional vendor DLLs are loaded from System32;
their installed-driver dependencies still require clean-machine validation. All 15 vendor
material hashes matched `third_party/SHA256SUMS`.

Pending: AMD hardware ABI/edge readings, Intel hardware that exposes GPU-domain sensors,
other ASUS firmware variants, linked GPUs, actual failure/recovery and cancellation latency,
sensor-scope accuracy comparisons, live UI, controlled performance/soak and clean-machine
packaging. No AppBar was launched and no existing Loadbar process was stopped for these checks.

## 1.1.5 publication build

On 2026-10-09, `pwsh -NoProfile -File scripts/package-release.ps1` built version 1.1.5,
passed all four Release CTest suites (3.87 s), and verified the single-entry ZIP against
the tested executable. Debug, RelWithDebInfo and ASan were reconfigured/rebuilt with the
release version and passed 4/4 suites each (10.41 s, 4.35 s and 14.54 s respectively).
Formatting and MSVC analysis passed. Clang-tidy evidence for the unchanged implementation
is recorded below; release preparation changed version metadata and documentation only.

```powershell
pwsh -NoProfile -File scripts/package-release.ps1
. ./scripts/enter-dev-shell.ps1
foreach ($preset in @('windows-x64-debug', 'windows-x64-relwithdebinfo', 'windows-x64-asan')) {
    cmake --preset $preset
    cmake --build --preset $preset
    ctest --preset $preset --output-on-failure
}
cmake --build --preset windows-x64-debug --target format-check analyze
dumpbin /dependents out/release/1.1.5/Loadbar.exe
dumpbin /headers out/release/1.1.5/Loadbar.exe
rg -n 'LoadLibrary|LoadPackagedLibrary|GetProcAddress|DELAYLOAD' src CMakeLists.txt cmake
git -c core.safecrlf=false diff --check
```

Package hashes and import/header inspection are in [packaging.md](packaging.md). The owner
approved the tooltip changes after manual use. The detailed interactive matrix, hardware
accuracy, controlled performance/soak and clean-machine execution remain pending.

## Independent tooltips and compact quantities (1.1.5)

Implemented on 2026-10-09. Model tests cover one-decimal MB/GB formatting for capacities,
rates and peaks, zero versus tiny positive readings, unit boundaries, invalid values,
warm-up, failures and retained observations. RAM/GPU popup tests keep matching capacities
and scope. Ordinary inline/Settings formats remain unchanged.

Settings tests exercise all 16 combinations of four preference flags, schema 1–10 migration,
schema 11 validation, checkbox draft/cancellation, background pixels, keyboard order and
responsive layout. A hidden native tooltip records pop/activation requests for all four
inline/popup combinations and verifies cache invalidation. Offscreen rendering tests verify
that tooltip enablement does not alter pixels and that inline-only and tooltip-only modes work.
Existing cache tests cover sample/layout changes and retention-deadline expiry.

Commands run from the pinned developer environment:

```powershell
. ./scripts/enter-dev-shell.ps1
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug --output-on-failure
foreach ($preset in @('windows-x64-release', 'windows-x64-relwithdebinfo', 'windows-x64-asan')) {
    cmake --preset $preset
    cmake --build --preset $preset
    ctest --preset $preset --output-on-failure
}
cmake --build --preset windows-x64-debug --target format-check tidy analyze
cmake --build out/build/windows-x64-debug/analyze
cmake --build --preset windows-x64-debug --target format-check analyze
clang-tidy -p out/build/windows-x64-debug/tidy-commands src/model/model.cpp tests/metrics_upgrade.cpp tests/settings_window.cpp
clang-tidy -p out/build/windows-x64-debug/tidy-commands tests/settings_window.cpp
foreach ($preset in @('windows-x64-debug', 'windows-x64-release', 'windows-x64-relwithdebinfo', 'windows-x64-asan')) {
    cmake --build --preset $preset
    ctest --preset $preset -R loadbar-unit --output-on-failure
}
foreach ($preset in @('windows-x64-debug', 'windows-x64-release', 'windows-x64-relwithdebinfo', 'windows-x64-asan')) {
    cmake --build --preset $preset
    ctest --preset $preset -R loadbar-settings --output-on-failure
}
dumpbin /dependents out/build/windows-x64-release/Loadbar.exe
dumpbin /headers out/build/windows-x64-release/Loadbar.exe
rg -n 'LoadLibrary|LoadPackagedLibrary|GetProcAddress|DELAYLOAD' src CMakeLists.txt cmake
git -c core.safecrlf=false diff --check
```

All four configurations passed all four CTest suites. Formatting and MSVC analysis passed.
A full clang-tidy scan found one callback conversion in the new test; using the existing Win32
pointer-conversion helper resolved it. The targeted follow-up passed. A null-handle diagnostic
in that test was fixed with an explicit failure branch. Final affected unit/Settings reruns
passed in all four configurations, including signed-zero formatting and checkbox keyboard order.
Release dependency/header inspection found only Windows imports, x64, CFG, ASLR, DEP,
high-entropy VA and an empty delay-import directory. No explicit dynamic loads were found.
The owner subsequently reported that the changes look good and requested release as 1.1.5.

Pending manual checks: open Settings and try each inline/popup combination with Apply;
Cancel/Close should discard unapplied changes. Confirm Show tooltips is below Show info on
hover, blends into the window background and remains reachable at narrow sizes and higher
DPI/text scale. Hover RAM, GPU, each disk and network: expect compact units, zero/tiny-positive
distinction, current device/scope/status and no raw capacity/peak byte counts. With inline
numbers off and tooltips on, check the 800 ms popup delay, sample changes and retained-age
refresh while stationary. With tooltips off, confirm the tray tooltip is still available.
Restart to verify persistence. The tests did not launch an AppBar, write real settings or
alter the desktop; live behavior and fresh production performance remain unverified.

## Settings checkbox backgrounds and Close controls (1.1.4)

On 2026-10-09, the final 1.1.4 builds passed all four CTest suites in Debug (8.76 s),
Release (5.25 s), RelWithDebInfo (3.28 s) and ASan (15.08 s). The hidden Settings tests
paint each preference checkbox into a DIB using `WM_PRINTCLIENT`, verify that empty pixels
match `COLOR_WINDOW`, and repeat for checked, unchecked, disabled and theme-reset states.
No screenshot, AppBar, collector or registry write is needed for these tests.

Footer tests cover separate left/right action groups, non-overlap, narrow/scrolling layouts,
DPI/text scaling, native forward/reverse Tab order, and conditional Retry. Close discards the
draft without setting application shutdown or destroying the bar, both with and without a
tray icon and from a reparented scrolling footer. Exit Loadbar retains its full label.

Commands run from the pinned developer environment:

```powershell
. ./scripts/enter-dev-shell.ps1
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug --output-on-failure
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
ctest --preset windows-x64-release --output-on-failure
cmake --build --preset windows-x64-debug --target format-check tidy analyze
cmake --build out/build/windows-x64-debug/analyze
cmake --build --preset windows-x64-debug --target format-check analyze
git -c core.safecrlf=false diff --check
```

MSVC analysis initially could not prove the test helper's bitmap handle was non-null after
its generic test assertion. An explicit failure branch resolved the diagnostic; the analysis
rebuild passed without suppression. Full clang-tidy completed without actionable diagnostics.
The final format/analysis rerun and diff check passed. The production change adds no drawing
resources or timers. Final publication checks also ran:

```powershell
. ./scripts/enter-dev-shell.ps1
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug --output-on-failure
pwsh -NoProfile -File scripts/package-release.ps1
foreach ($preset in @('windows-x64-relwithdebinfo', 'windows-x64-asan')) {
    cmake --preset $preset
    cmake --build --preset $preset
    ctest --preset $preset --output-on-failure
}
cmake --build --preset windows-x64-debug --target format-check analyze
clang-tidy -p out/build/windows-x64-debug/tidy-commands src/app/settings_ui.cpp src/app/application.cpp src/model/settings_form.cpp tests/settings_window.cpp
dumpbin /dependents out/release/1.1.4/Loadbar.exe
dumpbin /headers out/release/1.1.4/Loadbar.exe
rg -n 'LoadLibrary|LoadPackagedLibrary|GetProcAddress|DELAYLOAD' src CMakeLists.txt cmake
git -c core.safecrlf=false diff --check
```

All builds/tests, final format/MSVC analysis and targeted clang-tidy passed. The source search
found no explicit dynamic/delayed loads. Packaging results are in [packaging.md](packaging.md).
Independent adversarial and code-level performance reviews found no actionable issues.
Live high-contrast/DPI interaction and production performance measurements were not rerun.

Pending manual check: open Settings, confirm that all three preference checkboxes blend
into the surrounding background, and verify the footer at normal and narrow sizes. Change
a setting, choose Close, reopen Settings and confirm the unapplied change was discarded while
monitoring continued. Exit Loadbar should stop monitoring normally. Repeat with available
high-contrast, DPI and text-scale settings. No live desktop interaction was performed here.

## 1.1.3 publication build

On 2026-10-08, `pwsh -NoProfile -File scripts/package-release.ps1` built version 1.1.3,
passed all four Release CTest suites (2.84 s), and verified the single-entry ZIP against
the tested executable. Debug, RelWithDebInfo and ASan were reconfigured and rebuilt with
the release version and passed 4/4 suites each (8.05 s, 2.92 s and 14.10 s respectively).

Additional commands run:

```powershell
. ./scripts/enter-dev-shell.ps1
foreach ($preset in @('windows-x64-debug', 'windows-x64-relwithdebinfo', 'windows-x64-asan')) {
    cmake --preset $preset
    cmake --build --preset $preset
    ctest --preset $preset --output-on-failure
}
cmake --build --preset windows-x64-debug --target format-check analyze
dumpbin /dependents out/release/1.1.3/Loadbar.exe
dumpbin /headers out/release/1.1.3/Loadbar.exe
rg -n 'LoadLibrary|LoadPackagedLibrary|GetProcAddress|DELAYLOAD' src CMakeLists.txt cmake
git -c core.safecrlf=false diff --check
```

Formatting and MSVC analysis passed. The implementation's full/targeted clang-tidy evidence
is recorded below; publication changed version metadata and documentation only. Import/PE
inspection and package hashes are in [packaging.md](packaging.md). A fresh independent
adversarial review and final package review were clean. The owner reports successful manual
use; the detailed interactive matrix, hardware accuracy, performance/soak and clean-machine
packaging checks remain pending and are disclosed in the release notes.

## Edge-aware vertical layout (1.1.3)

On 2026-10-08, Debug, Release, RelWithDebInfo and ASan builds and all four CTest suites passed
with the pinned toolchain. Geometry tests cover all four edges, 128 logical processors
across groups, both shape settings, 40–640-DIP requests and text scales 1/1.5/2.25.
They assert complete readable tiles, containment, squares, P/E sides, strip-thickness fitting
and equal remaining device allocations. A review-found 128-thread uniform/unknown-class
case reproduced unused cross-strip space; the fix fits occupied rows and its regression
passes on all four edges. A strict equality assertion was corrected after
observing that square mode can enlarge tiles further than rectangle mode at 120 DIPs;
the regression requires that square mode never shrinks their cross-strip dimension.

Offscreen tests check Left/Right cache switches against a fresh renderer, text rotation,
hover suppression, DPI/text-scale transitions and restoration after injected text failure.
A high-contrast CPU overlay comparison verifies opposite quarter-turns at pixel level.
New 60 × 1400-pixel fixtures were visually inspected on both edges:
`render-fixtures/{left,right}-60-{squares,rectangles}-{normal,hover}.png` under each build.
These use synthetic readings through the production renderer, not hardware observations.

Commands for this update (from the x64 developer environment):

```powershell
. ./scripts/enter-dev-shell.ps1
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug --output-on-failure
foreach ($preset in @('windows-x64-relwithdebinfo', 'windows-x64-asan')) {
    cmake --preset $preset
    cmake --build --preset $preset
    ctest --preset $preset --output-on-failure
}
cmake --build --preset windows-x64-debug --target loadbar_readme_frames format-check tidy analyze
# After the review-found uniform-grid fix, rebuild/test the three configurations above, then:
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
ctest --preset windows-x64-release --output-on-failure
cmake --build --preset windows-x64-debug --target loadbar_readme_frames format-check analyze
clang-tidy -p out/build/windows-x64-debug/tidy-commands src/model/layout.cpp tests/design.cpp
dumpbin /dependents out/build/windows-x64-release/Loadbar.exe
git -c core.safecrlf=false diff --check
```

Final CTest results were 4/4 in Debug (8.59 s), Release (3.00 s), RelWithDebInfo
(3.06 s) and ASan (14.15 s). Format-check, MSVC analysis, the optional README-frame
helper build and the final targeted clang-tidy rerun passed. Release dependency inspection
found only Windows components, with no VC++ runtime DLL import. Diff checking passed.

The complete clang-tidy pass succeeded before the uniform-grid follow-up. Its targeted rerun
then flagged an intentional integer row calculation embedded in a floating-point expression;
storing the row as an integer before conversion resolved it without suppression or behavior
change. Two independent consecutive reviews were clean after the fitting fix and both
confirmed that final expression adjustment.

Manual acceptance: use Right at 60 DIPs, toggle squares, hover each widget, then switch to
Left. Confirm top-down/right and bottom-up/left inline text, opposite P/E sides, squares
using the strip thickness, equal device allocations, upward meters and upright icons below.
Repeat at 120 DIPs and available mixed-DPI/text-scale settings. Check disabled hover, menu,
Settings and clean Exit/work-area restoration. Live AppBar, hardware accuracy, production
performance/soak and standalone packaging checks remain pending; no live bar was launched.

## GPU fallback, CPU squares and interaction preferences (1.1.3)

All four suites passed in Debug, Release, RelWithDebInfo and ASan on 2026-10-08 using the
pinned toolchain. New regression coverage includes source priority, engine-query failure
isolation, memory retention with original scope/capacity/age, adapter changes, schema-10
migration and malformed values, checkbox drafts/Cancel, a stubbed Task Manager launcher,
square P/E tiles, vertical 0/50/100% pixels, hover suppression and cached-layout transitions.
Tests use fake readings and offscreen/hidden windows; they do not validate hardware accuracy.

Commands run from the repository root:

```powershell
. ./scripts/enter-dev-shell.ps1
cmake --preset windows-x64-debug
cmake --preset windows-x64-release
cmake --preset windows-x64-relwithdebinfo
cmake --preset windows-x64-asan
foreach ($preset in @('windows-x64-debug', 'windows-x64-release', 'windows-x64-relwithdebinfo', 'windows-x64-asan')) {
    cmake --build --preset $preset
    ctest --preset $preset --output-on-failure
}
cmake --build --preset windows-x64-debug --target format-check tidy analyze
clang-tidy -p out/build/windows-x64-debug/tidy-commands tests/continuity.cpp
cmake --build --preset windows-x64-debug --target format-check analyze
dumpbin /dependents out/build/windows-x64-release/Loadbar.exe
git diff --check
```

The first tidy pass found two test-code issues (unchecked optional access and vector capacity);
both were corrected. A Debug configure overlapping the analysis target encountered a Ninja
metadata lock; a subsequent isolated configure succeeded. Format-check and MSVC analysis
passed. All project files were checked by clang-tidy; the last remaining optional-access
warning needed a named reference for the analyzer to prove its guard, and the final targeted
rerun above passed. No diagnostics were suppressed to obtain a pass. Release imports contain
only Windows components, with no VC++ runtime DLL dependency; this is not a clean-machine
packaging test. Two independent consecutive final adversarial reviews were clean; both
confirmed the final test-only guard adjustment.

CTest writes previews to `out/build/windows-x64-release/render-fixtures/`. New files use
`horizontal-60-` or `vertical-60-`, then `squares-`/`rectangles-` and
`vram.png`/`shared.png`/`engines-only.png`. They are 1400 × 60 or 60 × 1400 pixels at 96 DPI,
rendered through production code with synthetic readings and no hover text. The square-mode
horizontal/vertical previews were visually inspected. No live bar was launched or altered;
manual AppBar/DPI interaction, GPU accuracy, performance/soak and standalone packaging remain
pending. The existing running copy was left alone.

## 1.1.1 publication build

`pwsh -NoProfile -File scripts/package-release.ps1` configured and built the normal
Release preset with version 1.1.1; all four CTest suites passed in 3.20 s. The script
verified the single-entry ZIP against the tested executable. Artifact hashes and import/PE
inspection are recorded in [packaging.md](packaging.md). The production source is unchanged
from the reviewed and tested CPU-sizing update below; this pass updates version resources,
release documentation and media. No live AppBar was launched.

## Logical-processor/performance and CPU sizing update

The 2026-10-08 update adds one tile per logical processor, reusable GPU aggregation scratch,
explicit provider deactivation, bounded glow masks and NIC retry backoff. The subsequent
CPU sizing correction allocates width to the tile grid and shares the remainder among devices.
Its regression tests check four/eight logical tiles, spacing, thickness growth, hidden widgets
and alignment. The RAM ellipsis fixture uses an 800-DIP edge so its ordinary capacity fits
while oversized capacities still exercise truncation with full tooltip values. Regressions cover
independent SMT colors/status/retention/high contrast, reordered identities, full processor-group
visibility, scratch release versus ordinary reset, backoff deadlines/recovery, glow bounds,
cache reuse and native-resource failure recovery. The four-core/eight-thread preview is
`render-fixtures/uniform4-8threads-60-normal.png`; the four-thread version is
`render-fixtures/uniform4-60-normal.png`, both under the candidate build: synthetic readings,
1400 × 60 pixels at 96 DPI, produced by the normal renderer without an AppBar or hover text.

Final warning-clean incremental builds and CTest results after the CPU sizing correction:

| Configuration | Result | Duration |
| --- | --- | --- |
| Debug | 4/4 passed | 9.09 s |
| Release candidate | 4/4 passed | 4.52 s |
| RelWithDebInfo | 4/4 passed | 4.52 s |
| ASan | 4/4 passed; no reported sanitizer errors | 14.67 s |

Commands ran from the pinned developer environment. Configure was followed by build/test;
small subsequent fixes used incremental builds and repeated all four suites:

```powershell
. ./scripts/enter-dev-shell.ps1
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug --output-on-failure
cmake --preset windows-x64-relwithdebinfo
cmake --build --preset windows-x64-relwithdebinfo
ctest --preset windows-x64-relwithdebinfo --output-on-failure
cmake --preset windows-x64-asan
cmake --build --preset windows-x64-asan
ctest --preset windows-x64-asan --output-on-failure
cmake --preset windows-x64-release -B out/build/windows-x64-release-candidate
cmake --build out/build/windows-x64-release-candidate
ctest --test-dir out/build/windows-x64-release-candidate --output-on-failure
cmake --build --preset windows-x64-debug --target format-check
cmake --build --preset windows-x64-debug --target tidy
cmake --build --preset windows-x64-debug --target analyze
dumpbin /dependents out/build/windows-x64-release-candidate/Loadbar.exe
git diff --check
```

Format-check, MSVC analysis and `git diff --check` passed. The full clang-tidy run found
one implicit integer-to-float conversion in the new test assertion. After changing its
literals to `6.0F`/`48.0F`, the affected file passed the same configured checks:

```powershell
clang-tidy -p out/build/windows-x64-debug/tidy-commands tests/design.cpp
```

All four configurations were rebuilt and all suites passed again after that test-only fix;
the unchanged files had passed the full tidy run. Final repetitions used
`cmake --build out/build/<directory>` and
`ctest --test-dir out/build/<directory> --output-on-failure`, for `windows-x64-debug`,
`windows-x64-release-candidate`, `windows-x64-relwithdebinfo` and `windows-x64-asan`.

The isolated Release folder leaves the running published executable untouched. Dependency
inspection lists Windows components only, with no VC++ runtime DLL import; source inspection
found no explicit `LoadLibrary`, `GetProcAddress` or `/DELAYLOAD` path. This does not replace a
clean-machine packaging check. The candidate executable SHA-256 is
`133FD4466BDBEC0759D6F37EC703E020B73565CBEB9E6675E2DF7D85DBD3EDAA` (1,068,032 bytes).

Two consecutive independent reviews of the thread/performance changes were clean. Two further
independent CPU-sizing reviews and final documentation/fixture rechecks found no actionable
defects. Both reviewers ran the Debug model tests; no assertions were removed or relaxed.
No warnings or tests were disabled. No live AppBar was launched/stopped/reconfigured, and no
user registry settings were changed. Live SMT hardware rendering, enlarged-glow appearance,
CPU-to-RAM spacing on the live desktop, controlled process/GPU performance, one-hour soak
and clean-machine packaging remain pending.
The [performance evidence](performance.md) separates synthetic probe results from production
measurements; [review.md](review.md) records independent review coverage.

## Drive visibility update

The drive-checkbox implementation adds schema-9 exclusion persistence (including escaped IDs,
maximum bounds and migration), hidden-provider continuity and query lifecycle tests, hidden
freshness deadlines, dynamic disk-index/render-cache checks, and native checklist draft,
keyboard, focus, scroll, empty-inventory and device-refresh coverage. The final builds passed
all four suites: Debug 15.78 s, Release 3.95 s, RelWithDebInfo 3.95 s, ASan 30.93 s, with no
reported sanitizer errors. All presets were configured using the pinned toolchain; subsequent
fixes used incremental builds/tests. Format-check, full clang-tidy, MSVC analysis and
`git diff --check` passed. After fixing the first review finding, two independent consecutive
adversarial reviews were clean.

Analysis findings were fixed without weakening checks: the second large discovery fixture
moved to the heap; exclusion storage uses a sorted unique vector with allocation-free empty
state and nonthrowing moves, avoiding the MSVC tree container's allocating move constructor.
The first review found a meaningless checkbox could appear on the empty informational row;
the list is now disabled while empty and re-enabled on discovery, with native regression tests.
Live hot-plug, restart/persistence, Shell placement and accessibility acceptance remain pending.
No live AppBar was launched and no user registry settings or Startup executable were changed.

## Earlier 1.1.0 GPU/network and CPU verification

On 2026-10-08, the unchanged pinned toolchain built Debug, Release, RelWithDebInfo and ASan.
All four CTest suites passed in each configuration; ASan reported no memory errors.
Final suite times were 15.76 s (Debug), 4.72 s (Release), 4.68 s (RelWithDebInfo), and
29.88 s (ASan). `format-check`, full `tidy`, MSVC `analyze`, and `git diff --check` passed.
Tidy findings in new test code (unsigned literals, reserved capacity, widened multiplication)
were corrected before the final passing run. Two independent consecutive adversarial reviews
were clean; scope and additional probes are recorded in [review.md](review.md).
The configure/build/test commands are the four-preset loop below. No Loadbar instance was
launched, replaced in Startup, or stopped; tests used hidden windows and offscreen rendering.

Added regressions cover all GPU/network visibility combinations, schema-8 persistence and
older-settings migration, hidden/disconnected draft choices, selective provider reset and
last-value/peak retention, hidden freshness deadlines, uniform SMT/grouped CPU squares,
DPI/text scaling, and cached-versus-fresh rendering after visibility changes.

The requested four-core previews are `render-fixtures/uniform4-60-normal.png` and
`render-fixtures/uniform4-60-hidden.png` under each build preset. They use the production
renderer with synthetic readings at 1400 × 60 pixels, 96 DPI, no hover text.

## Recorded 1.0.0 validation

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
| Toggle CPU squares, hover info and click action; Cancel, Apply, restart | Defaults off/on/on; draft and persistence work; squares retain P/E sizes; disabled hover has no overlays/bar tooltip; disabled click does nothing; tray/Settings/Exit remain usable |
| Left/right edges at 40/60/120 DIPs | Every thread remains visible; icons below graphics; all meters fill upward; non-CPU widgets share remaining height |
| Dedicated memory absent, shared available; both fail after a valid observation | Shared label/capacity used; dual failure retains last observation with original age; never-observed memory uses two-engine layout |
| Maximize/Snap on three displays with taskbar/another AppBar | Only the selected monitor reserves the negotiated strip; no drift/overlap |
| In Drives, uncheck one, Cancel, uncheck again and Apply; hide all, then show one | Checkbox draft/actions work; remaining widgets expand in both orientations; shared counters continue until all are hidden, then prime on showing |
| With Settings open, connect a new physical disk; disconnect/reconnect an excluded disk; restart | New identity is checked; excluded identity stays unchecked; catalog refresh preserves draft/focus/scroll; saved exclusions survive restart |
| With no discovered disks, use Tab/Space and reconnect a disk | Informational row has no checkbox and is skipped by Tab; arrival re-enables checked rows |
| Hide GPU, Network, then both; show each again and restart | Removed widgets free space; choices persist; hidden collection pauses; only shown family re-primes, preserving session peaks |
| Uniform 4/8/many-core CPU, both orientations and text scaling | Equal square cells, single row when possible, wrapping with every core present |
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
