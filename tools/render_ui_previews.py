#!/usr/bin/env python3
"""Render README previews from the current 640x172 LVGL layout constants."""

from __future__ import annotations

from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "main" / "assets"
OUTPUT = ROOT / "docs" / "screenshots"
SCALE = 3
WIDTH, HEIGHT = 640, 172

COLORS = {
    "bg": "#030A18",
    "bg_grad": "#071A35",
    "panel": "#10233F",
    "panel_dark": "#09172B",
    "text": "#F7F9FD",
    "muted": "#91A8C9",
    "chinese": "#397EE8",
    "chinese_dark": "#173E78",
    "math": "#F1B914",
    "math_dark": "#775000",
    "english": "#8068E3",
    "english_dark": "#302A6B",
    "break": "#39CBA7",
    "break_dark": "#0C5A58",
    "danger": "#F16E75",
    "danger_dark": "#983B45",
    "wifi": "#36B8C7",
    "wifi_dark": "#0A4E60",
    "weather": "#39BCE7",
    "weather_dark": "#0A416B",
}


def rgb(value: str) -> tuple[int, int, int]:
    value = value.lstrip("#")
    return tuple(int(value[index:index + 2], 16) for index in (0, 2, 4))


def mix(first: str, second: str, amount: float) -> tuple[int, int, int]:
    first_rgb, second_rgb = rgb(first), rgb(second)
    return tuple(round(first_rgb[index] * (1 - amount) + second_rgb[index] * amount)
                 for index in range(3))


def sx(value: float) -> int:
    return round(value * SCALE)


def box(values: Iterable[float]) -> tuple[int, int, int, int]:
    return tuple(sx(value) for value in values)  # type: ignore[return-value]


def font_path(bold: bool) -> Path:
    candidates = [
        ROOT / "build" / "NotoSansSC-Bold.ttf" if bold else Path("C:/Windows/Fonts/simhei.ttf"),
        Path("C:/Windows/Fonts/simhei.ttf"),
        Path("C:/Windows/Fonts/msyh.ttc"),
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise FileNotFoundError("No Chinese font found for README preview rendering")


def ui_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(font_path(bold)), sx(size))


def vertical_gradient(top: str, bottom: str, width: int, height: int) -> Image.Image:
    top_rgb, bottom_rgb = rgb(top), rgb(bottom)
    strip = Image.new("RGB", (1, height))
    pixels = []
    for y in range(height):
        amount = y / max(1, height - 1)
        pixels.append(tuple(round(top_rgb[i] * (1 - amount) + bottom_rgb[i] * amount)
                            for i in range(3)))
    strip.putdata(pixels)
    return strip.resize((width, height)).convert("RGBA")


def screen() -> Image.Image:
    image = vertical_gradient(COLORS["bg_grad"], COLORS["bg"], sx(WIDTH), sx(HEIGHT))
    draw = ImageDraw.Draw(image)
    stars = [(18, 15), (92, 9), (178, 39), (312, 12), (426, 25),
             (533, 8), (612, 34), (76, 147)]
    for index, (x, y) in enumerate(stars):
        radius = 2 if index % 3 == 0 else 1
        color = "#4D87DD" if index % 2 == 0 else "#36C6B6"
        draw.ellipse(box((x - radius, y - radius, x + radius, y + radius)), fill=color)
    return image


def rounded_gradient(image: Image.Image, xy: tuple[int, int, int, int],
                     top: str, bottom: str, radius: int = 14,
                     border: str | None = None, border_width: int = 2,
                     shadow: bool = True) -> None:
    x0, y0, x1, y1 = box(xy)
    width, height = x1 - x0, y1 - y0
    mask = Image.new("L", (width, height), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, width - 1, height - 1),
                                            radius=sx(radius), fill=255)
    if shadow:
        shadow_layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
        colored = Image.new("RGBA", (width, height), (*rgb(top), 80))
        shadow_mask = mask.filter(ImageFilter.GaussianBlur(sx(4)))
        shadow_layer.paste(colored, (x0, y0 + sx(2)), shadow_mask)
        image.alpha_composite(shadow_layer)
    gradient = vertical_gradient(top, bottom, width, height)
    image.paste(gradient, (x0, y0), mask)
    stroke = border or top
    ImageDraw.Draw(image).rounded_rectangle((x0, y0, x1 - 1, y1 - 1),
                                             radius=sx(radius), outline=stroke,
                                             width=sx(border_width))


def center_text(image: Image.Image, xy: tuple[int, int, int, int], text: str,
                size: int, color: str = COLORS["text"], bold: bool = False) -> None:
    x0, y0, x1, y1 = box(xy)
    draw = ImageDraw.Draw(image)
    draw.text(((x0 + x1) // 2, (y0 + y1) // 2), text, font=ui_font(size, bold),
              fill=color, anchor="mm")


def left_text(image: Image.Image, x: int, y: int, text: str, size: int,
              color: str = COLORS["text"], bold: bool = False) -> None:
    ImageDraw.Draw(image).text((sx(x), sx(y)), text, font=ui_font(size, bold),
                               fill=color, anchor="la")


def paste_icon(image: Image.Image, name: str, x: int, y: int, size: int) -> None:
    icon = Image.open(ASSETS / name).convert("RGBA")
    target = sx(size)
    ratio = min(target / icon.width, target / icon.height)
    icon = icon.resize((round(icon.width * ratio), round(icon.height * ratio)),
                       Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (target, target), (0, 0, 0, 0))
    canvas.alpha_composite(icon, ((canvas.width - icon.width) // 2,
                                  (canvas.height - icon.height) // 2))
    image.alpha_composite(canvas, (sx(x), sx(y)))


def line(image: Image.Image, xy: tuple[int, int, int, int], color: str,
         width: int = 1) -> None:
    ImageDraw.Draw(image).line(box(xy), fill=color, width=sx(width))


def button(image: Image.Image, xy: tuple[int, int, int, int], text: str,
           top: str, bottom: str, size: int = 16) -> None:
    rounded_gradient(image, xy, top, bottom, radius=12, border=top, shadow=False)
    center_text(image, xy, text, size, bold=True)


def slider(image: Image.Image, x: int, y: int, width: int, value: int,
           accent: str) -> None:
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle(box((x, y, x + width, y + 10)), radius=sx(5),
                           fill=COLORS["panel"])
    fill_width = max(10, round(width * value / 100))
    draw.rounded_rectangle(box((x, y, x + fill_width, y + 10)), radius=sx(5), fill=accent)
    knob_x = x + fill_width
    draw.ellipse(box((knob_x - 7, y - 3, knob_x + 7, y + 13)), fill=COLORS["text"])


def render_home() -> Image.Image:
    image = screen()
    center_text(image, (8, 6, 143, 34), "开始写作业", 24, bold=True)
    center_text(image, (8, 37, 143, 75), "17:10", 32, bold=True)
    center_text(image, (8, 77, 143, 99), "9月4日 周五", 16, COLORS["muted"])
    line(image, (23, 107, 128, 107), COLORS["chinese"], 1)
    center_text(image, (8, 113, 143, 139), "选择科目开始", 16, COLORS["break"])
    draw_wifi_mark(image, 36, 150, COLORS["break"], 8)
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle(box((58, 147, 76, 158)), radius=sx(2),
                           outline=COLORS["break"], width=sx(2))
    draw.rectangle(box((76, 150, 79, 155)), fill=COLORS["break"])
    draw.rectangle(box((61, 150, 72, 155)), fill=COLORS["break"])
    left_text(image, 94, 146, "95%", 14, COLORS["muted"], True)

    subjects = [
        (146, "语文", "本周82分钟", "ui_icon_book.png", "chinese", "chinese_dark"),
        (264, "数学", "本周96分钟", "ui_icon_compass.png", "math", "math_dark"),
        (382, "英语", "本周64分钟", "ui_icon_english.png", "english", "english_dark"),
        (500, "休息", "本周58分钟", "ui_icon_cup.png", "break", "break_dark"),
    ]
    for x, title, subtitle, icon, accent, dark in subjects:
        rounded_gradient(image, (x, 10, x + 110, 162), COLORS[accent], COLORS[dark])
        icon_size = 64 if title != "休息" else 56
        paste_icon(image, icon, x + (110 - icon_size) // 2, 6 if title != "休息" else 14,
                   icon_size)
        center_text(image, (x, 76, x + 110, 104), title, 20, bold=True)
        center_text(image, (x, 111, x + 110, 137), subtitle, 16, bold=True)
    return image


def render_report() -> Image.Image:
    image = screen()
    center_text(image, (8, 12, 140, 42), "本周学习报告", 20, bold=True)
    center_text(image, (8, 49, 140, 73), "9月4日 周五", 16, COLORS["chinese"])
    center_text(image, (8, 76, 140, 100), "每周3次", 16, COLORS["muted"])
    button(image, (20, 115, 128, 154), "返回", COLORS["chinese"], COLORS["chinese_dark"])
    line(image, (143, 17, 143, 155), COLORS["muted"])

    rings = [
        (151, "语文", "82分钟", "ui_icon_book.png", "chinese", "chinese_dark", .61),
        (249, "数学", "96分钟", "ui_icon_compass.png", "math", "math_dark", .71),
        (347, "英语", "64分钟", "ui_icon_english.png", "english", "english_dark", .47),
    ]
    draw = ImageDraw.Draw(image)
    for x, title, value, icon, accent, dark, progress in rings:
        arc_box = box((x + 9, 9, x + 81, 81))
        draw.arc(arc_box, 0, 359, fill=COLORS[dark], width=sx(6))
        draw.arc(arc_box, -90, -90 + round(360 * progress), fill=COLORS[accent], width=sx(6))
        paste_icon(image, icon, x + 21, 21, 48)
        center_text(image, (x, 83, x + 90, 110), title, 18, bold=True)
        center_text(image, (x, 115, x + 90, 141), value, 16, COLORS[accent])

    line(image, (445, 17, 445, 155), COLORS["muted"])
    paste_icon(image, "ui_icon_focus.png", 443, 6, 28)
    left_text(image, 488, 13, "专注", 16, COLORS["break"])
    center_text(image, (450, 39, 538, 70), "4时02分", 18, COLORS["break"], True)
    line(image, (455, 78, 532, 78), COLORS["muted"])
    paste_icon(image, "ui_icon_cup.png", 443, 79, 28)
    left_text(image, 488, 88, "休息", 16, COLORS["break"])
    center_text(image, (450, 112, 538, 145), "58分钟", 18, COLORS["break"], True)
    line(image, (542, 17, 542, 155), COLORS["muted"])
    paste_icon(image, "ui_icon_star.png", 556, 5, 72)
    center_text(image, (547, 88, 636, 113), "完成12段", 16, COLORS["math"])
    center_text(image, (547, 117, 636, 149), "真棒!", 20, COLORS["math"], True)
    return image


def render_schedule() -> Image.Image:
    image = screen()
    center_text(image, (9, 6, 124, 35), "本周课程表", 20, bold=True)
    center_text(image, (10, 40, 122, 67), "周四", 20, COLORS["math"], True)
    center_text(image, (10, 73, 122, 99), "9月3日", 16, COLORS["muted"])
    button(image, (18, 111, 114, 153), "返回", COLORS["panel"], COLORS["panel_dark"])
    courses = ["语文", "英语", "数学", "体育", "劳动", "美术", "科学"]
    palette = {
        "语文": ("chinese", "chinese_dark"), "英语": ("english", "english_dark"),
        "数学": ("math", "math_dark"), "美术": ("math", "math_dark"),
        "体育": ("break", "break_dark"), "劳动": ("break", "break_dark"),
        "科学": ("break", "break_dark"),
    }
    draw = ImageDraw.Draw(image)
    for index, course in enumerate(courses):
        x = 128 + index * 72
        accent, dark = palette[course]
        rounded_gradient(image, (x, 9, x + 66, 162), COLORS[accent], COLORS[dark], 12)
        draw.ellipse(box((x + 13, 26, x + 53, 66)),
                     fill=mix(COLORS[accent], COLORS["text"], .27))
        center_text(image, (x + 13, 26, x + 53, 66), str(index + 1), 20, bold=True)
        line(image, (x + 19, 81, x + 47, 81), COLORS["text"], 2)
        center_text(image, (x + 3, 98, x + 63, 127), course, 18, bold=True)
    return image


def draw_wifi_mark(image: Image.Image, cx: int, cy: int, color: str, size: int = 26) -> None:
    draw = ImageDraw.Draw(image)
    for radius, start in ((size, 205), (round(size * .65), 205), (round(size * .32), 205)):
        draw.arc(box((cx - radius, cy - radius, cx + radius, cy + radius)), start, 335,
                 fill=color, width=sx(3))
    draw.ellipse(box((cx - 2, cy + 2, cx + 2, cy + 6)), fill=color)


def render_wifi() -> Image.Image:
    image = screen()
    rounded_gradient(image, (8, 8, 120, 164), COLORS["break"], COLORS["panel_dark"])
    draw_wifi_mark(image, 64, 31, COLORS["break"], 20)
    center_text(image, (8, 39, 120, 60), "Wi-Fi", 16, bold=True)
    ImageDraw.Draw(image).ellipse(box((21, 76, 29, 84)), fill=COLORS["break"])
    left_text(image, 34, 67, "已连接", 16, COLORS["break"])
    center_text(image, (16, 90, 112, 111), "HomeStudy", 14)
    button(image, (13, 127, 62, 158), "断开", COLORS["danger"], COLORS["danger_dark"], 14)
    button(image, (66, 127, 115, 158), "返回", COLORS["panel"], COLORS["panel_dark"], 14)
    left_text(image, 136, 9, "选择网络", 20, bold=True)
    button(image, (520, 7, 630, 42), "扫描", COLORS["chinese"], COLORS["chinese_dark"])
    networks = [
        (136, "HomeStudy", True, "break"),
        (300, "Qingdao-WiFi", False, "break"),
        (464, "Guest_2.4G", False, "english"),
    ]
    for x, name, connected, accent in networks:
        rounded_gradient(image, (x, 50, x + 154, 162), COLORS["panel"],
                         COLORS["panel_dark"], border=COLORS[accent],
                         border_width=2 if connected else 1, shadow=connected)
        draw_wifi_mark(image, x + 77, 76, COLORS[accent], 17)
        center_text(image, (x + 11, 99, x + 143, 126), name, 15)
        if connected:
            center_text(image, (x + 8, 132, x + 146, 154), "已连接", 14,
                        COLORS["break"])
        else:
            draw = ImageDraw.Draw(image)
            for bar in range(3):
                draw.rounded_rectangle(box((x + 67 + bar * 8, 146 - bar * 3,
                                            x + 72 + bar * 8, 150)), radius=sx(2),
                                       fill=COLORS[accent])
    return image


def render_alarm() -> Image.Image:
    image = screen()
    center_text(image, (12, 12, 125, 42), "学习闹钟", 20, bold=True)
    center_text(image, (8, 49, 129, 74), "闹钟已开启", 16, COLORS["break"])
    center_text(image, (8, 81, 129, 105), "板载扬声器响铃", 16, COLORS["muted"])
    button(image, (17, 119, 120, 158), "返回", COLORS["panel"], COLORS["panel_dark"])
    button(image, (148, 10, 220, 44), "+", COLORS["chinese"], COLORS["chinese_dark"], 20)
    center_text(image, (137, 53, 231, 114), "06", 48, bold=True)
    button(image, (148, 122, 220, 156), "−", COLORS["panel"], COLORS["panel_dark"], 20)
    center_text(image, (224, 60, 254, 107), ":", 32, bold=True)
    button(image, (258, 10, 330, 44), "+", COLORS["chinese"], COLORS["chinese_dark"], 20)
    center_text(image, (247, 53, 341, 114), "00", 48, bold=True)
    button(image, (258, 122, 330, 156), "−", COLORS["panel"], COLORS["panel_dark"], 20)
    rounded_gradient(image, (362, 16, 626, 156), COLORS["english"], COLORS["english_dark"])
    left_text(image, 374, 26, "启用闹钟", 16)
    rounded_gradient(image, (549, 26, 605, 56), COLORS["break"], COLORS["break_dark"],
                     radius=15, shadow=False)
    ImageDraw.Draw(image).ellipse(box((580, 28, 606, 54)), fill=COLORS["text"])
    left_text(image, 374, 73, "重复", 16, COLORS["muted"])
    button(image, (451, 69, 605, 108), "工作日", COLORS["chinese"], COLORS["chinese_dark"])
    center_text(image, (374, 120, 605, 146), "分钟按5分钟调整", 16, COLORS["muted"])
    return image


def render_settings() -> Image.Image:
    image = screen()
    cards = [
        (8, "ui_icon_settings_duration.png", "每科时长", "60/50/35", "math", "math_dark"),
        (134, "ui_icon_settings_volume.png", "音量 72%", "", "break", "break_dark"),
        (260, "ui_icon_settings_brightness.png", "屏幕 77%", "", "weather", "weather_dark"),
        (386, "ui_icon_settings_lock.png", "自动锁屏", "10分钟", "english", "english_dark"),
        (512, "ui_icon_settings_back.png", "返回", "", "chinese", "chinese_dark"),
    ]
    for x, icon, title, value, accent, dark in cards:
        rounded_gradient(image, (x, 8, x + 120, 164), COLORS[accent], COLORS[dark])
        icon_y = 28 if title == "返回" else 5
        paste_icon(image, icon, x + 30, icon_y, 60)
        if title == "每科时长":
            center_text(image, (x + 4, 66, x + 116, 92), title, 20, bold=True)
            center_text(image, (x + 4, 96, x + 116, 121), value, 20, bold=True)
            center_text(image, (x + 4, 126, x + 116, 150), "语  数  英", 16,
                        COLORS["math"])
        elif title.startswith("音量"):
            center_text(image, (x + 4, 74, x + 116, 103), title, 20, bold=True)
            slider(image, x + 14, 124, 92, 72, COLORS["break"])
        elif title.startswith("屏幕"):
            center_text(image, (x + 4, 77, x + 116, 105), title, 20, bold=True)
            slider(image, x + 14, 132, 92, 77, COLORS["math"])
        elif title == "自动锁屏":
            center_text(image, (x + 4, 71, x + 116, 100), title, 20, bold=True)
            button(image, (x + 10, 111, x + 110, 145), value,
                   COLORS["wifi"], COLORS["wifi_dark"], 16)
        else:
            center_text(image, (x + 4, 103, x + 116, 134), title, 20, bold=True)
    return image


def render_weather() -> Image.Image:
    image = screen()
    center_text(image, (8, 7, 132, 39), "三日天气", 20, bold=True)
    center_text(image, (8, 39, 132, 64), "青岛 · 已更新", 15, COLORS["muted"])
    button(image, (12, 66, 120, 108), "刷新", COLORS["weather"], COLORS["weather_dark"])
    button(image, (12, 116, 120, 160), "返回", COLORS["chinese"], COLORS["chinese_dark"])
    forecasts = [
        (140, "今天 周五", "晴", "22 ~ 27 C", "ui_icon_weather_sunny.png", "weather", "weather_dark"),
        (305, "明天 周六", "多云", "21 ~ 26 C", "ui_icon_weather_partly_cloudy.png", "chinese", "chinese_dark"),
        (470, "后天 周日", "小雨", "20 ~ 24 C", "ui_icon_weather_rain.png", "english", "english_dark"),
    ]
    for x, date, condition, temp, icon, accent, dark in forecasts:
        rounded_gradient(image, (x, 9, x + 155, 163), COLORS[accent], COLORS[dark])
        center_text(image, (x + 5, 5, x + 150, 33), date, 16, bold=True)
        paste_icon(image, icon, x + 47, 31, 60)
        center_text(image, (x + 5, 88, x + 150, 112), condition, 16, bold=True)
        center_text(image, (x + 5, 116, x + 150, 143), temp, 18, COLORS["muted"], True)
    return image


def save(name: str, image: Image.Image) -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS).convert("RGB").save(
        OUTPUT / f"{name}.png", optimize=True
    )


def main() -> None:
    pages = {
        "home": render_home,
        "report": render_report,
        "schedule": render_schedule,
        "alarm": render_alarm,
        "wifi": render_wifi,
        "settings": render_settings,
        "weather": render_weather,
    }
    for name, renderer in pages.items():
        save(name, renderer())
        print(OUTPUT / f"{name}.png")


if __name__ == "__main__":
    main()
