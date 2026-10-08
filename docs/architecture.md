# Architecture

Loadbar is a native Win32 application with four static component libraries and one executable.

| Directory | Responsibility |
| --- | --- |
| `src/model` | Metrics, status/rate arithmetic, identity parsing, settings, geometry and layout |
| `src/platform` | Win32 ownership, monitor identity, registry, instance mutex and Shell transport |
| `src/telemetry` | Discovery, persistent counters, network collection and sampling worker |
| `src/ui` | Direct2D/DirectWrite rendering and Windows text-scale notifications |
| `src/app` | Window callbacks, AppBar orchestration, Settings and shutdown |

## Ownership and delivery

The UI thread owns windows, Shell registration and drawing resources. One joinable
`std::jthread` owns provider state and samples on interruptible waits. Missed deadlines are
skipped. Lock/suspend pauses sampling; resume re-primes counters.

A mutex-protected, one-slot mailbox publishes coherent snapshots with coalesced notifications.
No provider, Shell or graphics call runs under its mutex. Configuration generations reject
obsolete samples; a separate worker ID rejects late failure messages after Retry. Immutable
catalogs accompany snapshots so coalescing cannot lose discovery changes.

Application owns continuity, rate peaks, CPU identities and the completed disk inventory
across worker replacement. Only the active worker accesses these objects; Retry joins the old
worker before replacing it. Provider details and retention bounds are in [metrics.md](metrics.md).

Shutdown closes publication, releases the AppBar/tray, stops timers, signals and joins the
worker, revokes callbacks, then destroys resources. Windows APIs that block synchronously can
delay the join; stop tokens do not cancel them. The text-scale callback's shared notification
target is cleared before revocation. Exceptions do not cross Win32 or worker boundaries.

## Placement and recovery

An injectable Shell transport implements AppBar registration and negotiation in physical pixels.
The pure placement model evaluates readable thickness against the Shell-adjusted edge, including
up to four negotiations if SETPOS further shortens it. Identical settled submissions reuse the
final rectangle to avoid notification loops. Display/DPI changes and re-registration invalidate
that constraint. The safety cap is 25% of the monitor's perpendicular dimension; impossible
layouts fail visibly instead of omitting widgets.

Monitor preferences use display-configuration target paths. A missing preference falls back
to primary without losing the saved identity. Monitor choices refresh independently of the
telemetry catalog and preserve Settings drafts. Fullscreen lowers z-order without activation
and keeps the reservation. Explorer restart restores registration and the tray; repeated failure
removes the reservation and leaves Settings/Exit available.

A session-local mutex with a user-only ACL enforces one instance per user/session. A second
launch reports the existing instance before creating an AppBar; there is no IPC payload.

## Settings

A versioned registry value stores committed settings. Schema 7 accepts whole-number thickness;
older schemas validate against their original bounds before migrating percentage/fractional
sizes and retired appearance/rate controls. Conversion uses the resolved monitor and DPI.
Loading never writes; a save failure preserves the active in-memory configuration.

Settings uses native controls, a scrollable content host and a readings ListView that fills the
available space. Labels stack on narrow windows. The right-aligned footer wraps, or joins the
scrolling content when it cannot fit. Keyboard traversal follows control order in either mode.
DPI/text scale changes control sizing; ordinary resizing only changes layout and table columns.

Apply is enabled only for a valid changed draft; Cancel restores committed values. Placement
failure retains the draft. Tray changes refresh a clean form and preserve edits. Actionable
footer messages cover placement, sampling, tray, rendering, persistence and monitor fallback.
Retry appears only for recoverable infrastructure failures; individual counters retry themselves.
Closing Settings normally leaves monitoring active, but exits if the tray path is unavailable.

## Rendering and resources

The renderer consumes completed snapshots, never providers. Sample changes, resize, expose,
hover and status deadlines invalidate content; there is no continuous rendering loop. Cached
fonts, layouts, icon geometry and bounded glow bitmaps are reused. Resource groups commit
atomically; target loss discards only target-bound resources. See [design.md](design.md).

Native tooltips and the Settings readings list expose exact values, identities and raw status.
Painted cores are not separate UI Automation elements. List structure is reused across samples.

Defaults, `resources/loadbar.ico`, manifest, version 1.0.0 metadata and the MIT notice are
embedded. `resources/icon.svg` is editable artwork; it is not loaded at runtime. CMake tracks
file-backed resource and manifest dependencies explicitly.
