# Loadbar 1.3.0 performance evidence

Observed on 2026-10-09, using the final Release executable from the worktree based on
`d80aeb11d1883972d8c2705ed448e251ba21f9fd`. The running copy matched the packaged 1.3.0
executable by SHA-256:
`ddf03d8fcb1d3518df2cddef02bfe56aa319a925954e4c8e737456cac87fc443`.
The source is preserved by the `v1.3.0` release tag; the binary hash identifies this observation.

## Method and configuration

A read-only observer sampled the already-running process every five seconds after a
30-second observer warmup. Its 121 samples covered **601.91 seconds**, 20:37:58–20:48:00 UTC.
There was no AppBar launch, settings change, generated workload, debugger or application
instrumentation. Builds and tests ran after the interval. Ordinary desktop activity was
uncontrolled; five-second polling can miss brief peaks and the observer's overhead was not
separately measured.

Settings and window geometry matched at both endpoints: **250 ms sampling**, Top, 60 DIPs,
CPU squares and temperatures enabled, GPU/network visible, no hidden drives, hover info
enabled, tooltips disabled, and **Always show readout off**. The bar was 2160 × 90 physical
pixels at 144 DPI. The Settings window existed but was hidden at both endpoints. These
preferences do not prove that every provider returned valid observations.

Host: Windows 11 26H2 build 26300.9550, Intel Core Ultra 9 275HX, 24 logical processors.
A module snapshot included NVIDIA `nvwgf2umx.dll` 32.0.16.1742, Direct2D, DirectWrite, D3D11,
DXGI and DXCore; no NVAPI/ADLX/IGCL library appeared in that snapshot. This is not continuous
module tracing or a complete device inventory. Toolchain: [toolchain.md](toolchain.md).

The observer used process CPU time, private bytes, working set, handles and thread counts,
plus `GetGuiResources` and `GetProcessIoCounters`. A per-monitor-aware observer queried
window dimensions without moving windows. Local raw metadata, samples and summary remain
under ignored `out/performance-1.3.0/`; the report contains no private device identifiers.
Exact observation and summarization commands:

```powershell
pwsh -NoProfile -File out/measure-loadbar-1.3.0.ps1 -TargetProcessId 37704 -WarmupSeconds 30 -DurationSeconds 600
python out/summarize-performance-1.3.0.py
```

## Results

| Measure | Observed result |
| --- | --- |
| Total process CPU | 7,531.25 ms / 601.91 s = **12.51 ms/s** |
| One-core-equivalent CPU | **1.251%** |
| Whole-machine-normalized CPU | **0.0521%** across 24 logical processors |
| Private bytes | Median **67.09 MiB**; range **67.05–67.09 MiB** |
| Working set | Median **72.88 MiB**; range **72.84–72.96 MiB** |
| Memory endpoint changes | Private **0 MiB**; working set **−0.086 MiB** |
| Handles | 627–630; start/end 629 |
| Threads | 31–32; start/end 32 |
| GDI / USER objects | Constant 55 / 69 |
| Process read/write I/O | Zero additional operations and bytes in both categories |
| Other process I/O | 12,696 operations / 10,986,280 bytes |

CPU is `delta CPU milliseconds / elapsed seconds`; divide by 10 for one-core percent,
then by 24 for whole-machine percent. Private bytes and working set are distinct measures.
No sustained memory or resource-count growth was observed during this interval; that is
not a one-hour leak/lifetime result. Other I/O cannot be classified as disk writes or
network traffic from these counters alone.

**CPU and private memory exceeded the provisional 10 ms/s and 64 MiB targets.** This was
the existing 4-Hz configuration, not the prescribed controlled 1-Hz gate. No pass is claimed,
no linear extrapolation to 1 Hz is made, and older observations are not controlled comparisons.
Readout-on overhead was not measured. GPU/DWM attribution, paints per sample, collector/render
cost separation, vertical layouts, workload coverage, absent/present comparisons, hardware
accuracy and the one-hour soak remain pending.

## Independent review

The performance subagent found no actionable issue in bounded text/layout/glow caches,
sampling and coalesced delivery, hidden-provider handling or resource cleanup. It ran:

```powershell
./out/build/windows-x64-release/loadbar_render_tests.exe ./out/performance-review-v130-render
```

Exit 0: 24 continuity/resize cases, 49 scaling cases and 65 offscreen fixtures passed,
including readout cache reuse and resource reconstruction. This establishes tested code
properties, not production performance. No speculative runtime optimization was made.
