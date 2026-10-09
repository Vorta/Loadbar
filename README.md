# Loadbar

**A compact desktop system monitor designed for low CPU usage and a small RAM footprint.**

Loadbar shows per-core CPU activity, RAM, GPU, every physical disk, and network traffic in
one native Windows bar. Place it along any edge of your chosen monitor. It reserves desktop
space so ordinary maximized windows fit beside it. Show numbers beside the graphics or on hover.

**Always show readout: off**

![Loadbar with graphics and temperatures at 60 DIPs](docs/media/loadbar-demo.gif)

**Always show readout: on**

![Loadbar with numeric readouts and temperatures at the same scale](docs/media/loadbar-readout-demo.gif)

*Same scale, synthetic readings and temperatures: 60 DIPs, 8 P-cores + 16 E-cores,
with CPU squares enabled and one logical processor per core.*

Built in native C++, Loadbar samples once per second by default and redraws when readings
or window state change. It reuses resources and keeps current readings rather than a growing
history. No browser engine or background service. See the [performance evidence](docs/performance.md)
for measured usage and validation limits.

## What it shows

![Illustrated guide to Loadbar's CPU, memory, GPU, disk and network widgets](docs/media/loadbar-infographic.svg)

| Widget | Readings |
| --- | --- |
| CPU | One colored tile per logical processor, with physical-core relationships in details |
| RAM | Physical memory used, plus used/total capacity in readouts or on hover |
| GPU | Separate 3D activity, video decode, and GPU memory |
| Disks | Every discovered physical disk, with active time and separate read/write rates |
| Network | Download and upload on the selected interface, including local-network traffic |

Percentage meters use 0–100%. Each disk/network rate direction scales to its highest valid
reading since launch. Failed samples retain their last valid display; raw status and
observation age remain available in details.

Version 1.2.0 adds CPU temperature on compatible ASUS systems, plus GPU and drive
temperatures from installed drivers, using bundled Geist Mono. Temperatures appear below
their icons after the first valid reading. Coverage depends on the firmware and driver;
see [temperature sources](docs/metrics.md#temperature-sources-120). RAM temperature remains unavailable.
Uncheck **Show temperatures** in Settings to hide the readouts and stop all temperature
collection; other readings continue normally.

Version 1.3.0 enables **Always show readout** by default: numeric values sit
beside each graphic. Uncheck it in Settings to return to the original graphics and hover
mode. Popup tooltips and temperatures remain independently controlled.

## Run Loadbar

**Requires Windows 11 x64.** Download **[Loadbar 1.3.0](https://github.com/Vorta/Loadbar/releases/tag/v1.3.0)**.
See the [release notes](docs/releases/1.3.0.md) for details and validation status.

1. Run `Loadbar.exe`. Its notification-area icon may be in the overflow menu.
2. Click the icon for **Settings**. Choose the monitor, edge, GPU and network interface.
   Hide GPU or Network, or uncheck individual drives, to give the remaining widgets more space.
   New drives are shown automatically. Allow a few samples for counters to warm up.
3. Hover for readings and device details. Left-click the bar to open Task Manager;
   right-click for the menu. Inline hover readings, popup tooltips and the Task Manager shortcut
   can each be disabled separately in Settings.
4. Choose **Exit** in the tray/bar menu or **Exit Loadbar** in Settings to release the
   reserved space. **Close** dismisses Settings and discards unapplied changes while Loadbar
   keeps running.

Adjust thickness from **40–640 DIPs**, alignment, and sampling from **250–5,000 ms**.
Enable **Display CPU usage always as squares** to use square CPU tiles on hybrid processors too.
Loadbar increases thickness when needed to keep every widget readable. Settings is
keyboard-accessible; use `Win+B` to reach the tray. Preferences are saved per user in
`HKCU\Software\Loadbar`. Samples and session peaks are never saved. Loadbar does not
register itself to start with Windows.

## Build from source

Use PowerShell 7 and the [tested Windows toolchain](docs/toolchain.md). From the repository root:

```powershell
. .\scripts\enter-dev-shell.ps1
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
ctest --preset windows-x64-release --output-on-failure
```

The application is a single executable at `out\build\windows-x64-release\Loadbar.exe`.
Launch it with:

```powershell
Start-Process .\out\build\windows-x64-release\Loadbar.exe
```

See [testing](docs/testing.md) for Debug, ASan and analysis commands, or
[CONTRIBUTING.md](CONTRIBUTING.md) to contribute. Metric sources and limitations are in
[docs/metrics.md](docs/metrics.md).

Copyright © 2026 **Vorta**. Released under the [MIT license](LICENSE).
Bundled [Geist Mono](docs/dependencies.md) is licensed under the SIL Open Font License.
