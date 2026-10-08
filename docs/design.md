# Interface design

Loadbar uses a graphics-first layout with an opaque background, outlined device icons,
rounded meter tracks and static high-use glows. Installed Consolas is preferred, with
Cascadia Mono as fallback. Numbers appear only on hover; native tooltips and Settings
provide complete readings when inline text cannot fit.

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

Drives checkboxes appear below Network and above Edge in Settings. Unchecking a drive hides
its widget on Apply; new disks are checked automatically, and Cancel restores committed choices.

GPU and Network offer **Hide** independently. Hidden widgets leave no gap or hit target;
remaining device widgets share the freed width while CPU keeps its grid-sized allocation.

The compact layout determines widget and CPU rows from edge length, topology, device count
and Windows text scale. Greater bar thickness magnifies graphics, icons, hover fonts and
spacing together, capped by the space available to those rows. Further horizontal thickness
adds padding; vertical thickness can still widen graphics after icon/row scaling reaches its
cap. Edge/topology changes reflow; resize history does not affect the result.

The readable minimum may raise the requested thickness. Placement never silently hides cores
or disks. DPI converts between physical pixels and DIPs; Windows text scaling is separate
from content magnification. Borders remain one physical pixel. Placement limits and Settings
behavior are described in [architecture.md](architecture.md).

## Visual encoding

| Graphic | Encoding |
| --- | --- |
| CPU | Each tile uses its own logical processor's displayed utilization; physical-core relationships remain in details |
| RAM | Fixed percentage scale; hover shows coherent used/total binary GB and percentage |
| GPU | Device violet; visual order is 3D, memory, decode, with 10/4/4-DIP reference heights |
| Disk | Active/read/write in 10/4/4 proportions; hues `#65C55B`, `#B9DF9B`, `#36884D` |
| Network | Fixed 14/6-DIP download/upload split |
| Rates | Independent linear session-peak scales by device and direction |

CPU heat interpolates in sRGB. Each tile independently uses its retained observation through
failures; initial warm-up displays zero. A never-valid unavailable/error thread affects only
its own tile. [metrics.md](metrics.md) defines the status and scope rules.

Hover overlays appear immediately; the native tooltip delay is 800 ms. Text keeps a 9-DIP
minimum. RAM uses one line where possible, then two lines; an ellipsis indicates omitted
inline detail without discarding the complete tooltip/Settings value.

In high contrast, CPU interiors interpolate from `COLOR_WINDOW` at idle to
`COLOR_WINDOWTEXT` at full use, with a foreground outline keeping idle cores visible.
Outlines and hover text use the unblended system colors; glows are disabled. Intermediate
intensity encodes load without promising a minimum contrast ratio at every percentage.
See [Microsoft's high-contrast guidance](https://learn.microsoft.com/en-us/windows/win32/winauto/high-contrast-parameter).

## Interaction and verification

Left-click opens Task Manager using its fixed system path. Right-click opens the menu;
the notification icon and native Settings provide keyboard access and Exit.

The rendering suite exercises the production renderer on WIC bitmaps without an AppBar.
It checks CPU/meter levels, both orientations, DPI/text scaling, hover, retained/unavailable
values, resource failures and recovery, and resize equivalence. Fixtures under
`out/build/<preset>/render-fixtures/` are synthetic test artifacts. Live hover timing,
accessibility, Shell geometry and real GPU target loss remain manual checks in
[testing.md](testing.md). README media is documented separately in [animation.md](animation.md).
