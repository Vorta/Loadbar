# Interface design

Loadbar uses a graphics-first layout with an opaque background, outlined device icons,
rounded meter tracks and static high-use glows. Installed Consolas is preferred, with
Cascadia Mono as fallback. Numbers appear only on hover; native tooltips and Settings
provide complete readings when inline text cannot fit.

## Layout and scaling

CPU and every device widget share equal graphic widths along the full negotiated edge.
Physical-core rectangles stretch proportionally. In a hybrid 8+16 layout, one performance-core
cell spans two efficiency cells plus their gap; compact reference heights are 14/6 DIPs.
Efficiency classes group cores without changing metric aggregation. Uniform or unknown
topology uses neutral grouping.

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
| CPU | Whole rectangle uses the busiest sibling's displayed utilization; every logical processor is listed in details |
| RAM | Fixed percentage scale; hover shows coherent used/total binary GB and percentage |
| GPU | Device violet; visual order is 3D, memory, decode, with 10/4/4-DIP reference heights |
| Disk | Active/read/write in 10/4/4 proportions; hues `#65C55B`, `#B9DF9B`, `#36884D` |
| Network | Fixed 14/6-DIP download/upload split |
| Rates | Independent linear session-peak scales by device and direction |

CPU heat interpolates in sRGB. A retained sibling value remains eligible for its core's color;
initial warm-up displays zero. A never-valid unavailable/error sibling leaves the core
incomplete. [metrics.md](metrics.md) defines the status and scope rules.

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
