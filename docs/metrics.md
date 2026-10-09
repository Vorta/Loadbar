# Sources and interpretation

All normal execution uses the real providers below. Synthetic values are limited to explicit
tests and the optional README fixture.
Discovery and collection run on the worker, never in paint. Device metadata refreshes every
30 seconds and on reconfiguration/device/resume events. Queries remain open across ordinary
samples. A changed device/topology or resume resets the applicable baselines.

Each value carries identity, unit, monotonic timestamp, interval and status. Valid zero is distinct
from warming-up, stale, unavailable and error. A valid sample becomes stale after the greater of
three configured intervals or three seconds. Raw missing observations are never valid zeros. Native collection
failures retain operation/code in details; provider failures do not suppress unrelated families.
Fatal worker failure exposes Retry. Disk discovery failures retain the last completed inventory;
other never-discovered families can remain empty until rediscovery.

## Display continuity

`DisplayContinuity` is owned by Application and exclusively accessed by the joined sampling
worker after provider interpretation and peak updates, before coalesced publication. `Metric`
keeps its raw value/status/timestamp and carries a separate optional last valid observation.
Rendering obtains a disposable presentation view; no fallback is supplied to providers or peaks.
Initial warm-up displays zero. Later warm-up, stale, unavailable and error states retain the
latest valid value without decay. Never-valid errors/unavailability remain explicitly missing.
Tooltips and accessible details identify retained observations and their original age.

CPU keys use processor group/number; other keys combine stable identity and metric direction.
RAM and GPU memory retain percent, bytes, capacity and timestamp atomically, with dedicated/shared
scope separated. Selected GPU/NIC metadata survives temporary disappearance, Retry, and choosing
A again after B; a never-observed C cannot borrow another device's data. Discovered disks still
follow the catalog's visibility policy; returning identities recover their cached observations.

A fatal worker delivery marks the newest pending snapshot failed instead of replacing it with
an empty observation. Both ordinary and fallback failure notifications preserve the latest
completed data/topology/catalog. When no delivery is pending, the UI retains its current snapshot.

Storage has at most 69,632 scalar streams, 4,096 coherent memory scopes, and 4,096 known selection
records for each GPU/NIC family. One value/timestamp (and rate scale metadata) is retained per
key, never a timeline. New scalar/memory records beyond the bound report retention exhaustion;
current valid data remains displayable. Values and metadata are discarded on process exit.

## CPU and RAM

CPU uses English `\Processor Information(*)\% Processor Time` through PDH. Group and global
`_Total` instances are rejected by the strict `group,number` parser. Topology comes from
`GetLogicalProcessorInformationEx(RelationProcessorCore)` with every group mask enumerated.
Core groups are ordered by their first logical processor; sibling readings are never summed. Unknown
topology is labeled unmapped, with exact group/processor identification in details. Heterogeneous
and more-than-64-processor fixtures are covered by deterministic tests. Hardware topology beyond
this host remains untested.

CPU topology discovery now returns errors separately from completed topology. A failed scan keeps
its last successful mapped metadata, and the Settings discovery diagnostic preserves native failure
operation/code. PDH can still identify logical processors by group/number when mapping is absent.
`CpuSamples` is application-owned across joined-worker Retry and retains their identity metadata
through missing/error arrays, capped at 65,536 known processors. It retains no metric history;
DisplayContinuity supplies last-valid readings and age. A completed topology scan authorizes pruning
counter-only/removed identities. Counter errors cannot remove previously visible cells. Native
malformed/truncated/empty topology buffers are errors, not evidence of zero processors.

Presentation additionally groups cores using the topology API's EfficiencyClass. The highest
class is the performance group; lower classes retain their class identities in details.
Each logical processor has its own solid tile, colored by its own displayed utilization.
Each thread uses its retained observation or initial warm-up zero independently; a never-valid
unavailable/error thread affects only its own tile. Tooltips identify the physical core and
processor group/number. Uniform four-core/eight-thread CPUs display eight equal squares.
Unknown/uniform topology is neutral. The P/E hover summaries average logical processors within
each group; the overall summary averages all logical processors. Incomplete observations produce
an explicit incomplete summary when a sibling has neither a current nor retained/displayable value.

RAM calls `GlobalMemoryStatusEx`: `100 * (ullTotalPhys - ullAvailPhys) / ullTotalPhys`, in floating
point after denominator/range checks. The same observation retains used physical bytes and total
physical bytes. Hover and Settings show e.g. `47.0/63.4GB 74%` (percentage is calculated before
rounding); GB here follows Windows' binary convention, 1,073,741,824 bytes, explicitly labeled in
the tooltip. Capacity is one decimal and percentage an integer in hover. Errors, stale data,
resume and worker failures preserve the last coherent percentage/bytes/capacity observation for
display, with raw failure status and observation age in details. Commit/pagefile figures are not used.

## PDH lifecycle

Persistent wildcard handles are added with `PdhAddEnglishCounterW`; `PdhGetCounterInfoW` provides
the localized full path for explicit wildcard expansion during setup. Formatted sampling uses
`PdhGetFormattedCounterArrayW`, `PDH_FMT_DOUBLE | PDH_FMT_NOCAP100`, call status and every item's
CStatus. Only valid/new statuses are accepted. Names and values are copied before buffer reuse.
Size races re-query from a zero size, with three attempts and a 32 MiB ceiling. Newly observed or
reconnected instances warm up; disappearing names lose their baseline. Initial invalid rate data
retains warming-up status; presentation alone shows initial zero or the last valid observation.
Formatted PDH rates are never divided by elapsed time again.
Collection/add failures retry with exponential backoff capped at 30 seconds. There are no per-tick
process launches or query reconstruction. Localized Windows behavior remains a manual gate.

## Disk

SetupAPI disk interfaces supply persistent device-instance identities; storage device-number
IOCTLs resolve the current `PhysicalDisk` instance number. All discovered physical disks are
shown individually, in current physical disk-number order, with stable device identities on
both directions. Aliases with the same device identity/number are deduplicated. No capacity
scan, benchmark, media read or SSD assumption is made.

Disk discovery is transactional. Class-list failure, unexpected enumeration termination,
identity-critical per-device failure, conflicting identity/number mappings or exhausting the
4,096-interface bound reject the incomplete scan. Only ERROR_NO_MORE_ITEMS completes a scan;
an exact duplicate mapping is deduplicated. Optional friendly-name/system-disk-preference failure
uses a label/preference fallback. Failed discovery preserves operation and native code.

Application owns the last successful `DiskInventory`, accessed exclusively by the joined worker,
so it also survives worker Retry. Failure preserves all known widgets but publishes raw error
metrics and skips both disk counter queries: old PhysicalDisk numbers cannot be trusted after a
failed scan. Display continuity retains values, original observation ages and peaks. A never-seen
inventory exposes a discovery error in its placeholder, tooltip and Current readings; even native
ERROR_NOT_READY remains error, not warm-up zero. The normal 30-second rediscovery retries, plus
existing device/reconfiguration/resume events. Successful rediscovery re-primes both queries,
including when identities/numbers are unchanged. A completed empty scan clears the inventory and
removes former widgets; a later failure does not resurrect confirmed-removed devices.

The native failure/completion distinction follows
[SetupDiGetClassDevsW](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdigetclassdevsw)
and [SetupDiEnumDeviceInterfaces](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdienumdeviceinterfaces).
Their declarations and error constants were checked against the pinned SDK. Test API callbacks
exercise control flow without accessing hardware; real hot-plug/driver behavior is still pending.

Read and write use `\PhysicalDisk(*)\Disk Read Bytes/sec` and `Disk Write Bytes/sec` from one
persistent query shared across the devices. Each disk matches its exact numeric prefix, excluding
`_Total` and lookalike prefixes (disk 1 cannot match disk 10). These are OS-visible device I/O,
not internal NAND operations. Partition counters are not added. Removable/virtual storage may
not expose a usable physical mapping. An absent counter is unavailable only for that disk;
query failure gives every disk its own error/priming state. No rates are summed across disks.

Active time uses a **separate** persistent `\PhysicalDisk(*)\% Idle Time` query, the same
exact disk identity matching, and `100 - idle`. Only valid, primed 0–100% idle values become
active readings. Non-finite/out-of-range data retains an error and diagnostic value; missing or
failed idle data cannot suppress read/write. Throughput failure likewise leaves active time
independent. This is a percentage of the measurement interval, not summed queued I/O time.
Microsoft's [PhysicalDisk counter definitions](https://learn.microsoft.com/en-us/previous-versions/aa394262(v=vs.85))
describe idle time; the implementation reads PDH directly and does not use WMI.

Discovery refresh removes disappeared disks and adds new devices, re-primes disk counters on
catalog changes, and triggers layout negotiation when the widget count changes. Before refresh,
a missing counter remains visibly unavailable. An empty discovery produces a clearly unavailable
disk placeholder, never a healthy zero. The previous single-disk setting is explicitly retired
by schema migration; it no longer hides other disks.

## Network

`GetIfTable2` discovers interface GUIDs and runtime LUIDs. Automatic selection requires an active
hardware interface associated with a default route; multiple qualifying routes expose ambiguity.
Loopback is excluded. Virtual/tunnel interfaces are separately labeled and may be explicitly
selected. GUIDs are persisted, never transient indices.

`GetIfEntry2` reads 64-bit InOctets and OutOctets. Download/upload are unsigned deltas divided by
the actual positive `steady_clock` elapsed seconds, timestamped at the completed interface read
so variable CPU/disk collection latency cannot distort the denominator. Decreases, zero/negative ordering, disconnect,
reselection and resume prime a new baseline. These measurements include LAN traffic. Link speeds
are discovery metadata only, not the scale or inferred internet capacity.
API errors retry after 1, 2, 4, 8, 16 and then at most every 30 seconds, measured from the
completed failed read. Skipped attempts preserve the native error. Success, interface changes
and explicit recovery/reset clear backoff; the first successful connected observation primes
a new baseline. Successfully read disconnected interfaces remain unavailable without API-error
backoff. Units remain B/s, KiB/s, MiB/s and GiB/s, never mislabeled bits/s.

## Rate scales since launch

Every disk read/write direction and selected interface upload/download direction uses its own
maximum valid observation since this process started. Fill is `clamp(current / peak, 0, 1)`;
zero is empty, a first positive observation fills the bar, and half the peak fills half.
There is no logarithmic floor, decay, rolling window, reference ceiling, or guessed capacity.
Tooltips show each peak in MB/s or GB/s; Settings retains the detailed B/s value. Percentages
retain fixed 0–100% scales. All popup byte quantities, including RAM/GPU used and total capacity,
use one decimal in MB or GB (divide by 1024 squared/cubed, switching at 1 GB). The popup labels
these as Windows-style binary units. Zero is `0.0 MB` (or `MB/s`); positive values below 0.1 MB
are `<0.1 MB` (or `MB/s`). Raw byte-count capacity lines are omitted. This formatting changes
neither measurements nor peaks, scopes, statuses or retained-observation ages. Inline and Settings
formatting are unchanged.

`SessionPeaks` is owned by Application and exclusively accessed by the single sampling worker.
Collector updates it before latest-value publication, so a coalesced-away high sample still
raises the scale. A new worker on Retry borrows the same object after the old worker joins.
Stable physical-device IDs/interface GUIDs plus direction key each peak. Disconnection, selection
changes, cadence changes, resume and counter-baseline resets keep peaks; process exit discards them.
Only finite nonnegative valid rates with a positive finite interval and increasing timestamps
update peaks. Invalid/stale/warming readings keep their status and any prior peak. No samples or
peaks are saved to disk. There are at most 4,096 device/direction records (one maximum and latest
accepted timestamp each); exhaustion reports an explicit error for new streams without evicting
existing peaks. Normal two-disk/one-interface use needs six records.

## GPU

DXGI enumerates non-software adapters and reported dedicated/shared capacities. A display-config
adapter device path is preferred as persistent identity; a vendor/device/subsystem/revision tuple
is the fallback. A tuple collision remains ambiguous. A runtime LUID is only the current counter
join key, never the persisted key. Automatic selection prefers the largest dedicated capacity;
ties require selection. DXCore confirms integrated/discrete classification and a single physical
adapter before memory scope is accepted.

`\GPU Engine(*)\Utilization Percentage` names are strictly parsed into LUID, physical node,
engine, process and engine type. Exact `3D` and `VideoDecode` types remain separate. Distinct
per-process contributions are added only within the same hardware engine, adapter and interval.
Duplicate keys, invalid rows and mixed timestamps/intervals invalidate the aggregate. The displayed
value is the busiest engine within each requested type; different engines are never summed.
An empty requested type is unavailable, not inferred idle. Raw values above 100 remain visible in
details/numbers with a diagnostic note; only fill is clipped. Driver-specific instance formats
that do not parse cannot become a misleading healthy value.

GPU memory first uses the selected `\GPU Adapter Memory(*)\Dedicated Usage` row divided
by matching DXGI dedicated capacity. If that observation is unavailable, warming, stale or invalid,
it tries adapter-wide `Shared Usage` against the selected adapter's DXGI shared-memory limit.
Multiple physical nodes, duplicate rows, unknown scope and zero capacity are rejected separately
for each source. Engine-query exceptions do not suppress memory queries. No process budgets,
CurrentUsage, process-memory sums or system-RAM substitution are used.

The selected meter is labeled **GPU VRAM** or **GPU shared**. Shared memory describes the adapter's
reported shared-system-memory limit, not physical VRAM; see [DXGI adapter metadata](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_adapter_desc).
Fresh shared data takes precedence over retained dedicated data. If both fail, the last displayed
observation survives with its matching bytes, percentage, capacity, scope and original timestamp.
Retention is bounded and keyed by stable adapter identity; Hide, reconnect and Retry preserve it.
Before the first valid observation, the memory bar is omitted and the GPU uses only 3D/decode.
Settings still exposes the raw memory failure. Integrated/linked-adapter accuracy remains a
hardware validation item.

## Temperature sources (1.2.0)

CPU temperature uses the already installed ASUS ATKACPI interface, opened with zero desired
access and overlapped I/O. The only request is IOCTL `0x0022240C`, status method `0x53545344`
(`DSTS`), endpoint `0x00120094`, in the 16-byte packet `{method, 8, endpoint, 0}`. The parser
requires 4–16 returned bytes, presence encoding `0x00010000`, and a 1–150°C low-word value.
It reports **ASUS firmware-reported CPU temperature**, not a verified package/hottest-core
scope. The same endpoint may describe Intel or AMD CPUs on compatible ASUS systems; it
does not provide generic CPU-brand coverage. The OEM protocol is not a public Microsoft API.
Its read-only use was explicitly reviewed against the
[G-Helper protocol implementation](https://github.com/seerge/g-helper/blob/main/app/AsusACPI.cs)
and probed under a standard user before implementation. Loadbar's reader is independently
implemented; no G-Helper code/runtime is bundled. No INIT, DEVS, fan, power, or control calls
are issued. One cached handle/event and 16-byte response remain owned through cancellation.
Timeout cancellation starts after two seconds; a driver that fails to complete can delay
shutdown. Failed reads/open attempts back off to 30 seconds and reset after resume/Retry.

GPU temperature uses `D3DKMTOpenAdapterFromLuid` for the selected adapter and
`D3DKMTQueryAdapterInfo` with `KMTQAITYPE_ADAPTERPERFDATA`. The main sensor is reported
in tenths of Celsius. Physical-adapter count and thermal capabilities are cached; linked
adapters use their hottest main sensor, with its index and matching limits in details.
All physical-adapter queries must succeed before claiming that maximum; partial failures
retain the previous complete observation instead of silently changing its scope.
A failed query or a zero record with no thermal capabilities does not establish a valid
zero temperature. Adapter changes/reset close and reopen the binding.
[Microsoft GPU interface](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_adapter_perfdata).

When that source fails, Loadbar loads only the selected GPU vendor's optional driver API:

| Vendor | Source and scope |
| --- | --- |
| NVIDIA | NVAPI `NvAPI_GPU_GetLogicalGpuInfo` matches the selected LUID; `NvAPI_GPU_GetThermalSettings` accepts only GPU-target sensors, excluding VRAM/board/power sensors. All matching physical members must report; show the hottest main sensor. |
| AMD | ADLX `IADLXGPU2::LUID` and distinct GPU IDs match the selected adapter. `GetCurrentGPUMetrics / GPUTemperature` supplies edge temperature, never hotspot. Matching members must equal Windows' physical-adapter count. |
| Intel | IGCL device properties match the selected LUID and Intel vendor ID. `ctlEnumTemperatureSensors / ctlTemperatureGetState` accepts GPU-domain sensors only and labels the maximum accordingly. Linked adapters are unavailable through this fallback because physical-member coverage cannot be verified. |

Missing/ambiguous identities, incomplete physical scope, invalid values and API failures
cannot become valid readings. Enumeration is bounded (256 logical devices/sensors, 64 physical
members, and NVIDIA's smaller API-specific limits). Interface versions and exports are checked.
The Windows source is first on each new binding; a working source stays cached until it
fails or settings/recovery reset it. Failure tries the alternate source, then retries with
2/4/8/16/30-second backoff. Healthy Windows reads never initialize a vendor API. GPU Hide
releases both bindings. No vendor history tracking, controls, helper process, or DLL download
is used. Intel enables only the Level Zero telemetry initialization flag, not firmware-update
functionality. Driver calls run on the worker and do not offer a guaranteed cancellable timeout.

These scopes are deliberately named separately; temperatures from different sensor locations
are not interchangeable accuracy references. See [NVIDIA thermal sensors](https://docs.nvidia.com/nvapi/group__gputhermal.html),
[AMD adapter identity](https://gpuopen.com/manuals/adlx/adlx-sdk-references/adlx-interfaces/gpu/iadlxgpu2/),
[AMD edge temperature](https://gpuopen.com/manuals/adlx/adlx-sdk-references/adlx-interfaces/performance-monitoring/iadlxgpumetrics/gputemperature/),
and [Intel IGCL](https://intel.github.io/drivers.gpu.control-library/api.html).

Drive temperature uses each discovered device-interface path, resolved with its stable
instance ID, and `IOCTL_STORAGE_QUERY_PROPERTY / StorageDeviceTemperatureProperty`.
Sensor 0 is preferred and described as composite **where reported**; otherwise the hottest
reported sensor is used. Signed Celsius is preserved. The parser validates version, returned
length, count, duplicate indices and the `0x8000` not-reported sentinel. The desktop-source
sanity range is −99 to 250°C, distinct from safe operating limits. A bounded 4-KiB descriptor
buffer accepts at most 128 sensors. Device warning/critical limits remain separate metadata.
[Microsoft storage descriptor](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-storage_temperature_data_descriptor).

Each visible drive owns one cached handle, event and asynchronous request buffer. No second
request overlaps the first. The worker polls completion without waiting, requests cancellation
after two seconds, and retains storage until completion. Delayed results carry the request's
original timestamp. Failed requests back off; unsupported/power/open failures cannot create
a per-tick retry loop. Hidden/disconnected devices cancel outstanding work. Discovery failure
pauses disk bindings until a complete successful scan. Sleeping drives are skipped using
`GetDevicePowerState`; temperature queries do not deliberately wake them. Storage drivers
can still affect power transitions and cancellation latency, requiring hardware checks.
Completed I/O or power-state failures discard the handle so an unchanged interface path
can recover after disconnect/reconnect. Shutdown cancels and drains pending requests before
freeing their buffers; a broken driver that never completes cancellation can delay worker join.

Temperature collection is capped at one Hz, or the configured slower sample interval.
**Show temperatures** is enabled by default. Applying it unchecked hides temperature
readouts and thermal icon effects, stops new sensor opens/reads and vendor probing, and
releases idle handles and request buffers. Only completion polling of already-canceled
asynchronous requests remains until their buffers can be released safely. An executing
synchronous driver call must return before the worker accepts new settings. No unrelated
utilization/rate query or session peak is reset. Re-enabling restores retained values with
their original ages while requesting fresh observations; Settings identifies disabled
collection as paused and popup tooltips omit temperature values.
The worker records temperature transitions independently, so an off/on pair coalesced during
a slow collection still resets only temperature bindings when the worker resumes.

Confirmed missing vendor DLLs/exports or successfully established absence of a main sensor
are remembered for five minutes for the selected stable identity and runtime binding. The
library and API objects are still released immediately. Transient access, initialization,
enumeration, identity and reading errors retain normal backoff. Device changes, reset/resume
and re-enabling clear the negative result; a time-based retry also discovers changed capability.

Asynchronous completion can reduce the effective cadence. Existing last-valid continuity
retains the temperature, sensor, limits and original age through failure, Hide, resume and
worker Retry, keyed by stable device identity. Sensor values never contribute to rate peaks.
The UI shows no initial temperature until a real valid observation, then retains it through
failures; details always expose the underlying status.

**CPU temperature outside compatible ASUS firmware and RAM temperature remain outside this
provider set.** Generic ACPI zones do not reliably identify CPU package or DIMM sensors.
Unsupported provider access leaves the readout absent and explains the limitation in details;
it does not assert that the hardware has no sensor. Network has no temperature metric.

## Evidence and limitations

Provider smoke checks demonstrated execution on the build host, including independent disks,
RAM capacity and GPU/NIC observations. They did not establish measurement accuracy, integrated
GPU scope on other hardware or non-English counter behavior. A transient out-of-range disk
idle value correctly remains an error. The current validation record and scope-matched manual
comparison procedures are in [testing.md](testing.md).

Primary references: Microsoft's [GPU scope discussion](https://devblogs.microsoft.com/directx/gpus-in-the-task-manager/),
[English counters](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhaddenglishcounterw)
and [formatted arrays](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw).

## Collection and presentation reuse

The optimization pass keeps the sources, scopes and formulas above. Each counter source owns its
reused arrays; borrowed batches expire on that source's next collection/reset and are copied into
the coherent snapshot before publication. PDH priming membership distinguishes the prior completed
array from the array being read. Invalid/disappeared instances lose eligibility and re-prime when
they return. GPU caching stores parsed identity only, never a previous counter status/value; its
8,192-name bound does not drop overflow samples. CPU topology lookup is rebuilt when discovery
changes, with unmatched counters still labeled as unmapped.

Presentation views borrow snapshot strings only during synchronous interpretation/formatting.
The renderer stores numeric readings, not borrowed strings, across operations. Raw diagnostic
changes still reach details/tooltips even when identical visible retained readings avoid repaint.
Deadline-based staleness preserves the strict existing threshold; retained-age text refreshes
only when there is a visible consumer. These are implementation properties, not counter-accuracy
or performance measurements. See testing.md for the post-change provider smoke runs.

## Hidden GPU and network widgets

Hide pauses collection for that family: GPU PDH queries close, and the NIC is not read.
The 30-second device catalog refresh continues so Settings remains useful. Device preferences,
last-valid readings and rate peaks remain in memory; visibility is persisted separately.
Showing a family re-primes only its counters, including rapid hide/show changes coalesced
before collection. CPU/RAM/disks and the other visible family keep collecting normally.
Settings reports “Hidden — sampling paused”; hidden metrics do not schedule freshness paints.

## Hidden physical drives

Settings stores explicit hidden stable IDs, never a whitelist of known drives. Collection still
publishes those identities with “Hidden — sampling paused” and retains their last observations
and session peaks. Shared wildcard disk queries continue while at least one disk is visible;
hidden rows are ignored during interpretation, so their values and peaks do not advance.
When all detected drives are hidden, both disk queries close and skip collection. Showing a
drive then opens fresh queries and primes them; toggling one drive while others remain visible
does not reset shared counters. Discovery continues while hidden. New identities automatically
collect and display; a reconnected excluded identity remains hidden even if its disk number changes.
The empty-discovery unavailable widget is distinct from an inventory whose disks are all hidden.

## Always-visible summaries (1.3.0)

Readout columns consume existing snapshots; they add no counter or sensor queries. CPU total
is the arithmetic mean of the displayed logical-processor observations, preserving SMT
weighting. For completely classified hybrid topology, P is the highest efficiency class and
E is the remaining classes, matching the existing hover policy. Any never-observed failed
logical processor makes the total unavailable; per-processor retained values and warm-up
zeros otherwise follow normal display continuity. These summaries never replace the tiles.

RAM percentage and used/total capacity use the same observation. GPU memory retains its
selected dedicated/shared scope, amount and capacity through failures; the readout omits it
only before the first valid memory observation. Disk and network directions remain separate
and use current rates, not peaks. The new compact byte suffixes are binary B/K/M/G/T/P/E;
rate suffixes imply /s. Explicit units, raw statuses, scopes and original ages remain in
Settings and tooltips. No readout value feeds back into telemetry or scaling.
