"""Encode text-free, normal-mode offscreen frames; never captures a desktop."""

import argparse
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("docs/media"))
    args = parser.parse_args()
    size = (1400, 60)  # 96-DPI source: one physical pixel per DIP, with no resizing or padding.
    images = []
    for index in range(24):
        with Image.open(args.frames / f"frame-{index:02}.png") as source:
            if source.size != size:
                raise ValueError(f"Unexpected fixture size: {source.size}, expected {size}")
            images.append(source.convert("RGB"))
    args.output.mkdir(parents=True, exist_ok=True)
    images[10].save(args.output / "loadbar-preview.png", optimize=True)
    # One shared palette prevents unrelated colors from flickering between samples.
    palette_source = Image.new("RGB", (size[0], size[1] * len(images)))
    for index, image in enumerate(images):
        palette_source.paste(image, (0, size[1] * index))
    palette = palette_source.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    quantized = [image.quantize(palette=palette, dither=Image.Dither.NONE) for image in images]
    path = args.output / "loadbar-demo.gif"
    quantized[0].save(path, save_all=True, append_images=quantized[1:], duration=1000,
                      loop=0, optimize=False, disposal=1)
    with Image.open(path) as result:
        if result.n_frames != 24 or result.size != size:
            raise ValueError("GIF validation failed")
        duration = 0
        for index in range(result.n_frames):
            result.seek(index)
            result.load()
            duration += result.info.get("duration", 0)
        if duration != 24000:
            raise ValueError("GIF timing validation failed")
    print(f"Created {path}: 24 frames / 24 seconds / {path.stat().st_size:,} bytes")


if __name__ == "__main__":
    main()
