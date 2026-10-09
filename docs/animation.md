# README animation

The README GIFs are explicit synthetic documentation fixtures rendered through Loadbar's
production Direct2D/DirectWrite renderer. It is not a recording, benchmark, hardware-accuracy
test, or proof of Shell work-area behavior. README prose identifies the synthetic readings.
Both show temperatures; the second also enables **Always show readout**. There are no
hover, resize or caption sequences. All visible text comes from the production renderer.
The normal application has no demo mode and does not link this fixture executable.

Both 24-second loops show the same samples at a fixed 60-DIP thickness. Their sources are
1920 × 60 pixels at 96 DPI, preserving native pixels with no padding or image resizing.
This width accommodates readouts without shrinking the CPU example. The fixture
checks that both layouts fit at 1.5× compact content scale, with identical CPU tile, icon,
temperature and meter heights. Non-CPU meters share the space left by each mode.
Sampling is one update per second, without interpolated animation.
Each depicts one horizontal bar with a fictional hybrid CPU (eight P-cores and sixteen
E-cores, one logical processor per core), RAM, GPU, two disks and one NIC. The fixture checks
that exactly eight P tiles and sixteen E tiles are rendered. CPU loads use a fixed-seed
pseudorandom sequence with independent per-tile values, shared by both animations.
Both enable **Display CPU usage always as squares**, retaining the larger P-core and smaller
E-core tile sizes. CPU, GPU and both drives have synthetic temperatures; RAM has none,
matching current provider coverage. The infographic is a schematic guide, not an exact layout.
The same session-peak model used by the application processes the synthetic rates.

The supplied docs/media/loadbar-infographic.svg is embedded in the README's widget
guide. Its PNG alternative is retained in docs/media for reuse.
The two GIFs share dimensions, palette and timing. A README viewer may scale the images to
fit its content width. The 60-DIP setting refers to the offscreen application rendering,
not a promise of 60 screen pixels in every browser layout.

SVG supports animation in browsers, but GitHub's documented repository image viewer does
not support SVG animation. GIF is the conservative README format; the PNG is a still-image
alternative. See [GitHub image viewing](https://docs.github.com/en/repositories/working-with-files/using-files/working-with-non-code-files#viewing-images).

Rebuild explicitly from the repository root:

```powershell
. .\scripts\enter-dev-shell.ps1
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release --target loadbar_readme_frames
& .\out\build\windows-x64-release\loadbar_readme_frames.exe .\out\readme-frames
python scripts/make-readme-animation.py --frames out/readme-frames --output docs/media
```

The optional composition script uses **Pillow 12.2.0** (already installed in the preparation
environment, Python 3.14.7), under the [Pillow HPND license](https://github.com/python-pillow/Pillow/blob/12.2.0/LICENSE).
It encodes GIF/PNG only; it is not a build or runtime dependency of Loadbar.
No library is downloaded by these scripts. Check Pillow's version before reproducing the
asset; any installation is an explicit developer action. No fonts are loaded by the encoding
script. Output frames/build files stay under out/;
only the finished GIFs and graphics-only still preview are checked in.

For 1.3.0, the optional fixture was rebuilt with the current production code. The encoder
decodes every GIF frame and verifies its pixels against the quantized source, plus dimensions,
frame count, one-second timing and infinite looping. Both modes' source frames were visually
checked. This validates the files; GitHub playback remains a publishing-time check.
