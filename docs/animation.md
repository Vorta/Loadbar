# README animation

The README GIF is an explicit synthetic documentation fixture rendered through Loadbar's
production Direct2D/DirectWrite renderer. It is not a recording, benchmark, hardware-accuracy
test, or proof of Shell work-area behavior. README prose identifies the synthetic readings;
the animation contains no text, hover values, captions or resize demonstrations.
The normal application has no demo mode and does not link this fixture executable.

The 24-second loop shows normal-mode readings at a fixed 60-DIP thickness. Its source is
1400 x 60 pixels at 96 DPI, preserving native pixels with no padding or image resizing.
Sampling is one update per second, without interpolated animation.
It depicts one horizontal bar with a fictional hybrid CPU (eight SMT P-cores and sixteen
single-thread E-cores, matching the infographic), RAM, GPU, two disks and one NIC.
The same session-peak model used by the application processes the synthetic rates.

The supplied docs/media/loadbar-infographic.svg is embedded in the README's widget
guide. Its PNG alternative is retained in docs/media for reuse.
The animation shares the SVG's 1400-unit width; a README viewer may scale both images to
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
only the finished GIF and still preview are checked in.
