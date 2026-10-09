"""Encode matching readout-off/on offscreen frames; never captures a desktop."""

import argparse
from pathlib import Path

from PIL import Image, ImageChops


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("docs/media"))
    args = parser.parse_args()
    size = (1920, 60)  # 96-DPI source: one physical pixel per DIP, no resizing or padding.
    modes = (("graphics-only", "loadbar-demo.gif"), ("readout", "loadbar-readout-demo.gif"))
    sequences = []
    for mode, _ in modes:
        images = []
        for index in range(24):
            with Image.open(args.frames / mode / f"frame-{index:02}.png") as source:
                if source.size != size:
                    raise ValueError(f"Unexpected fixture size: {source.size}, expected {size}")
                images.append(source.convert("RGB"))
        sequences.append(images)
    args.output.mkdir(parents=True, exist_ok=True)
    sequences[0][10].save(args.output / "loadbar-preview.png", optimize=True)
    # Share colors across both modes and every sample, including text antialiasing.
    images = [image for sequence in sequences for image in sequence]
    palette_source = Image.new("RGB", (size[0], size[1] * len(images)))
    for index, image in enumerate(images):
        palette_source.paste(image, (0, size[1] * index))
    palette = palette_source.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    for (_, filename), sequence in zip(modes, sequences, strict=True):
        quantized = [image.quantize(palette=palette, dither=Image.Dither.NONE)
                     for image in sequence]
        path = args.output / filename
        quantized[0].save(path, save_all=True, append_images=quantized[1:], duration=1000,
                          loop=0, optimize=False, disposal=1)
        with Image.open(path) as result:
            if result.n_frames != 24 or result.size != size or result.info.get("loop") != 0:
                raise ValueError("GIF dimensions/frame count/loop validation failed")
            for index, expected in enumerate(quantized):
                result.seek(index)
                if result.info.get("duration") != 1000:
                    raise ValueError("GIF frame timing validation failed")
                difference = ImageChops.difference(result.convert("RGB"), expected.convert("RGB"))
                if difference.getbbox() is not None:
                    raise ValueError(f"GIF frame {index} does not match its quantized source")
        print(f"Created {path}: 24 frames / 24 seconds / {path.stat().st_size:,} bytes")


if __name__ == "__main__":
    main()
