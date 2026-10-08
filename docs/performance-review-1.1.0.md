# Loadbar 1.1.0 performance review

Reviewed commit `7726b81827817b8b388893f413b2c859157b82b5` on 2026-10-08.
This is an assessment of the published implementation, not an optimization patch.
Two independent reviewers covered collection/model and rendering/UI paths. Application source,
settings, the running instance and the desktop were not changed.

## Scope and evidence

Evidence consists of source and lifetime review, bounded synthetic Release probes, and passive
observation of the already-running, uninstrumented Release executable. The probe programs
finished before passive measurement started. No debugger, sanitizer, generated workload,
AppBar launch/reconfiguration, or system profiler recording was used.

The observed executable's SHA-256 matches the published 1.1.0 asset:
`B22AD2C4D7ED83B48F99AB02A2DBC310F0F2FC2AF59D10AB30B109A8BE008ECB`.
The toolchain remains the pinned set in [toolchain.md](toolchain.md).

## Passive live observation

The already-running Release process was observed for **602.32 seconds**, after an additional
30 seconds excluded from measurement. There were 121 observations approximately five seconds
apart, from 17:57:38 to 18:07:40 UTC on 2026-10-08. The process had already been running for
about ten minutes. Start/end settings matched: **250 ms sampling**, top edge, 60 DIPs,
GPU/network visible and no hidden disks. Settings existed but was hidden at both endpoints.
A separate per-monitor-aware query measured the bar at 2160 × 90 physical pixels / 144 DPI
(150%). The first observer's virtualized window dimensions must not be used as physical pixels.

| Measure | Observed result |
| --- | --- |
| Total process CPU | 7,546.875 ms over 602.32 s = **12.53 ms/s** |
| One-core-equivalent CPU | **1.253%** |
| Whole-machine-normalized CPU, 24 LPs | **0.0522%** |
| Private bytes | Median **66.95 MiB**; range 66.92–67.02 MiB; end minus start −8 KiB |
| Working set | Median **66.34 MiB**; range 66.30–66.36 MiB; end minus start +56 KiB |
| Handles | 550–558; start 554, end 552 |
| Threads | 31–34; start/end 32 |
| GDI objects | 58 throughout |
| USER objects | 63–64; start 64, end 63 |
| Process read/write I/O | Zero additional operations and bytes in both categories |
| Other process I/O | 10,789 operations / 10,751,320 bytes |

This run is above the project's numeric 10 ms/s CPU and 64 MiB private-memory targets, while
working set is below the 96 MiB aim. **It is not the prescribed 1 Hz performance gate:** the
user's configuration was left at 4 Hz, ordinary desktop activity was uncontrolled, and no
absent/present comparison or repeated run was performed. Do not extrapolate linearly to 1 Hz.
No sustained resource growth was apparent in these samples; that is not a one-hour leak test.
Five-second sampling can miss short peaks. Private bytes include in-process OS/driver costs
as well as application allocations, and private bytes and working set are different measures.

The known UI thread consumed 2,125 ms (**3.53 ms/s**). Another persistent thread consumed
5,156.25 ms (**8.56 ms/s**), but its role was not established by stacks or instrumentation.
This suggests profiling that thread next; it does not identify the collector as the cause.
Thread counters do not isolate renderer, provider or driver functions. OS/driver-created
threads contribute to the observed count; Loadbar owns one explicit sampling worker.
The process I/O categories are general accounting, not a disk/network trace. Zero writes
supports the absence of periodic writes during this interval; it does not prove every
possible path is free of I/O. Other I/O cannot be classified from these counters alone.

Environment: Windows 11 Pro 26H2 build 26300.9550; Intel Core Ultra 9 275HX, 24 physical/logical
processors; 68,044,156,928 bytes physical RAM reported. Installed graphics: Intel Graphics
32.0.101.8805 and NVIDIA GeForce RTX 5090 Laptop GPU 32.0.16.1742. Disks: NVMe HFS002TEJ9X101N
and Samsung SSD 990 PRO 4TB. An active physical interface was CalDigit Thunderbolt 10G Ethernet
(10 Gb/s). The selected adapter/interface mapping, metric validity, complete multi-monitor
arrangement and Windows text scale were not independently verified in this observation.
Visibility settings alone do not prove every provider returned valid data. No hardware
accuracy or GPU/DWM utilization result is claimed.

## Findings

### GPU aggregation allocates on every sample

`aggregate_engines` in [snapshot.cpp](../src/model/snapshot.cpp) creates fresh owning map/set
nodes and copies engine identity strings. The parsed-name cache avoids reparsing, but the two
engine-type aggregations still allocate in proportion to the selected process/engine rows.
A warmed call to `GpuSamples::read` followed by both production aggregations produced:

| Synthetic selected GPU rows | Allocations | Requested bytes |
| --- | ---: | ---: |
| 64 | 104 | 7,120 |
| 512 | 776 | 53,712 |
| 4,096 | 6,152 | 426,448 |

The fixture uses equal 3D/decode contributions, four engine keys, distinct process IDs and
matching timestamps. These are single-call allocation counts, not process CPU measurements
or evidence of the live machine's row count. Profile this path separately from PDH before
choosing reusable scratch storage or sorted numeric keys. Preserve all duplicate, interval,
status and engine-scope validation.

### Explicit Hide retains provider scratch memory

`CounterSource::reset` closes its PDH query but retains output arrays and their strings;
`PdhQuery::reset` retains raw buffer capacity. The hidden-GPU fast return also retains its
parsed-name and selected-sample caches. A deterministic probe confirmed that reset retained
all 64, 512 and 4,096 seeded output rows. Requested PDH formatted-array sizes are limited
to 32 MiB per source; retained vector capacity and copied representation storage are
additional. GPU parsed names are capped at 8,192.

This is bounded high-water retention, not an unbounded leak or ongoing hidden collection.
It is useful during active sampling and transient failures. A separate explicit-deactivation
path could free these scratch resources on Hide, while keeping last-valid observations and
session peaks in their existing owners. Do not shrink every tick or on every transient error.

### Cold glow generation has measurable latency and scratch-memory cost

[renderer.cpp](../src/ui/renderer.cpp) constructs glow masks synchronously on the UI drawing
path. Two float images, one pixel image and separable CPU convolutions scale with pixel area
and blur radius. A production-renderer probe using a synthetic 24-core/two-disk snapshot with
high readings, software WIC targets and one cold/cached frame per configuration reported:

| Pixels / DPI | Content scale | Cold frame | Cached frame |
| --- | ---: | ---: | ---: |
| 1920 × 60 / 96 | 1.50 | 5.47 ms | 0.60 ms |
| 3840 × 640 / 96 | 3.93 | 25.87 ms | 1.06 ms |
| 3840 × 960 / 144 | 2.62 | 25.42 ms | 1.00 ms |
| 640 × 2160 / 96 | 4.34 | 54.22 ms | 1.57 ms |

Each configuration created five glow bitmaps on the cold frame and none on the cached frame.
Cold timings include layout/resource creation, so they do not isolate blur time. Software WIC
is not the production HWND/GPU path; these results identify a cold-path opportunity, not steady
application overhead. First high-use readings, resize, DPI changes and target loss can trigger it.

The four-million-pixel guard permits 48,000,000 bytes (45.8 MiB) across the three temporary
images, before bitmap, cache, target and driver memory. That is a code-derived upper bound,
not an observed allocation at a supported layout. The separate 16 MiB bitmap cache budget
is not an overall renderer-memory budget. Evaluate bounded-resolution or nine-slice glow
construction, preserving appearance and resource ownership, and test scratch peaks as well
as latency.

### Network failure retries lack backoff

[network.cpp](../src/telemetry/network.cpp) calls its reader on every sample after API errors;
reset clears rate baselines but does not defer retries. Persistent failures are retried at
each sample, nominally up to four calls per second at 250 ms. This is a confirmed gap in the
bounded-backoff policy, though
no material CPU cost was established. Add a monotonic error retry deadline that explicit
selection/recovery events can reset, keeping first-success priming and display continuity.

## Supported strengths and lower-priority opportunities

The review found persistent queries, interruptible waits, one owned joinable sampling worker,
bounded latest-value delivery, capped retention and no growing sample history. There is no
busy wait, priority/timer-resolution escalation, per-sample process launch, telemetry transmission
or periodic application file-write path. Settings persistence happens on committed changes.

Geometry, brushes, icons, fonts, text layouts and glow resources are reused; hidden/minimized
Settings skips reading-string construction. Normal samples do not rebuild layout. Resource
ownership review found no confirmed unbounded GDI/USER/D2D leak. This does not establish a
one-hour lifetime pass.

Full-frame drawing, 30-second discovery, fresh snapshot allocations, visible-Settings formatting
and O(disks × counter rows) disk matching are secondary profiling candidates. They are bounded
or deliberate tradeoffs; do not complicate them without evidence of significant cost. In
particular, two-drive lookup is not a credible first optimization priority.

## Reproduction and limits

Local scratch artifacts are ignored under `out/`: `measure-loadbar.ps1`, `summarize-performance.py`,
`read-loadbar-geometry.ps1`, `perf-telemetry-probe.cpp/.exe`, `perf-render-probe.cpp/.exe`,
and `performance-1.1.0/{metadata,samples,summary}.json`. They are review artifacts rather
than production features or committed regression tests.

The passive measurement and result-summary commands completed successfully:

```powershell
pwsh -NoProfile -File out/measure-loadbar.ps1 -TargetProcessId 29252 -DurationSeconds 600 -WarmupSeconds 30
python out/summarize-performance.py
pwsh -NoProfile -File out/read-loadbar-geometry.ps1
```

The Release scratch executables `out/perf-telemetry-probe.exe` and
`out/perf-render-probe.exe` also completed successfully; results are above. No source changes
were made, so build/CTest/static-analysis suites were not rerun for this review.

The script opens the existing process with query-only access and samples cumulative process
and thread CPU time, private bytes, working set, handles, thread count, GDI/USER objects and
I/O counters every five seconds. The observer writes its local results, not Loadbar. It does
not restart or reconfigure the app. Hardware inventory was queried once; no private device
identities are included in this report. Thread time is attribution by thread, not function-level
profiling. Loaded graphics/driver modules are included in process totals; mapped image sizes
must not be added to private bytes or treated as resident memory.

API interpretation follows Microsoft's [GUI resource counts](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguiresources),
[process I/O accounting](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprocessiocounters),
and [memory counters](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex).
Window geometry was checked from a per-monitor-aware observer thread because
[GetWindowRect is DPI-virtualized](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect).

The separate release gate still requires 1 Hz horizontal and vertical runs, absent/present
comparisons, GPU/DWM attribution, paints per sample, controlled workloads, failure/recovery
and one-hour reconfiguration soak. Do not infer those results, a leak-free lifetime, a
1 Hz extrapolation or optimal code from this review.
