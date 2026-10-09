# Interface design

Loadbar uses a graphics-first layout with an opaque background, outlined device icons,
rounded meter tracks and static high-use glows. Embedded Geist Mono supplies bar text;
native controls keep their Windows font. **Always show readout** is enabled by default in
1.3.0, including upgrades, and places a two-line numeric column after every graphic. With it
disabled, **Show info on hover** controls the original inline overlays. That checkbox is
disabled while readouts are on, without erasing its choice. Native tooltips and Settings
provide complete readings. **Show tooltips** independently controls the native bar popup.
Hover info and tooltips are enabled by default; Settings readings and the tray tooltip remain
available. Popup byte quantities and rates use compact MB/GB and MB/s/GB/s with one decimal and explicitly binary units.

## Layout and scaling

CPU width follows its logical-processor grid and effective bar thickness. RAM and the other
visible device graphics divide the remaining row width equally along the negotiated edge.
Each logical processor has a separate tile, ordered by class on hybrid CPUs, then physical
core and group/number. Hybrid rectangles keep proportional class widths; a performance-class tile
spans two efficiency-class tile widths plus their gap, with 14/6-DIP reference heights.
Uniform or unknown efficiency classes use equally sized squares, left-aligned within the
CPU widget. A four-core/eight-thread CPU therefore shows eight squares. Squares use a single
row whenever they fit at the 6-DIP readable minimum, otherwise wrap with 2-DIP reference gaps.
Their side length fits the existing CPU band. The CPU widget ends at the grid, so a small
CPU does not leave a large empty allocation before RAM. At reference scale, hybrid P/E
tiles prefer 22/10-DIP widths, compressing toward 14/6 DIPs on constrained edges.
**Display CPU usage always as squares** uses 14/6-DIP P/E sides while keeping class grouping
and logical-processor siblings. It defaults off; uniform CPUs remain square in either mode.
Checked mode grows the square grid to use the strip thickness, bounded by the space needed
for readable device widgets. The CPU allocation follows the fitted grid along the edge;
remaining device graphics share the space equally.

Drives checkboxes appear below Network and above Edge in Settings. Unchecking a drive hides
its widget on Apply; new disks are checked automatically, and Cancel restores committed choices.

GPU and Network offer **Hide** independently. Hidden widgets leave no gap or hit target;
remaining device widgets share the freed width while CPU keeps its grid-sized allocation.

The compact layout determines widget and CPU rows from edge length, topology, device count
and Windows text scale. Greater bar thickness magnifies graphics, icons, hover fonts and
spacing together, capped by the space available to those rows. Further horizontal thickness
adds padding in rectangle mode; explicit square mode can use that space for the CPU grid.
Vertical bars stack CPU, RAM, GPU, drives and network; each upright icon is
centered below its graphic. Meters sit side by side and fill bottom to top, with the usual
10/4/4 or 14/6 proportions across their widths. The CPU grid rotates clockwise on Right
(E tiles left of P) and counterclockwise on Left (E tiles right of P), preserving each
logical processor and sibling ordering. Its fitted dimensions determine the CPU allocation;
non-CPU graphics divide the remaining height equally. The vertical compact device graphic is
at least 22 × 22 compact DIPs. At the 60-DIP reference scale, icon separation is 10 DIPs
and widget separation is 16 DIPs. Edge/topology
changes reflow; resize history does not affect the result.

The readable minimum may raise the requested thickness. Placement never silently hides cores
or disks. DPI converts between physical pixels and DIPs; Windows text scaling is separate
from content magnification. Borders remain one physical pixel. Placement limits and Settings
behavior are described in [architecture.md](architecture.md).

## Visual encoding

| Graphic | Encoding |
| --- | --- |
| CPU | Each tile uses its own logical processor's displayed utilization; physical-core relationships remain in details |
| RAM | Fixed percentage scale; hover shows coherent used/total binary GB and percentage |
| GPU | Device violet; 3D, selected memory, decode in 10/4/4 proportions; 3D/decode in 14/6 when memory has never been observed |
| Disk | Active/read/write in 10/4/4 proportions; hues `#65C55B`, `#B9DF9B`, `#36884D` |
| Network | Fixed 14/6-DIP download/upload split |
| Rates | Independent linear session-peak scales by device and direction |

CPU heat interpolates in sRGB. Each tile independently uses its retained observation through
failures; initial warm-up displays zero. A never-valid unavailable/error thread affects only
its own tile. [metrics.md](metrics.md) defines the status and scope rules.

Hover overlays appear immediately; the native tooltip delay is 800 ms. Text keeps a 9-DIP
minimum. RAM uses one line where possible, then two lines; an ellipsis indicates omitted
inline detail without discarding the complete tooltip/Settings value. On Right, inline text
reads top-down; on Left, bottom-up. Its backing panel rotates with the text and clips to the
widget. Icons, native popup tooltips and Settings text stay upright.

In high contrast, CPU interiors interpolate from `COLOR_WINDOW` at idle to
`COLOR_WINDOWTEXT` at full use, with a foreground outline keeping idle cores visible.
Outlines and hover text use the unblended system colors; glows are disabled. Intermediate
intensity encodes load without promising a minimum contrast ratio at every percentage.
See [Microsoft's high-contrast guidance](https://learn.microsoft.com/en-us/windows/win32/winauto/high-contrast-parameter).

## Temperature readouts (1.2.0)

The original [temperature reference](media/loadbar-temperatures.png),
[2× component export](media/loadbar-temperature-strip@2x.png), and
[color guide](media/loadbar-temperature-colors.png) are kept locally. The component export's
2400 × 120 pixels represent 1200 × 60 DIPs; its eight P and sixteen E tiles use square mode.
The CPU-shape setting still controls the application layout. Color thresholds and availability
policy are canonical in [AGENTS.md](../AGENTS.md#temperatures).

At 60 DIPs and 100% Windows text scaling, outer margins and device gaps are 16 DIPs. Each
32-DIP icon column precedes its graphic by 10 DIPs. The CPU divider is 1 × 30 DIPs, with
16-DIP separation on both sides. The graphic band is 33 DIPs high, centered vertically.
For the reference topology, its 189-DIP CPU grid leaves 126 DIPs for each other graphic.
P/E square corners have 4/2-DIP radii. Dimensions are shared by minimum-size calculation
and arrangement, normalized to the existing 40-DIP compact scale.

Icons are 18 × 18 DIPs at the reference scale, including network. A never-observed temperature
leaves its icon centered, without a placeholder. First observation moves that icon upward by
8 DIPs. A centered Geist Mono Medium 11-DIP Celsius line follows; in horizontal bars its
visible glyph bottom aligns with the graphic bottom. At the reference scale the icon starts
at y=13 and the text box runs from y=34 to 46.5 before pixel snapping. DirectWrite
[ink overhangs](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/ns-dwrite-dwrite_overhang_metrics)
provide the cached drawing offset; line-box leading is not treated as visible text.
Pixel snapping preserves the common bottom edge. Meters, CPU cells and unrelated icons do not
move when a temperature first arrives.
Disabling Show temperatures restores the centered icons and removes readouts, tint and thermal
glow, including retained temperatures. It is independent of hover information and tooltips.
Signed and three-digit values have a bounded wider hit/paint area without changing columns.
Temperature text has a 9-DIP minimum multiplied by Windows text scaling; DirectWrite glyph
bounds are tested using the embedded font. Vertical icons/readouts stay upright and use the
existing gaps without moving the next graphic. The vertical icon tail accommodates the larger
icon even before a temperature arrives. Content magnification and DPI remain separate.

Thermal icon color and glow follow the current policy in AGENTS.md. Rendering and dirty-region
decisions share a pure appearance calculation, so fractional changes inside the ramps repaint
even when the whole-number label is unchanged. Glow masks are independent of color/intensity
and remain cached; temperature changes do not create a continuous animation loop.

Production-renderer previews named `temperature-reference-*-1x-*.png` and
`temperature-reference-*-2x-*.png` cover normal/hot/critical states at 1200/1400-DIP lengths
and both vertical edges. Additional `temperature-ramp-top-1400x60-2x-*.png` previews show
60, 75, 90, 92.5 and 95°C with identical loads. Readings are synthetic, including RAM, whose live provider remains
unimplemented. Existing `temperature-*-60-*.png` fixtures cover first observation, retention,
reset, mixed availability and high contrast. Native full-edge AppBar borders remain opaque;
the reference's rounded presentation frame is not a change to the reserved window shape.

## Interaction and verification

Left-click opens Task Manager using its fixed system path when **Open Task Manager on click**
is checked (the default). Unchecking it leaves left-click inactive. Right-click opens the menu;
the notification icon and native Settings provide keyboard access and Exit.

The rendering suite exercises the production renderer on WIC bitmaps without an AppBar.
It checks CPU/meter levels, both orientations, DPI/text scaling, hover, retained/unavailable
values, resource failures and recovery, and resize equivalence. Fixtures under
`out/build/<preset>/render-fixtures/` are synthetic test artifacts. Live hover timing,
accessibility, Shell geometry and real GPU target loss remain manual checks in
[testing.md](testing.md). README media is documented separately in [animation.md](animation.md).

## Always-visible readouts (1.3.0)

The [supplied reference](media/loadbar-always-readout.png) defines the two-line composition.
CPU shows the logical-processor-weighted mean and, for completely classified hybrid CPUs,
P/E means using the same highest-class/rest grouping as the existing CPU hover summary.
Uniform or unknown classes omit that second line; incomplete displayed CPU inputs show an
unavailable total. Logical-processor tiles remain independent and complete.
RAM shows percentage plus used/total capacity. GPU shows 3D plus selected memory bytes and
▶decode percentage; never-observed memory leaves only decode on the second line. Each drive
shows active percentage plus R/W rates. Network shows ↓download then ↑upload. Component
palettes color the text; high contrast uses system foreground. Readouts obey existing
warm-up and last-valid continuity and never influence collection or session peaks.

Readout widths in compact DIPs are CPU 52 for classified hybrid topology or 30 otherwise,
RAM 56, GPU 62, disk 72 and network 42. Each follows its graphic with a 6-DIP gap. Widths
cover the maximum formatted text rather than the current reading, so values never move
widgets. Readout-mode widget spacing is 8 DIPs at the 60-DIP reference scale, including
either side of the CPU divider. Remaining space goes to equally sized non-CPU graphics;
minimum sizing and wrapped rows/columns use the actual component budgets.

Geist Mono uses 11/9-compact-DIP Medium/Regular text. Shared baselines at 9/20 compact DIPs
within a centered 22-DIP band replace independent text-box centering. Baseline offsets come
from [DirectWrite line metrics](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/ns-dwrite-dwrite_line_metrics)
and are cached with each text layout. Font size and spacing follow graphic magnification and
Windows text scale. Vertical groups retain temperature clearance inside their allocation;
wrapped columns reserve at least 28 compact DIPs so upright thermal labels cannot overlap.
The original graphics-only spacing remains unchanged.
Compact values use binary B/K/M/G/T/P/E, with rate suffixes implying bytes per second;
capacities share their denominator's unit. Rates have at most one decimal, with whole
numbers at 100 and above; zero stays zero and tiny positive values use <0.1. Tooltips
explain the convention and retain full device identity, scope, statuses and observation age.

Horizontal columns follow each graphic. Right-edge columns follow below it and rotate
top-down; Left-edge columns precede it spatially so bottom-up reading encounters the
readout after the graphic. Icons and temperatures stay upright beneath each complete group.
Short vertical edges reflow into additional widget columns through the same minimum-size
negotiation. In wrapped horizontal layouts, device-only rows use the compact graphic band
rather than inheriting a taller CPU grid. Every non-CPU graphic retains the same length. Applying the setting reflows
without restarting providers. Cached readout layouts are independent of temperatures and
hover overlays; unchanged text survives hover transitions. The [README animations](animation.md)
compare both modes at matching dimensions and graphic scale, with temperatures enabled.
