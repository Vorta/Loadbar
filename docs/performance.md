# Performance

**A passive ten-minute observation of Release 1.1.0 is recorded in the
[performance review](performance-review-1.1.0.md).** It used the existing 250 ms horizontal
configuration; the prescribed 1 Hz performance gate remains pending. Automated test durations
and offscreen probes do not establish production overhead. See
[AGENTS.md](../AGENTS.md#performance-gate) for the targets and required conditions.

## Implemented cost controls

- One sampling worker with persistent queries, reusable PDH buffers and interruptible waits.
- Bounded latest-value delivery, last-valid observations and session peaks; no sample history.
- Cached CPU lookup and GPU instance parsing, with fresh status/duplicate checks on each sample.
  The 8,192-name parsing-cache limit does not discard overflow samples.
- Reused immutable catalogs and unchanged Settings rows; hidden Settings skips sample updates.
- Non-owning presentation views and numeric render caches avoid copying diagnostic strings.
- Change-driven painting, cached tooltip text and deadline-based staleness checks.
- Reused drawing resources, with glow bitmap pixel payloads capped at 16 MiB and 32 entries.
  Driver/object overhead is additional. Each glow raster is capped at 65,536 pixels and
  1,024 pixels per side; its three temporary images total at most 768 KiB, plus a small kernel.

These are implementation properties, not quantified savings or a passed performance gate.
Full-frame drawing and periodic discovery remain deliberate correctness choices. Further
changes to discovery, drawing, glow generation or link-time optimization need profiling.

## Follow-up implementation

The logical-processor/performance update replaces per-sample GPU map/set allocation with a
worker-owned sortable reference workspace. A Release scratch probe of warmed production
`GpuSamples::read` plus both aggregations recorded **zero allocations/zero requested bytes**
for unchanged valid fixtures of 64, 512 and 4,096 selected rows. The corresponding baseline
counts were 104, 776 and 6,152 allocations. This establishes the tested allocation property,
not production CPU savings; changing instances, errors and anomalies can still allocate.

GPU Hide and all-hidden drives explicitly release disposable provider buffers. Ordinary
counter resets keep reusable capacity. The reset/deactivation tests verify this distinction;
last-valid observations and session peaks remain separately owned. NIC API failures now have
bounded exponential backoff. The previous review remains baseline evidence, not a description
of these fixed paths. Correctness and build results are in [testing.md](testing.md).

A seven-run alternating baseline/candidate comparison used the same synthetic 24-thread,
two-disk high-load fixture and software WIC renderer. Builds and analysis were finished before
this comparison. Median whole-frame times were:

| Pixels / DPI | Baseline cold | Candidate cold | Baseline cached | Candidate cached |
| --- | ---: | ---: | ---: | ---: |
| 1920 × 60 / 96 | 5.59 ms | 5.71 ms | 0.56 ms | 0.61 ms |
| 3840 × 640 / 96 | 25.54 ms | 19.87 ms | 1.07 ms | 1.03 ms |
| 3840 × 960 / 144 | 25.36 ms | 19.94 ms | 1.16 ms | 1.11 ms |
| 640 × 2160 / 96 | 52.26 ms | 21.42 ms | 1.46 ms | 1.43 ms |

Cold enlarged frames improved, most strongly in the vertical fixture; compact and cached
results are approximately unchanged, without a statistical significance claim. These timings
include layout/resources/drawing, not isolated blur cost, and do not measure HWND/GPU or
production process overhead. The bound reduces construction scratch, not all process memory.
Ignored local probes: `out/perf-telemetry-candidate.exe`, `out/perf-render-candidate.exe`,
`out/compare-glows.py`, with raw runs and medians in `out/glow-comparison.json`. Commands:

```powershell
./out/perf-telemetry-candidate.exe
./out/perf-render-candidate.exe
python out/compare-glows.py
```

## Measurement procedure

Use the Release x64 executable without a debugger or sanitizer. Record its hash/commit,
toolchain, Windows build, CPU/RAM/storage/NIC/GPU and drivers, display layout/DPI/text scale,
selected devices, cadence, workload and measurement method. Redact private identifiers.

For both representative horizontal and vertical layouts, enable every metric at 1 Hz, warm
up for 30 seconds, then record at least 10 minutes. Use external profiling such as WPR/WPA
or Performance Monitor. Measure an equivalent absent/present pair, repeat to characterize
noise, and disclose profiler overhead. Separate collector/render cost from additional DWM work.

| Measurement | Record |
| --- | --- |
| CPU | `1000 * delta(process CPU seconds) / elapsed seconds` in ms/s; also one-core-equivalent percent and that percent divided by LP count |
| Memory | Private bytes and working set separately; steady levels and peaks |
| GPU | Attributable engine activity and DWM cost, with method and noise |
| Paints | Telemetry-driven paints per sample, separate from expose/resize/user events |
| I/O | Application writes/network requests, separate from OS counter and driver activity |
| Lifetime | Memory, handles, GDI/USER objects, threads and snapshots over a one-hour soak |

With explicit authorization, cover idle, CPU/disk/network activity, 3D, decode, many GPU
processes, visible/hidden states, hover, enlarged layouts and high contrast. Include
DPI/device changes, suspend/resume and Retry in the soak. Do not omit metrics or slow sampling
to obtain a pass. Reconfiguration and first-use glow allocation deserve separate latency checks.

## Evidence status

The 1.1.0 observation measured 12.53 ms/s process CPU and median 66.95 MiB private bytes
at 250 ms sampling, with no sustained resource growth over that ten-minute interval.
The [review](performance-review-1.1.0.md) separates live observations from synthetic probes
and records the allocation, retained-memory, glow-generation and retry findings addressed
by the follow-up implementation above.

Controlled 1 Hz horizontal/vertical measurements, absent/present comparison, GPU/DWM cost,
paint counts, workload coverage and the one-hour soak remain **pending**. Neither source
review nor this single observation establishes an absolute optimum or a passed performance
gate. Build and regression evidence is in [testing.md](testing.md), and release requirements
in [packaging.md](packaging.md).

The subsequent CPU content-width correction changes allocation geometry, not sampling or
paint scheduling. The synthetic timings above predate that correction and are not a
measurement of the final layout. Final process/GPU overhead still requires the controlled
performance gate.

The unreleased GPU-fallback/vertical-layout update adds no worker or timer. Shared GPU memory
is queried only when the dedicated observation is not valid. Disabling hover skips overlay
text, tooltip updates and hover-only retention-age wakeups. Presentation toggles do not
restart providers. These are implementation properties, not measured overhead improvements;
the new layouts still need the controlled performance and soak checks above.

Edge-aware square fitting performs a bounded search only when layout inputs change. Edge
switches invalidate the existing layout key; unchanged frames reuse it. Rotated hover text
uses drawing transforms and the existing text cache, without new timers or worker work.
These changes have not been measured in a production performance run.
