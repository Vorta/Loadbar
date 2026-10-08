# Review record

Independent adversarial and optimization reviews informed the implementation before the
initial 1.0.0 commit. Findings were fixed with regression coverage; final implementation
and release-preparation rounds each ended with two independent consecutive clean reviews.

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
