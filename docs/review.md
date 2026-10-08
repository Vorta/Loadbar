# Review record

Independent adversarial and optimization reviews informed the implementation before the
initial 1.0.0 commit. Findings were fixed with regression coverage; final implementation
and release-preparation rounds each ended with two independent consecutive clean reviews.

## Logical-processor/performance update

Two independent consecutive adversarial/performance reviews found no actionable issues in
this update. Reviewers checked per-thread tile identity/status and layout reordering, GPU
scratch-reference lifetimes and aggregation semantics, Hide/deactivation ownership, glow
bounds and DIP mapping, and network backoff/recovery. The first independently ran the Debug
model and offscreen renderer executables and inspected the eight-thread preview; the second
ran both Release-candidate executables. Both checked the diff. Neither launched an AppBar or
changed user settings. These reviews do not establish production overhead or hardware accuracy.

## 1.1.0 local change review

Two independent consecutive adversarial reviews found no actionable defects in the final
visibility/square-core implementation. Both also checked the final test-only widening fix.
Reviews covered settings migration/drafts, selective resets and coalesced generations,
continuity, dynamic widget indices, rendering caches, deadlines, and documentation.
Separate deterministic probes against the production model passed 9,740 and 11,113 feasible
randomized layouts respectively; the second also preserved GPU memory readings across 100
hidden snapshots. These were scratch correctness probes, not performance measurements.

## Drive visibility review

The first round reproduced an empty-list keyboard defect: Space could create a meaningless
checkbox on the no-drives informational row. Disabling the empty checklist and re-enabling
it on discovery fixed the issue; native tests cover interaction eligibility and transitions.

Two independent consecutive follow-up reviews found no actionable defects. They covered
schema 9 migration/bounds, sorted exclusion invariants, draft/hotplug handling, query pause
and reopening, retained values/peaks, deadlines, widget identity remapping and render caches.
The first independently checked 20,000 deterministic hide/show operations against a `std::set`
oracle, canonical serialization and malformed records in an isolated model executable. The
second independently ran the Debug unit and hidden native Settings executables; both exited 0.
No reviewer launched an AppBar, changed user settings or treated these checks as hardware evidence.

## Coverage and resolved findings

| Area | Representative corrections |
| --- | --- |
| Shell/layout | Bounded renegotiation after Shell edge shortening, settled-position loop prevention, resize-driven invalidation |
| Discovery | Transactional disk enumeration; retained CPU/disk identities and diagnostics through failures and worker replacement |
| Metrics/delivery | Correct NIC observation intervals, per-metric stale deadlines, catalog preservation during coalescing, last-valid continuity |
| Settings | Command filtering, dirty-state handling, responsive readings/footer, keyboard traversal, monitor refresh independent of telemetry |
| Rendering | Atomic resource recovery, solid CPU cells, utilization-dependent high-contrast colors, bounded scale-aware caches |
| Resources/tray | Manifest/license incremental dependencies, exact embedded notice tests, standard version-4 tray tooltip |
| Tooling/media | Optional fixture isolation from normal execution and standalone analysis; reproducible normal-mode README animation |

Current behavior is described in [architecture.md](architecture.md), [metrics.md](metrics.md)
and [design.md](design.md). Commands, automated results and manual procedures are consolidated
in [testing.md](testing.md); performance and packaging have separate evidence documents.

A clean source review means no additional actionable finding in its scope. It does not prove
live Shell behavior, hardware accuracy, production overhead or standalone compatibility.
The README media is synthetic; no reviewer used it as hardware evidence.

## CPU content-width follow-up

Two independent consecutive reviews found no actionable defects in grid-sized CPU allocation
and redistribution to equal-width non-CPU graphics. They checked narrow/wrapped/vertical
layouts, uniform/hybrid/empty/high-count topology, hidden devices, compact-fit monotonicity,
thickness magnification, alignment, DPI snapping and renderer hit/invalidation geometry.
Both ran the Debug model executable successfully. Final documentation and RAM-ellipsis
fixture adjustments were rechecked independently; all truncation assertions remain intact.
Live AppBar appearance and production performance were not exercised.

## Unreleased GPU fallback and presentation preferences

After correcting a Markdown table delimiter and the future-schema test label, two fresh,
independent adversarial reviews completed consecutively with no actionable findings. They
covered dedicated/shared/retained memory scope and age, adapter changes, two-engine layout,
P/E square sizing, complete thread coverage, vertical allocation/fill/icon placement, cache
invalidation, native draft/Cancel and interaction gates, schema-10 migration, regression tests
and documentation. Both inspected generated production-renderer previews and ran diff checks.
The earlier acceptance reviewer also ran the Release model and hidden-settings test binaries.

These reviews did not launch an AppBar or Task Manager, compare hardware counters, measure
production resource usage, or test standalone packaging. Automated verification is recorded
in [testing.md](testing.md); manual gates remain pending.

## Unreleased edge-aware CPU and hover layout

The first review was clean. The second found a reproducible uniform/unknown CPU square-fit
gap: 128 threads could occupy only part of the declared cross-strip band. A regression
failed before the fix; complete occupied-row fitting corrected it. Both reviewers then
independently reviewed the final implementation, consecutively reporting no actionable
findings. Both confirmed the final explicit integer-row calculation required by clang-tidy.

Their scope included resolved-edge propagation, CPU identities/readability, square fitting,
P/E sides, remaining device allocations, cache invalidation, hover rotation/clipping and
resource-failure recovery. Each ran a model test executable; neither launched the AppBar,
measured production overhead, or validated hardware/standalone behavior. Full automated
verification and the pending interactive procedure are in [testing.md](testing.md).

## 1.1.3 publication review

At the owner's request, a fresh subagent independently reviewed the complete feature diff
before publication, including GPU memory fallback/continuity, schema-10 settings,
interaction gates, CPU fitting and vertical rendering. It reported no actionable findings
and independently ran the Release model test executable successfully. No code changes were
needed from this review. The owner also reported that the application looks and works
correctly during their manual use; no detailed hardware or test-matrix coverage was claimed.
