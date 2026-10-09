# Performance

**The final 1.3.0 Release executable has a fresh ten-minute passive observation and an
independent performance review in [the 1.3.0 evidence](performance-review-1.3.0.md).** At the
existing 250 ms cadence with readouts off, it measured **12.51 ms/s CPU**, **67.09 MiB median
private bytes** and **72.88 MiB median working set**. Memory and resource counts were stable
over the interval. CPU/private memory exceeded the provisional targets in this configuration;
the controlled 1-Hz gate, readout-on measurements and one-hour soak remain pending.

## Historical 1.2.0 observation

**The 1.2.0 implementation has an independent review and passive ten-minute observation in
[the 1.2.0 review](performance-review-1.2.0.md).** At the existing 250 ms cadence,
it measured 14.38 ms/s process CPU, median 67.31 MiB private bytes and 59.90 MiB working set.
The report identified vendor capability retries, hover/text cache churn and small hidden-drive
scratch retention. All three improvements are implemented with deterministic regression tests;
the earlier live observation does not measure the resulting savings.

The **Show temperatures** switch stops temperature opens/reads and vendor probing while
disabled, releases idle sensor resources, and excludes temperatures from paint/deadline work.
Pending canceled requests retain their buffers until completion. Temperature text uses its
own bounded component slots, so hover transitions reuse it. Confirmed missing vendor capability
is remembered without retaining initialized driver libraries; transient failures remain retryable.

The [1.1.0 review](performance-review-1.1.0.md) remains historical evidence, not a controlled
comparison. Both observations used existing 250 ms horizontal configurations; the prescribed
1 Hz performance gate remains pending. Automated test durations and offscreen probes do not
establish production overhead. See
[AGENTS.md](../AGENTS.md#performance-gate) for the targets and required conditions.

## Always-visible readouts (1.3.0)

An independent performance subagent reviewed rendering/layout caches, sampling/handoff,
hidden-provider handling, retention and resource cleanup on 2026-10-09. It found no actionable
performance or resource-usage issue, so no speculative optimization was made. It ran:

```powershell
./out/build/windows-x64-release/loadbar_render_tests.exe ./out/performance-review-v130-render
```

The command exited 0: 24 continuity/resize cases, 49 scaling cases and 65 offscreen fixtures
passed, including readout cache reuse, changed-slot replacement and resource reconstruction.
This is source review and regression evidence, not a production CPU/RAM measurement. Controlled
Release measurements and the one-hour lifetime soak remain pending for 1.3.0.

Readouts are presentation-only: toggling them does not reconfigure collectors or reset rate
baselines/peaks. They use three bounded text-cache slots per visible component, independent
of temperature and hover caches. Hover entry/exit skips bar invalidation while readouts are
on. Layout dimensions do not depend on live numeric text.

Component-specific readout widths use bounded prefix sums during layout, so wrapping does
not repeatedly measure text. Font baselines are obtained only when a cached text layout is
created; unchanged paints reuse them. The spacing/typography correction adds no sampling or timers.

Production-renderer offscreen tests verify zero new text layouts on unchanged rest/hover
transitions and exactly one new layout when only the GPU memory amount changes. Formatting
still runs for components during a paint; the cached DirectWrite layouts are reused. No
whole-process CPU/RAM saving or controlled production-overhead result is claimed. The
existing 1-Hz measurement, GPU/DWM comparison and one-hour soak gates remain pending.

## Temperature spacing update (1.2.0)

The reference-layout update adds one cached DirectWrite text format for temperature lines.
It is rebuilt on font-scale changes, with unchanged snapshots reusing layout, glyph and glow
resources. Temperature values remain integer-formatted and repaint only when their displayed
number, blended icon color, glow intensity or availability changes. Fractional changes in the
60–95°C ramps can change icon appearance without changing the number. DirectWrite ink offsets
are cached with each temperature text layout; fractional tint/glow updates reuse these layouts
and the existing glow masks. Offscreen tests count resource creation to verify this reuse.
Layout measurements and text hit bounds are
computed on reflow, not through provider calls in paint. No collector, timer, thread or
periodic I/O was added. Existing cache/recovery tests and the new repeated-reference renders
passed; this is code/test evidence, not a new production-overhead measurement.

## Independent tooltip controls (1.1.5)

The new tooltip preference is presentation-only and does not restart providers. Disabled
popups return before cursor queries/text formatting. Enabled popups retain the existing
observation/layout/deadline cache; popup age refresh no longer depends on inline numbers.
No new worker, timer, periodic I/O or sample history was introduced. Settings adds one native
checkbox owned by the existing parent window. These are inspected code properties; no new
production CPU/RAM/GPU or lifetime measurement was performed.

## 1.1.4 Settings review

A separate performance subagent found no actionable resource or hot-path regression on
2026-10-09. Checkbox painting uses a system-owned brush; the Close button is created once
with Settings and destroyed with its parent. Footer arithmetic uses fixed-size arrays and
bounded loops. Reparenting occurs only when layout requires it. Closing Settings stops its
freshness scheduling needs, and hidden/minimized Settings skips detail formatting. No new
sampling, rendering loop, timer, thread or periodic I/O was added. This was source inspection,
not a fresh CPU/RAM/GPU, paint-cadence or one-hour lifetime measurement; those gates remain pending.

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

## Temperature implementation (1.2.0)

Temperatures reuse the sampling worker and bounded snapshot handoff. GPU bindings/capabilities
and drive handles are cached. Queries run at most once per second, with failure backoff;
hidden devices pause and sleeping drives skip temperature I/O. Each drive owns one 4-KiB
request buffer, one event and one handle; pending/retiring requests are capped at 1,024.
Last-valid temperature retention is bounded to 4,096 device/component records. These upper
bounds are safety limits, not representative resource usage.

Geist Mono adds 173,204 embedded bytes plus its notice. DirectWrite copies the font into a
private collection once per renderer lifetime; text layouts use the existing cache. Temperature
presence changes rebuild layout, while whole-degree/color/glow changes invalidate only the
affected block. Sub-degree changes with identical presentation request no resting repaint.
There is no new Loadbar-owned thread, timer, sensor history, periodic file write or network request.

Fake-provider tests verify cadence, backoff, sleep/Hide and non-overlapping requests. Rendering
tests verify cache reuse and temperature-only repaint decisions. A standard-user read-only
probe confirmed provider availability on the host, not overhead or accuracy. Controlled
Release CPU/private-bytes/working-set/GPU measurements, sleeping-drive power behavior,
driver cancellation latency and the one-hour soak remain pending for 1.2.0.

The ASUS reader reuses one handle, event, 16-byte input and 16-byte response. GPU vendor
fallbacks initialize lazily, cache API tables/device/sensor bindings and stop on GPU Hide.
A working Windows source incurs no vendor initialization. Fake API tests verify binding
reuse, failure backoff and reference cleanup. Vendor libraries can create their own threads
and retain driver resources; those costs are not established by the bounded application state.
The read-only diagnostic reports first-bind duration; it does not measure steady-state overhead.
The final Release probe measured 13.48 ms for NVIDIA binding/first read and 259.79 ms for
Intel initialization/enumeration (which reported no sensors). These single observations were
made during development checks, not a controlled performance run. Debug or debugger-attached
timings are diagnostic evidence only, not Release overhead measurements.
