from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageOps


ROOT = Path(__file__).resolve().parents[1]
DOWNLOADS = Path.home() / "Downloads"
OUT_ROOT = ROOT / "dist" / "google-play" / "screenshots"


@dataclass(frozen=True)
class Source:
    slug: str
    label: str
    path: Path


PHONE_SOURCES = [
    Source("programs", "Programs", DOWNLOADS / "8fa64df9-4781-4b8e-8459-3db1a3ebd462.jpg"),
    Source("colors", "Colors", DOWNLOADS / "4160123e-4c3d-4c6c-9f56-4dc02aebf4ce.jpg"),
    Source("effects", "Effects", DOWNLOADS / "68383189-0358-44c8-875d-e9c3648d9ee1.jpg"),
    Source("settings", "Settings", DOWNLOADS / "2e2ef383-4097-4042-9e45-e30cfd9b6681 (1).jpg"),
]


def dashboard_source() -> Path:
    matches = sorted(DOWNLOADS.glob("*193226*.png"))
    if not matches:
        raise FileNotFoundError("Could not find the dashboard screenshot with 193226 in Downloads.")
    return matches[0]


def cover(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    scale = max(size[0] / image.width, size[1] / image.height)
    resized = image.resize((round(image.width * scale), round(image.height * scale)), Image.Resampling.LANCZOS)
    left = (resized.width - size[0]) // 2
    top = (resized.height - size[1]) // 2
    return resized.crop((left, top, left + size[0], top + size[1]))


def contain(image: Image.Image, size: tuple[int, int], margin: int = 0) -> Image.Image:
    target = (size[0] - margin * 2, size[1] - margin * 2)
    scale = min(target[0] / image.width, target[1] / image.height)
    return image.resize((round(image.width * scale), round(image.height * scale)), Image.Resampling.LANCZOS)


def soft_background(source: Image.Image, size: tuple[int, int]) -> Image.Image:
    background = cover(source, size).filter(ImageFilter.GaussianBlur(34))
    overlay = Image.new("RGB", size, "#080808")
    return Image.blend(background, overlay, 0.58)


def save_jpeg(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.convert("RGB").save(path, "JPEG", quality=94, subsampling=0, optimize=True)


def make_portrait(source: Image.Image, size: tuple[int, int]) -> Image.Image:
    source = source.convert("RGB")
    output = soft_background(source, size)
    foreground = contain(source, size)
    x = (size[0] - foreground.width) // 2
    y = (size[1] - foreground.height) // 2

    shadow = Image.new("RGBA", (foreground.width + 48, foreground.height + 48), (0, 0, 0, 0))
    shadow_draw = ImageDraw.Draw(shadow)
    shadow_draw.rectangle((24, 24, 24 + foreground.width, 24 + foreground.height), fill=(0, 0, 0, 115))
    shadow = shadow.filter(ImageFilter.GaussianBlur(18))
    output.paste(shadow.convert("RGB"), (x - 24, y - 24), shadow)
    output.paste(foreground, (x, y))
    return output


def make_landscape_dashboard(source: Image.Image, size: tuple[int, int]) -> Image.Image:
    source = source.convert("RGB")
    output = soft_background(source, size)
    foreground = contain(source, size, margin=64)
    x = (size[0] - foreground.width) // 2
    y = (size[1] - foreground.height) // 2

    shadow = Image.new("RGBA", (foreground.width + 70, foreground.height + 70), (0, 0, 0, 0))
    shadow_draw = ImageDraw.Draw(shadow)
    shadow_draw.rounded_rectangle((35, 35, 35 + foreground.width, 35 + foreground.height), radius=22, fill=(0, 0, 0, 150))
    shadow = shadow.filter(ImageFilter.GaussianBlur(22))
    output.paste(shadow.convert("RGB"), (x - 35, y - 35), shadow)

    panel = Image.new("RGB", (foreground.width + 18, foreground.height + 18), "#111111")
    panel_draw = ImageDraw.Draw(panel)
    panel_draw.rounded_rectangle((0, 0, panel.width - 1, panel.height - 1), radius=18, fill="#111111", outline="#2e2e2e", width=2)
    output.paste(panel, (x - 9, y - 9), rounded_rect_mask(panel.size, 18))
    output.paste(foreground, (x, y))
    return output


def rounded_rect_mask(size: tuple[int, int], radius: int) -> Image.Image:
    mask = Image.new("L", size, 0)
    draw = ImageDraw.Draw(mask)
    draw.rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius=radius, fill=255)
    return mask


def prepare_portrait_set(folder: str, size: tuple[int, int], prefix: str) -> list[Path]:
    paths: list[Path] = []
    for index, source in enumerate(PHONE_SOURCES, start=1):
        if not source.path.exists():
            raise FileNotFoundError(source.path)
        image = ImageOps.exif_transpose(Image.open(source.path))
        output = make_portrait(image, size)
        destination = OUT_ROOT / folder / f"{prefix}-{index:02d}-{source.slug}-{size[0]}x{size[1]}.jpg"
        save_jpeg(output, destination)
        paths.append(destination)
    return paths


def prepare_dashboard(folder: str, size: tuple[int, int], prefix: str) -> Path:
    image = ImageOps.exif_transpose(Image.open(dashboard_source()))
    output = make_landscape_dashboard(image, size)
    destination = OUT_ROOT / folder / f"{prefix}-00-dashboard-{size[0]}x{size[1]}.jpg"
    save_jpeg(output, destination)
    return destination


def verify(path: Path) -> str:
    with Image.open(path) as image:
        width, height = image.size
    ratio = width / height
    ok_ratio = abs(ratio - 9 / 16) < 0.002 or abs(ratio - 16 / 9) < 0.002
    ok_size = path.stat().st_size <= 8 * 1024 * 1024
    return f"{path} | {width}x{height} | {path.stat().st_size} bytes | ratio={'ok' if ok_ratio else 'bad'} | size={'ok' if ok_size else 'bad'}"


def main() -> None:
    outputs: list[Path] = []
    outputs += prepare_portrait_set("phone", (1080, 1920), "phone")
    outputs.append(prepare_dashboard("tablet-7", (1920, 1080), "tablet7"))
    outputs += prepare_portrait_set("tablet-7", (1080, 1920), "tablet7")
    outputs.append(prepare_dashboard("tablet-10", (2560, 1440), "tablet10"))
    outputs += prepare_portrait_set("tablet-10", (1440, 2560), "tablet10")

    for path in outputs:
        print(verify(path))


if __name__ == "__main__":
    main()
