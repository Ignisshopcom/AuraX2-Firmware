from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageOps


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "dist" / "google-play"
ICON_SOURCE = ROOT / "pwa" / "icons" / "ignis-512.png"
MARK_SOURCE = ROOT / "android" / "AuraXFinder" / "app" / "src" / "main" / "res" / "drawable" / "ignis_mark.png"
PROGRAMS_SCREENSHOT = Path.home() / "Downloads" / "8fa64df9-4781-4b8e-8459-3db1a3ebd462.jpg"

FONT_DIR = Path("C:/Windows/Fonts")
FONT_REGULAR = FONT_DIR / "segoeui.ttf"
FONT_BOLD = FONT_DIR / "segoeuib.ttf"


def font(path: Path, size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(path), size=size)


def rounded_mask(size: tuple[int, int], radius: int) -> Image.Image:
    mask = Image.new("L", size, 0)
    draw = ImageDraw.Draw(mask)
    draw.rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius=radius, fill=255)
    return mask


def paste_rounded(base: Image.Image, image: Image.Image, xy: tuple[int, int], radius: int) -> None:
    mask = rounded_mask(image.size, radius)
    base.paste(image, xy, mask)


def text(draw: ImageDraw.ImageDraw, xy: tuple[int, int], value: str, face: ImageFont.FreeTypeFont, fill: str) -> None:
    draw.text(xy, value, font=face, fill=fill)


def phone_screen_from_photo(size: tuple[int, int]) -> Image.Image:
    if not PROGRAMS_SCREENSHOT.exists():
        return Image.new("RGB", size, "#111111")

    source = ImageOps.exif_transpose(Image.open(PROGRAMS_SCREENSHOT)).convert("RGB")
    scale = size[0] / source.width
    resized = source.resize((size[0], round(source.height * scale)), Image.Resampling.LANCZOS)
    top = 0
    if resized.height > size[1]:
        top = min(92, resized.height - size[1])
    return resized.crop((0, top, size[0], top + size[1]))


def make_app_icon() -> Path:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    destination = OUT_DIR / "app-icon-512.png"
    icon = Image.open(ICON_SOURCE).convert("RGBA")
    if icon.size != (512, 512):
        icon = icon.resize((512, 512), Image.Resampling.LANCZOS)
    icon.save(destination, optimize=True)
    return destination


def make_feature_graphic() -> Path:
    width, height = 1024, 500
    image = Image.new("RGB", (width, height), "#080808")
    pixels = image.load()

    for y in range(height):
        for x in range(width):
            vx = x / (width - 1)
            vy = y / (height - 1)
            orange = max(0, 1 - math.hypot((vx - 0.13) * 1.1, (vy - 0.42) * 1.9))
            blue = max(0, 1 - math.hypot((vx - 0.83) * 2.0, (vy - 0.18) * 2.5))
            r = int(8 + 42 * orange + 5 * blue)
            g = int(8 + 15 * orange + 18 * blue)
            b = int(8 + 5 * orange + 34 * blue)
            pixels[x, y] = (r, g, b)

    draw = ImageDraw.Draw(image)

    for i in range(23):
        x = -4 + i * 47
        top = 392 + int(22 * math.sin(i * 0.62))
        color = (255, 106 + (i % 5) * 18, 32)
        glow = Image.new("RGBA", (76, 76), (0, 0, 0, 0))
        glow_draw = ImageDraw.Draw(glow)
        glow_draw.rounded_rectangle((17, 17, 56, 56), radius=8, fill=(*color, 170))
        glow = glow.filter(ImageFilter.GaussianBlur(8))
        image.paste(glow.convert("RGB"), (x - 19, top - 19), glow)
        draw.rounded_rectangle((x, top, x + 34, top + 34), radius=7, fill=color)
        draw.rounded_rectangle((x, top, x + 34, top + 34), radius=7, outline=(255, 188, 107), width=1)

    for i in range(9):
        x = 560 + i * 48
        draw.line((x, 54, x + 104, 398), fill=(255, 104, 32), width=1)

    mark = Image.open(MARK_SOURCE).convert("RGBA").resize((142, 142), Image.Resampling.LANCZOS)
    glow = Image.new("RGBA", (206, 206), (0, 0, 0, 0))
    glow.alpha_composite(mark.resize((168, 168), Image.Resampling.LANCZOS), (19, 19))
    glow = glow.filter(ImageFilter.GaussianBlur(18))
    glow_tint = Image.new("RGBA", glow.size, (255, 112, 29, 0))
    glow_tint.putalpha(glow.getchannel("A").point(lambda p: int(p * 0.55)))
    image.paste(glow_tint.convert("RGB"), (72, 84), glow_tint)

    tile = Image.new("RGB", (184, 184), "#101010")
    tile_draw = ImageDraw.Draw(tile)
    tile_draw.rounded_rectangle((0, 0, 183, 183), radius=34, fill="#101010", outline="#303030", width=2)
    paste_rounded(image, tile, (86, 92), 34)
    image.paste(mark, (107, 113), mark)

    title_font = font(FONT_BOLD, 60)
    subtitle_font = font(FONT_REGULAR, 29)
    chip_font = font(FONT_BOLD, 20)

    text(draw, (304, 102), "AuraX Finder", title_font, "#fff4ea")
    text(draw, (308, 176), "Find devices. Control light shows.", subtitle_font, "#e7d7ca")

    for idx, label in enumerate(("Local Wi-Fi", "Sync", "No login")):
        widths = (122, 72, 102)
        x = 308 + sum(widths[:idx]) + idx * 14
        draw.rounded_rectangle((x, 242, x + widths[idx], 282), radius=20, fill="#1b1b1b", outline="#40332a")
        text(draw, (x + 15, 250), label, chip_font, "#ff9a4d")

    phone = Image.new("RGB", (260, 422), "#080808")
    phone_draw = ImageDraw.Draw(phone)
    phone_draw.rounded_rectangle((0, 0, 259, 421), radius=36, fill="#080808", outline="#3b3b3b", width=3)
    screen = phone_screen_from_photo((226, 374))
    paste_rounded(phone, screen, (17, 24), 24)
    phone_draw.rounded_rectangle((88, 12, 172, 18), radius=3, fill="#373737")

    phone_shadow = Image.new("RGBA", (318, 480), (0, 0, 0, 0))
    shadow_draw = ImageDraw.Draw(phone_shadow)
    shadow_draw.rounded_rectangle((24, 24, 284, 446), radius=38, fill=(0, 0, 0, 170))
    phone_shadow = phone_shadow.filter(ImageFilter.GaussianBlur(16))
    image.paste(phone_shadow.convert("RGB"), (700, 24), phone_shadow)
    paste_rounded(image, phone, (724, 22), 36)

    destination = OUT_DIR / "feature-graphic-1024x500.png"
    image.save(destination, optimize=True)
    return destination


def main() -> None:
    app_icon = make_app_icon()
    feature = make_feature_graphic()
    for path in (app_icon, feature):
        with Image.open(path) as img:
            print(f"{path} {img.size[0]}x{img.size[1]} {img.mode} {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
