"""Turns the full-size screenshots into the JPEG figures the report and the
slides use (delivery/figures/jpg), cropping where a figure needs a close-up.

Run after capture_all.sh and the tour stills:
    sh delivery/figures/capture_all.sh
    x64/Release/OpenGLMiniProject.exe --tour-stills delivery/figures/tour 8,16,26,30,44,54,60,63,66.5,72,80,88,103,108,112,116
    python delivery/figures/make_figures.py
"""

from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
OUT = HERE / "jpg"

# name: (source, crop box in 1920x1080 pixels or None)
FIGURES = {
    # screenshots from --capture
    **{name: (f"shots/{name}.png", None) for name in [
        "city_overview", "top_view", "lineup_front", "lineup_back", "crossroads_day", "bus_stop",
        "street_level", "ring_road", "crossroads_night", "street_level_night", "shopping_night",
        "sky_night", "park_plaza", "petrol_station", "houses", "on_foot", "time_morning", "time_noon",
        "time_evening", "time_night", "weather_clear", "weather_cloudy", "weather_rain",
        "rain_night_driver", "people_waiting", "walk_signal", "zebra", "roundabout_r2",
        "rt_off_rain_night", "rt_on_rain_night", "rt_off_noon", "rt_on_noon", "rt_question",
        "hud_chase", "hud_driver"]},
    # the same pair, cropped to the road where the reflections are
    "rt_off_rain_night_road": ("shots/rt_off_rain_night.png", (0, 380, 1920, 1080)),
    "rt_on_rain_night_road": ("shots/rt_on_rain_night.png", (0, 380, 1920, 1080)),
    # stills from the demo tour (--tour-stills)
    "tour_billboard": ("tour/tour_008.00.png", None),
    "tour_city": ("tour/tour_016.00.png", None),
    "tour_red_light": ("tour/tour_026.00.png", None),
    "tour_green_light": ("tour/tour_030.00.png", None),
    "tour_driver_view": ("tour/tour_044.00.png", None),
    "tour_people": ("tour/tour_054.00.png", None),
    "tour_evening": ("tour/tour_072.00.png", None),
    "tour_night_city": ("tour/tour_080.00.png", None),
    "tour_neon_street": ("tour/tour_088.00.png", None),
    "tour_rt_question": ("tour/tour_103.00.png", None),
    "tour_reflections_1": ("tour/tour_108.00.png", None),
    "tour_reflections_2": ("tour/tour_112.00.png", None),
    "tour_reflections_3": ("tour/tour_116.00.png", None),
    # the Bezier fountain in the three shading modes, close up
    "fountain_flat": ("tour/tour_060.00.png", (560, 330, 1360, 780)),
    "fountain_gouraud": ("tour/tour_063.00.png", (560, 330, 1360, 780)),
    "fountain_phong": ("tour/tour_066.50.png", (500, 300, 1300, 750)),
}


def main():
    OUT.mkdir(exist_ok=True)
    for name, (source, box) in FIGURES.items():
        image = Image.open(HERE / source).convert("RGB")
        if box:
            image = image.crop(box)
        if image.width > 1600:
            image = image.resize((1600, round(image.height * 1600 / image.width)), Image.LANCZOS)
        image.save(OUT / f"{name}.jpg", quality=88, optimize=True)
    print(f"Wrote {len(FIGURES)} figures to {OUT}")


if __name__ == "__main__":
    main()
