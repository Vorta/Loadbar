# Loadbar 1.2.0 performance and memory review

Reviewed the unpublished worktree on 2026-10-09, based on commit
`a537a139539af214a51bdb322b0bdfc291b3da17`. An independent subagent reviewed collection,
temperature providers, snapshots, rendering, caches and resource lifetime. The primary agent
corroborated findings and passively observed the already-running Release application.
Application code, settings and desktop placement were not changed by this review.

## Live observation

The running `D:\Startup\Loadbar.exe` matched the current Release build by SHA-256:
`A812BA480BDAFCF7E6DDBA00CEBBC0FCCB5B4D0D82F9CF4BE5B2E6D50F3AB665`.
After a 30-second observer warmup, 121 query-only samples covered 601.94 seconds,
17:11:43–17:21:45 UTC. There was no debugger, application instrumentation, generated workload,
AppBar launch, reconfiguration or concurrent review build/probe. Ordinary desktop activity
was uncontrolled. Five-second sampling can miss short peaks.

Start/end settings and geometry matched: 250 ms sampling, Top, 50 DIPs, GPU/network visible,
no hidden disks, and a 2160 × 75 physical-pixel bar at 144 DPI. Settings was not open at either
endpoint. A per-monitor-aware observer avoided DPI-virtualized geometry. Visibility preferences
do not prove that every provider returned valid readings; hardware accuracy was not checked.

Host: Windows 11 build 26300.9550, Intel Core Ultra 9 275HX, 24 logical processors.
The process included NVIDIA graphics driver `nvwgf2umx.dll` 32.0.16.1742, Direct2D,
DirectWrite, D3D11, DXGI and DXCore. A module snapshot contained no NVAPI/ADLX/IGCL DLL;
this is not a continuous module-load trace or memory attribution. Toolchain is unchanged
from [toolchain.md](toolchain.md).

| Measure | Observed result |
| --- | --- |
| Total process CPU | 8,656.25 ms / 601.94 s = **14.38 ms/s** |
| One-core-equivalent CPU | **1.438%** |
| Whole-machine-normalized CPU | **0.0599%** across 24 LPs |
| Private bytes | Median **67.31 MiB**; range **65.89–67.39 MiB** |
| Working set | Median **59.90 MiB**; range **58.94–60.38 MiB** |
| Memory endpoint changes | Private +1.39 MiB; working set +1.34 MiB |
| Handles | 523–533; start 531, end 525 |
| Threads | 31–34; start 33, end 32 |
| GDI / USER objects | 12–13 / 15–16; end 12 / 15 |
| Process read/write I/O | Zero additional operations and bytes in both categories |
| Other process I/O | 12,728 operations / 11,018,868 bytes |

Private memory increased in steps during the first five minutes; its final-five-minute
endpoint change was −4 KiB. This does not demonstrate a sustained leak, a leak-free lifetime,
or the cause of the increase. Private bytes are committed private memory; working set measures
resident pages, including shareable pages. Neither identifies the component responsible.
See Microsoft's [memory-counter definitions](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex).

The known UI thread consumed 3.43 ms/s. Another persistent thread consumed 10.36 ms/s,
but its role was not established by stacks or instrumentation. Profile it before attributing
cost to collection or a particular provider. The other I/O category cannot be classified as
disk writes or network traffic from these counters alone; see
[process I/O accounting](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprocessiocounters).

CPU and private-memory figures exceed the project's numeric 10 ms/s and 64 MiB targets.
This was **not the prescribed 1-Hz gate**: the existing 4-Hz configuration was preserved,
and there were no controlled workload, absent/present or repeated-run comparisons. Do not
extrapolate linearly to 1 Hz or compare this to the older 1.1.0 observation as an improvement
or regression measurement.

## Prioritized findings

### P2: Repeated vendor initialization for stable missing temperature capability

[VendorGpuTemperature::sample](../src/telemetry/vendor_temperature.cpp) destroys all vendor
state after any nonvalid result. If Windows temperature is also unavailable, the outer
retry in [TemperatureProviders](../src/telemetry/temperature.cpp) eventually repeats every
30 seconds. A stable absence of a GPU-domain temperature sensor therefore repeats DLL
load, initialization, enumeration, termination and unload.

Three fake negative-capability calls produced three loads, initializations, closes and frees
for each NVIDIA, AMD and Intel path; Intel's size/fill enumeration pair ran six times.
This confirms repeated work, not real CPU cost. The previous Intel first-bind wall-clock
measurement must not be converted into CPU savings, and this conditional path does not
explain the current host's measured resource use.

Keep prompt release of vendor state, but retain a small negative-capability result keyed
to the selected adapter/binding generation. Recheck on relevant device/reset events and a
longer bounded discovery deadline. Distinguish explicit sensor/API absence from transient
errors. Do not retain an initialized vendor library merely to remember that it is unavailable.

### P3: Hover transitions replace unchanged temperature text layouts

The positional cache in [renderer.cpp](../src/ui/renderer.cpp) interleaves always-visible
temperature text with optional hover overlays. Switching hover changes the meaning of cache
slots. With an unchanged 24-LP/two-drive synthetic snapshot and five temperatures, the
production renderer produced these counts:

| Frame | New text layouts | Temperature layouts among them |
| --- | ---: | ---: |
| First rest | 5 | 5 |
| Repeated rest | 0 | 0 |
| First hover | 21 | 4 |
| Repeated hover | 0 | 0 |
| Return to rest | 4 | 4 |
| Repeated rest | 0 | 0 |
| Return to hover | 4 | 1 |

Give temperatures stable per-block slots or a separate bounded cache. Preserve text/font,
DPI and geometry invalidation. This is a small interaction-cost improvement; unchanged
resting and unchanged hovered frames already reuse resources.

### P3: Hidden drives retain disposable temperature request buffers

In [TemperatureProviders::configure](../src/telemetry/temperature.cpp), a hidden request that
was not pending closes its handle/event but survives the erase predicate. Its 4-KiB buffer,
device metadata and reading remain allocated. Remove disabled, completed requests while
keeping canceled requests alive until completion. Last-valid display readings are already
owned separately by `DisplayContinuity`.

Two previously visible hidden drives would release about 8 KiB of buffers plus metadata.
This is a small, bounded cleanup, not a remedy for a roughly 67-MiB process footprint.

## Implemented follow-up

The three findings above are now addressed. Missing vendor capability has a five-minute
adapter-specific retry cache with reset/identity invalidation; initialized vendor resources
still close promptly. Temperature text has separate bounded per-component slots, and completed
disabled drive requests are erased immediately, including after cancellation drains.

The new Show temperatures preference also stops all temperature opens/reads/probing when
disabled, without resetting other counters or peaks. It suppresses thermal drawing and
temperature-driven age timers. Tests cover disabled cold start, pending cancellation,
rapid re-enabling, resource release, transient vendor recovery and hover-cache reuse.
The exact later build/review results are recorded in [testing.md](testing.md).

These are verified call-count, cache and ownership changes. Whole-process CPU/RAM savings
after the changes have not been measured; the live figures above belong to the earlier binary.

An independent follow-up probe in `out/v120-performance-one/` repeated the original fake
vendor and offscreen cases. Across three missing-capability calls, each vendor now performed
one load/initialize/close/free rather than three; Intel's size/fill enumeration count fell
from six to two. First rest created five thermal layouts. First hover created seventeen
overlay layouts and zero thermal layouts. Subsequent rest/hover transitions created zero
layouts. Provider/pause and all-edge cache/visibility regressions also passed. These counts
establish eliminated work, not a percentage reduction in whole-process resource use.

That review found a further lifecycle issue: rapid off/on changes could coalesce while the
worker was busy, losing the temperature reset. A dedicated temperature configuration generation
now preserves it through Collector without resetting other telemetry. Regression tests use a
real worker started paused, so configuration coalescing is exercised without hardware reads.

## What to preserve and what needs profiling

The review found no confirmed unbounded queue or resource leak. Preserve the joinable worker,
interruptible waits, latest-value handoff, persistent PDH queries, reusable GPU aggregation,
hidden-family deactivation and hidden-Settings formatting suppression. Sensor calls stay on
the worker with cadence/backoff controls. Failed vendor state is released before Windows
fallback takes over. These properties are useful; they are not a one-hour lifetime pass.

Glow bitmap pixel payload is effectively capped at **8 MiB** by 32 entries × 65,536 pixels ×
4 bytes, stricter than the separate 16-MiB budget. Construction scratch is at most 768 KiB
plus a kernel; COM/driver overhead is additional. The 32-MiB PDH raw-array request check is
also not a process-memory limit: vector capacity, expanded records and instance maps are
additional. Retention is bounded and stores no sample history. Limit declarations do not
mean those capacities are preallocated.

Fresh snapshot storage is a profiling candidate, not a prioritized rewrite. Warmed
`CpuSamples::apply` with 24 LPs requested 5,031 bytes in 25 allocations for fresh output,
versus zero allocations with reused output. This synthetic count is allocation traffic,
not retained RAM or measured CPU time. Recycling snapshots would add ownership complexity;
first establish that this cost matters. The fixed 173,204-byte font copy likewise does not
justify custom COM ownership solely for a small one-time saving.

Inline Release sizes were Metric 152, Snapshot 2,256, Processor 176, DiskReading 736 and
TemperatureReading 216 bytes. These exclude owned heap storage, COM and driver allocations.
Do not sum them to explain private bytes or trim useful caches merely to lower a memory number.
Larger savings need stack/allocation attribution and a controlled comparison.

## Verification and remaining checks

These commands completed successfully; the fake/offscreen probe ran after passive observation:

```powershell
pwsh -NoProfile -File out/measure-loadbar-1.2.0.ps1 -TargetProcessId 6780 -DurationSeconds 600 -WarmupSeconds 30
python out/summarize-performance-1.2.0.py
./out/run-review-performance-ram.ps1
./out/review-performance-ram.exe | Tee-Object -FilePath out/review-performance-ram-results.txt
git -c core.safecrlf=false diff --check
```

Ignored artifacts include `out/performance-1.2.0/{metadata,samples,summary}.json`, the two
observer/summary scripts, and `out/review-performance-ram.cpp`, its build script and results.
The build script records the exact `/O2 /MT /std:c++23preview /W4 /WX` command against current
Release libraries and embedded font resources. It made no live sensor calls or AppBar changes.

No application source changed during the original review; follow-up implementation and
verification are recorded separately in [testing.md](testing.md). Controlled 1-Hz horizontal/vertical measurements,
GPU/DWM attribution, paint counts, workload/failure coverage, driver cancellation latency,
one-hour reconfiguration soak and clean-machine packaging remain pending.
