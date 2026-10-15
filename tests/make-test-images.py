#!/usr/bin/env python3
"""Creates the test image folder used by tests/gui-test.sh.

Runs on the development machine (needs Pillow and ImageMagick's "magick"):
    python3 tests/make-test-images.py out-dir

One image per format Haiku's translators read, plus the cases a browser and
viewer must handle: a large photo, a very tall image (scrolling), alpha, a
tiny image, an EXIF rotated JPEG, a corrupt file, a file without extension,
a text file (must be ignored), natural sort names and a subfolder.
"""

import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont


def font(size):
    for name in ("DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
                 "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def picture(width, height, label, hue, mode="RGB"):
    """A gradient 'landscape' with a big label, so thumbnails differ."""
    image = Image.new("RGB", (width, height))
    draw = ImageDraw.Draw(image)
    for y in range(height):
        t = y / max(1, height - 1)
        r = int((hue[0] * (1 - t) + 30 * t))
        g = int((hue[1] * (1 - t) + 160 * t))
        b = int((hue[2] * (1 - t) + 60 * t))
        draw.line([(0, y), (width, y)], fill=(r, g, b))
    r = min(width, height) // 6
    draw.ellipse([width * 0.7 - r, height * 0.25 - r, width * 0.7 + r,
                  height * 0.25 + r], fill=(255, 220, 80))
    size = max(10, min(width, height) // 6)
    draw.text((width * 0.08, height * 0.6), label, fill=(255, 255, 255),
              font=font(size), stroke_width=max(1, size // 15),
              stroke_fill=(0, 0, 0))
    return image.convert(mode) if mode != "RGB" else image


def main():
    out = sys.argv[1]
    os.makedirs(os.path.join(out, "More photos"), exist_ok=True)
    p = lambda name: os.path.join(out, name)

    picture(1600, 1200, "JPEG", (200, 80, 60)).save(p("sunset.jpg"), quality=88)
    picture(1200, 900, "PNG", (60, 90, 220)).save(p("lake.png"))
    picture(800, 600, "GIF", (220, 160, 40)).convert("P").save(p("desert.gif"))
    picture(640, 480, "BMP", (120, 60, 200)).save(p("violet.bmp"))
    picture(1024, 768, "TIFF", (40, 160, 160)).save(p("teal.tiff"))
    picture(1000, 750, "WebP", (180, 40, 120)).save(p("berry.webp"), quality=85)
    picture(800, 600, "TGA", (90, 140, 40)).save(p("meadow.tga"))
    picture(700, 500, "PPM", (150, 150, 150)).save(p("grey.ppm"))
    # classic (BMP based) icon entries; Haiku's ICO translator does not read
    # the PNG compressed entries newer icons may contain
    picture(64, 64, "ICO", (230, 100, 30)).save(p("icon.ico"), sizes=[(64, 64)],
                                                bitmap_format="bmp")
    picture(900, 700, "J2K", (30, 60, 120)).save(p("night.jp2"))
    picture(64, 48, "s", (200, 200, 60)).save(p("tiny.png"))

    picture(1200, 900, "AVIF", (220, 60, 160)).save(p("avif-source.png"))
    subprocess.run(["avifenc", "-q", "70", p("avif-source.png"),
                    p("pink.avif")], check=True, capture_output=True)
    os.remove(p("avif-source.png"))

    picture(900, 700, "PSD", (100, 100, 220)).save(p("psd-source.png"))
    subprocess.run(["magick", p("psd-source.png"), p("layers.psd")],
                   check=True)
    os.remove(p("psd-source.png"))

    # large photo for zoom and loading time
    picture(6000, 4000, "LARGE 6000x4000", (90, 140, 210)).save(
        p("large.jpg"), quality=85)

    # very tall image: scrolling with the wheel; numbered bands
    tall = Image.new("RGB", (900, 6000), (240, 240, 240))
    draw = ImageDraw.Draw(tall)
    for i in range(30):
        y = i * 200
        draw.rectangle([0, y, 900, y + 199],
                       fill=(255 - i * 6, 120 + i * 4, 80 + i * 5))
        draw.text((40, y + 40), f"Band {i + 1}", fill=(0, 0, 0), font=font(90))
    tall.save(p("tall.png"))

    # transparency
    alpha = Image.new("RGBA", (600, 600), (0, 0, 0, 0))
    draw = ImageDraw.Draw(alpha)
    draw.ellipse([50, 50, 550, 550], fill=(230, 60, 60, 255))
    draw.ellipse([200, 200, 400, 400], fill=(255, 255, 255, 120))
    alpha.save(p("alpha.png"))

    # EXIF orientation 6 (rotate 90 degrees clockwise): stored landscape
    # 800x600 with the label sideways, must show as portrait 600x800
    rotated = picture(600, 800, "EXIF", (60, 200, 120)).rotate(90, expand=True)
    exif = Image.Exif()
    exif[0x0112] = 6
    rotated.save(p("portrait-exif.jpg"), exif=exif, quality=88)

    # natural sort: IMG_2 before IMG_10
    for n in (1, 2, 10, 20):
        picture(400, 300, f"IMG {n}", (40 * n % 255, 100, 200)).save(
            p(f"IMG_{n}.jpg"), quality=80)

    # a PNG without extension: found by sniffing the content
    picture(500, 400, "no ext", (200, 120, 200)).save(p("untitled"),
                                                       format="PNG")

    # a corrupt "image" and a text file
    with open(p("broken.jpg"), "wb") as f:
        f.write(b"\xff\xd8\xff\xe0 this is not really a JPEG" * 20)
    with open(p("notes.txt"), "w") as f:
        f.write("Not an image.\n")

    for i, hue in enumerate([(200, 40, 40), (40, 200, 40), (40, 40, 200)]):
        picture(800, 600, f"More {i + 1}", hue).save(
            os.path.join(out, "More photos", f"more-{i + 1}.jpg"))

    print("created", out)


if __name__ == "__main__":
    main()
