# Performance

**Production overhead has not been measured.** Automated test durations, source review and
offscreen rendering do not establish CPU, memory or GPU usage. The performance targets in
[AGENTS.md](../AGENTS.md#performance-gate) remain pending.

## Implemented cost controls

- One sampling worker with persistent queries, reusable PDH buffers and interruptible waits.
- Bounded latest-value delivery, last-valid observations and session peaks; no sample history.
- Cached CPU lookup and GPU instance parsing, with fresh status/duplicate checks on each sample.
  The 8,192-name parsing-cache limit does not discard overflow samples.
- Reused immutable catalogs and unchanged Settings rows; hidden Settings skips sample updates.
- Non-owning presentation views and numeric render caches avoid copying diagnostic strings.
- Change-driven painting, cached tooltip text and deadline-based staleness checks.
- Reused drawing resources, with glow bitmap pixel payloads capped at 16 MiB and 32 entries.
  Driver/object overhead is additional. Cold glow construction can use larger temporary buffers.

These are implementation properties, not quantified savings or a passed performance gate.
Full-frame drawing and periodic discovery remain deliberate correctness choices. Further
changes to discovery, drawing, glow generation or link-time optimization need profiling.

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

Ten-minute measurements, absent/present comparison, target evaluation and the one-hour soak
are all **pending**. Static reviews removed redundant allocations and event-path work;
they did not establish an absolute optimum. Build and regression evidence is in
[testing.md](testing.md), and release requirements in [packaging.md](packaging.md).
