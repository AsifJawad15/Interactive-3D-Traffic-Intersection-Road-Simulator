"""Builds the 10-slide presentation, delivery/slides/Smart_City_Digital_Traffic_Signals.pptx.

Every piece of text is an editable PowerPoint text box; the pictures come from
delivery/figures/jpg (python delivery/figures/make_figures.py) and the demo
video from delivery/video/demo.mp4 (python delivery/video/make_video.py).

Run:  python delivery/slides/build_slides.py
Needs: pip install python-pptx
"""

from pathlib import Path

from PIL import Image
from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_CONNECTOR, MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.util import Emu, Inches, Pt

HERE = Path(__file__).resolve().parent
FIGURES = HERE.parent / "figures" / "jpg"
VIDEO = HERE.parent / "video" / "demo.mp4"
OUT = HERE / "Smart_City_Digital_Traffic_Signals.pptx"

TITLE = "3D Smart City with Digital Traffic Signals"
COURSE = "CSE 4102: Graphics Lab"
AUTHOR = "Asif Jawad  |  Roll 2107007  |  Section A"

NAVY = RGBColor(0x0E, 0x16, 0x26)
PANEL = RGBColor(0x18, 0x23, 0x38)
YELLOW = RGBColor(0xFF, 0xD2, 0x1A)
WHITE = RGBColor(0xFF, 0xFF, 0xFF)
GREY = RGBColor(0xB8, 0xC2, 0xD3)
FONT = "Segoe UI"

W, H = Inches(13.333), Inches(7.5)


def fill(shape, color, transparency=None):
    shape.fill.solid()
    shape.fill.fore_color.rgb = color
    if transparency is not None:
        # python-pptx has no transparency setter; write the alpha element.
        from pptx.oxml.ns import qn
        solid = shape.fill._xPr.find(qn("a:solidFill"))
        clr = solid[0]
        alpha = clr.makeelement(qn("a:alpha"), {"val": str(int((1 - transparency) * 100000))})
        clr.append(alpha)
    shape.line.fill.background()


def text(slide, left, top, width, height, lines, size=20, color=WHITE, bold=False, align=PP_ALIGN.LEFT,
         anchor=MSO_ANCHOR.TOP, bullets=False, spacing=6):
    """A text box; `lines` is a list of strings or (string, dict of overrides)."""
    box = slide.shapes.add_textbox(left, top, width, height)
    frame = box.text_frame
    frame.word_wrap = True
    frame.vertical_anchor = anchor
    frame.margin_left = frame.margin_right = Inches(0.05)
    for index, line in enumerate(lines):
        options = {}
        if isinstance(line, tuple):
            line, options = line
        paragraph = frame.paragraphs[0] if index == 0 else frame.add_paragraph()
        paragraph.alignment = align
        paragraph.space_after = Pt(spacing)
        run = paragraph.add_run()
        run.text = (u"▸  " + line) if bullets and not options.get("plain") else line
        run.font.name = FONT
        run.font.size = Pt(options.get("size", size))
        run.font.bold = options.get("bold", bold)
        run.font.color.rgb = options.get("color", color)
    return box


def picture(slide, name, left, top, width=None, height=None):
    path = FIGURES / f"{name}.jpg"
    return slide.shapes.add_picture(str(path), left, top, width, height)


def picture_cover(slide, name, left, top, width, height):
    """Fills the box without stretching: crops the picture to its shape."""
    path = FIGURES / f"{name}.jpg"
    image_w, image_h = Image.open(path).size
    shape = slide.shapes.add_picture(str(path), left, top, width, height)
    box_ratio = width / height
    image_ratio = image_w / image_h
    if image_ratio > box_ratio:
        cut = (1 - box_ratio / image_ratio) / 2
        shape.crop_left = shape.crop_right = cut
    else:
        cut = (1 - image_ratio / box_ratio) / 2
        shape.crop_top = shape.crop_bottom = cut
    return shape


def label(slide, left, top, width, words, size=14, color=NAVY, back=YELLOW):
    shape = slide.shapes.add_shape(MSO_SHAPE.ROUNDED_RECTANGLE, left, top, width, Inches(0.42))
    fill(shape, back)
    frame = shape.text_frame
    frame.margin_top = frame.margin_bottom = Inches(0.02)
    paragraph = frame.paragraphs[0]
    paragraph.alignment = PP_ALIGN.CENTER
    run = paragraph.add_run()
    run.text = words
    run.font.name = FONT
    run.font.size = Pt(size)
    run.font.bold = True
    run.font.color.rgb = color
    return shape


def arrow(slide, x1, y1, x2, y2, color=YELLOW):
    line = slide.shapes.add_connector(MSO_CONNECTOR.STRAIGHT, x1, y1, x2, y2)
    line.line.color.rgb = color
    line.line.width = Pt(2.5)
    from pptx.oxml.ns import qn
    ln = line.line._get_or_add_ln()
    ln.append(ln.makeelement(qn("a:tailEnd"), {"type": "triangle", "w": "med", "len": "med"}))
    return line


def new_slide(prs, heading=None, kicker=None):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    background = slide.background.fill
    background.solid()
    background.fore_color.rgb = NAVY
    if heading:
        bar = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, 0, 0, Inches(0.16), Inches(1.05))
        fill(bar, YELLOW)
        text(slide, Inches(0.45), Inches(0.22), Inches(10.5), Inches(0.7), [heading], size=32, bold=True)
        if kicker:
            text(slide, Inches(9.2), Inches(0.32), Inches(3.8), Inches(0.5), [kicker], size=14, color=YELLOW,
                 align=PP_ALIGN.RIGHT, bold=True)
    return slide


def notes(slide, words):
    slide.notes_slide.notes_text_frame.text = words


def caption(slide, left, top, width, words, size=14):
    return text(slide, left, top, width, Inches(0.4), [words], size=size, color=GREY, align=PP_ALIGN.CENTER)


def main():
    prs = Presentation()
    prs.slide_width, prs.slide_height = W, H

    # 1. Title -----------------------------------------------------------------
    slide = new_slide(prs)
    picture_cover(slide, "rt_on_rain_night", 0, 0, W, H)
    shade = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, 0, Inches(3.9), W, Inches(3.6))
    fill(shade, NAVY, transparency=0.18)
    text(slide, Inches(0.7), Inches(4.1), Inches(12), Inches(0.5), [COURSE], size=24, color=YELLOW, bold=True)
    text(slide, Inches(0.7), Inches(4.65), Inches(12), Inches(1.2), [TITLE], size=44, bold=True)
    text(slide, Inches(0.7), Inches(5.75), Inches(12), Inches(0.5),
         ["An interactive OpenGL city with traffic, people, day and night, weather and ray tracing"],
         size=18, color=GREY)
    text(slide, Inches(0.7), Inches(6.45), Inches(12), Inches(0.5), [AUTHOR], size=20, bold=True)
    notes(slide, "Good morning. My project is a 3D Smart City with Digital Traffic Signals, written in C++ with "
                 "OpenGL 3.3. Everything you will see is built from code: there is no imported model. "
                 "The picture is the shopping street at night in the rain, with ray tracing on.")

    # 2. Demo video --------------------------------------------------------------
    slide = new_slide(prs, "Demo Video", "2 minutes")
    video_w = Inches(9.9)
    video_h = Emu(int(video_w * 9 / 16))
    left = int((W - video_w) / 2)
    poster = FIGURES / "tour_billboard.jpg"
    if VIDEO.exists():
        slide.shapes.add_movie(str(VIDEO), left, Inches(1.25), video_w, video_h,
                               poster_frame_image=str(poster), mime_type="video/mp4")
    else:
        picture(slide, "tour_billboard", left, Inches(1.25), video_w, video_h)
        text(slide, left, Inches(3.5), video_w, Inches(0.6), ["(run make_video.py, then rebuild the slides)"],
             size=18, align=PP_ALIGN.CENTER)
    caption(slide, Inches(0.5), Inches(6.95), W - Inches(1.0),
            "City fly-over  |  my car at a red light  |  people crossing  |  shading  |  day to night  |  "
            "weather  |  ray tracing", size=13)
    notes(slide, "Click the video to play it (2:00). It shows the moving and interactive parts: the camera "
                 "flies over the city, my car waits at a red light and drives on, the driver's view, people "
                 "crossing at the walk signal, the fountain changing between flat, Gouraud and Phong shading, "
                 "the time buttons from afternoon to night, the weather buttons to rain, and the ray-tracing "
                 "question answered Yes, with lamps and neon reflected in the wet road.")

    # 3. Features ----------------------------------------------------------------
    slide = new_slide(prs, "Features")
    text(slide, Inches(0.6), Inches(1.35), Inches(6.2), Inches(5.6), [
        "A closed city, 400 m across: 19 junctions",
        "Digital traffic signals: left arrow, green, yellow, all-red",
        "36 vehicles of 11 kinds, 80 people with walk signals",
        "Drive your own car, or walk; 7 camera views",
        "164 buildings, 255 trees, 194 street lamps, 47 neon signs",
        "5 time presets: sun, moon and shadows",
        "Weather: clear, cloudy, rain",
        "Ray tracing: reflections, soft shadows",
        "72 FPS at 1920 x 1080",
    ], size=19, bullets=True, spacing=9)
    # frame pipeline, as editable boxes
    steps = [("Input", "keys, mouse"), ("Simulation", "1/60 s steps"), ("Shadow maps", "sun / moon"),
             ("Scene pass", "lighting + shading"), ("Ray tracing", "reflections, AO"), ("Bloom + HUD", "to screen")]
    text(slide, Inches(7.4), Inches(1.3), Inches(5.4), Inches(0.4), ["Each frame"], size=16, color=YELLOW, bold=True)
    top = Inches(1.8)
    for index, (name, detail) in enumerate(steps):
        box = slide.shapes.add_shape(MSO_SHAPE.ROUNDED_RECTANGLE, Inches(8.2), top, Inches(3.8), Inches(0.62))
        fill(box, PANEL)
        box.line.color.rgb = YELLOW
        frame = box.text_frame
        paragraph = frame.paragraphs[0]
        paragraph.alignment = PP_ALIGN.CENTER
        run = paragraph.add_run()
        run.text = name
        run.font.name, run.font.size, run.font.bold, run.font.color.rgb = FONT, Pt(16), True, WHITE
        run = paragraph.add_run()
        run.text = "   " + detail
        run.font.name, run.font.size, run.font.color.rgb = FONT, Pt(12), GREY
        if index + 1 < len(steps):
            arrow(slide, Inches(10.1), top + Inches(0.62), Inches(10.1), top + Inches(0.84))
        top += Inches(0.84)
    notes(slide, "The features in short. The frame pipeline on the right: input, then the simulation in fixed "
                 "1/60 second steps so it behaves the same on every computer, then the shadow maps, the scene "
                 "pass where the lighting and shading happen, the optional ray-tracing passes, and last bloom and "
                 "the HUD. The traffic is collision-free by construction: 32 half-hour tests had zero collisions.")

    # 4. 3D transformations --------------------------------------------------------
    slide = new_slide(prs, "3D Transformations", "Lab 1 and 2")
    picture_cover(slide, "lineup_front", Inches(0.45), Inches(1.3), Inches(6.1), Inches(3.43))
    picture_cover(slide, "top_view", Inches(6.8), Inches(1.3), Inches(6.1), Inches(3.43))
    caption(slide, Inches(0.45), Inches(4.75), Inches(6.1), "11 vehicle kinds: translate + rotate + scale")
    caption(slide, Inches(6.8), Inches(4.75), Inches(6.1), "Top view: routes rotated onto every junction")
    text(slide, Inches(0.6), Inches(5.3), Inches(12.2), Inches(1.9), [
        ("Model-View-Projection:   clip = P · V · M · v,     M = T(position) · R(heading) · S(size)",
         {"bold": True}),
        "Hierarchical cars: wheels are children of the body; a wheel turns by Δθ = distance / radius",
        "One turn authored once, then rotated by 90°, 180°, 270° and moved to each junction",
    ], size=18, bullets=True)
    notes(slide, "Every object reaches the screen through the model, view and projection matrices. The model "
                 "matrix scales, rotates by the heading and translates. The vehicles are hierarchical: the wheels "
                 "are children of the body, they steer with atan(wheelbase / radius) and roll by distance over "
                 "radius: a sedan wheel of 0.33 m turns 174 degrees per metre. A turn is written once for the "
                 "northbound approach and rotated onto the other arms: the 2D rotation from Lab 1. "
                 "Right-turn radius 12 - 5.25 = 6.75 m, left-turn radius 12 + 1.75 = 13.75 m.")

    # 5. Illumination model ------------------------------------------------------------
    slide = new_slide(prs, "Illumination Model: Four Light Types", "Lab 3")
    img_w, img_h = Inches(8.4), Inches(4.725)
    img_left, img_top = Inches(0.45), Inches(1.25)
    picture(slide, "crossroads_night", img_left, img_top, img_w, img_h)

    def at(fx, fy):
        return img_left + int(img_w * fx), img_top + int(img_h * fy)

    callouts = [
        ("DIRECTIONAL: moon", (0.03, 0.03), (0.33, 0.20)),
        ("SPOT: billboard lamp", (0.02, 0.16), (0.29, 0.28)),
        ("EMISSIVE: neon", (0.47, 0.10), (0.44, 0.31)),
        ("POINT: street lamp", (0.72, 0.20), (0.79, 0.40)),
        ("SPOT: headlights", (0.40, 0.66), (0.50, 0.56)),
    ]
    for words, (lx, ly), target in callouts:
        x, y = at(lx, ly)
        tag = label(slide, x, y, Inches(2.3), words, size=12)
        tx, ty = at(*target)
        start_y = y + Inches(0.42) if ty > y else y
        arrow(slide, x + Inches(1.15), start_y, tx, ty)
    text(slide, Inches(9.05), Inches(1.2), Inches(4.0), Inches(5.9), [
        ("I = ambient + diffuse + specular", {"bold": True, "color": YELLOW, "size": 16}),
        ("Directional", {"bold": True, "color": YELLOW}),
        "Sun at noon (1.54, 1.50, 1.38), 59° up; moon (0.08, 0.09, 0.16)",
        ("Point (194 lamps)", {"bold": True, "color": YELLOW}),
        "1 / (1 + 0.09 d + 0.032 d²), reach 22 m",
        ("Spot", {"bold": True, "color": YELLOW}),
        "Billboard lamp 30° / 42°; headlights 10° / 26°",
        ("Emissive", {"bold": True, "color": YELLOW}),
        "Neon, windows, signals: strength 3.0",
    ], size=14, spacing=3)
    caption(slide, Inches(0.45), Inches(6.05), img_w, "The central crossroads at 22:00", size=13)
    notes(slide, "The illumination model is Phong's: ambient plus diffuse N dot L plus specular R dot V to the "
                 "power of the shininess, summed over the lights. All four light types are in this one frame. "
                 "Directional: the moon at night and the sun by day; at noon its colour is 1.54, 1.50, 1.38 from 59 "
                 "degrees up, which is why the day looks bright and white. Point: 194 street lamps with the lab's "
                 "attenuation 1, 0.09, 0.032; straight under a lamp the attenuation is 0.40, at 10 m 0.20. "
                 "Spot: the lamp under the GRAPHICS LAB billboard with cut-offs 30 and 42 degrees, and every "
                 "car's headlights, 10 and 26 degrees. Emissive: neon, lit windows and the signal lenses glow "
                 "with strength 3 and bloom.")

    # 6. Shading --------------------------------------------------------------------
    slide = new_slide(prs, "Shading: Flat, Gouraud, Phong", "Keys 1, 2, 3")
    names = [("fountain_flat", "Flat", "one normal per face"),
             ("fountain_gouraud", "Gouraud", "lit at vertices, colours interpolated"),
             ("fountain_phong", "Phong", "normals interpolated, lit per pixel")]
    width = Inches(4.05)
    height = Emu(int(width * 450 / 800))
    for index, (name, title, detail) in enumerate(names):
        left = Inches(0.45) + index * (width + Inches(0.2))
        picture(slide, name, left, Inches(1.6), width, height)
        label(slide, left + Inches(1.2), Inches(1.6) + height + Inches(0.2), Inches(1.65), title, size=18)
        caption(slide, left, Inches(1.6) + height + Inches(0.75), width, detail, size=14)
    text(slide, Inches(0.6), Inches(5.0), Inches(12.2), Inches(1.4), [
        "One shader pair, switched with uShadingMode while the city runs",
        "The Bezier fountain: flat shows every facet; Phong keeps the highlight sharp",
    ], size=18, bullets=True)
    notes(slide, "One shader pair does all three modes. Flat takes one normal per triangle from the screen "
                 "derivatives, so every facet of the fountain basin shows. Gouraud evaluates the lighting at the "
                 "vertices and interpolates the colours, which smears small highlights. Phong interpolates the "
                 "normal and lights every pixel. Example: with shininess 58, a highlight between two vertices "
                 "20 degrees off gets 0.03 with Gouraud and 1.0 with Phong.")

    # 7. Textures and curves -----------------------------------------------------------
    slide = new_slide(prs, "Texture Mapping and Bezier Surfaces", "Lab 4 and 5")
    tile_w = Inches(4.05)
    tile_h = Inches(2.9)
    tiles = [("street_level", "Asphalt: GL_REPEAT + mipmaps"),
             ("park_plaza", "Leaf cards: RGBA, alpha-tested"),
             ("fountain_phong", "Bezier surfaces of revolution")]
    for index, (name, words) in enumerate(tiles):
        left = Inches(0.45) + index * (tile_w + Inches(0.2))
        picture_cover(slide, name, left, Inches(1.35), tile_w, tile_h)
        caption(slide, left, Inches(4.3), tile_w, words, size=14)
    text(slide, Inches(0.6), Inches(4.95), Inches(12.2), Inches(2.3), [
        "Diffuse + specular map pair on the crates; windows drawn per pixel from texture coordinates",
        "Bezier curve  B(t) = Σ nCᵢ (1−t)ⁿ⁻ⁱ tⁱ Pᵢ ,  swept round the y axis",
        "Fountain, lamp posts, bins, bollards, monument; tree branches and car bodies from Bezier curves",
    ], size=18, bullets=True)
    notes(slide, "Textures follow the lab's loadTexture with explicit wrapping and filtering: the road repeats "
                 "one 80 m tile with GL_REPEAT and trilinear mipmaps; the crates carry the lab's diffuse and "
                 "specular map pair; tree leaves are RGBA cards cut out with an alpha test. The Bezier surfaces "
                 "use the Lab 5 nCr and Bernstein code with control points written in the source. Example: the "
                 "basin's 5 control points at t = 0.5 give radius 1.625 m at height 0.40 m; the rim is 2.10 m out, "
                 "so the basin is 4.2 m across.")

    # 8. Day-night and weather ---------------------------------------------------------
    slide = new_slide(prs, "Day, Night and Weather", "Sun, moon, shadows, rain")
    grid = [("time_morning", "Morning 07:00"), ("time_noon", "Noon 12:00"), ("time_evening", "Evening 18:30"),
            ("time_night", "Night 22:00"), ("weather_cloudy", "Cloudy"), ("weather_rain", "Rain")]
    cell_w, cell_h = Inches(4.05), Inches(2.52)
    for index, (name, words) in enumerate(grid):
        left = Inches(0.45) + (index % 3) * (cell_w + Inches(0.2))
        top = Inches(1.3) + (index // 3) * (cell_h + Inches(0.55))
        picture_cover(slide, name, left, top, cell_w, cell_h)
        caption(slide, left, top + cell_h + Inches(0.02), cell_w, words, size=14)
    notes(slide, "The directional light is a real sun path at 40 degrees north: sin h = sin(lat) sin(dec) + "
                 "cos(lat) cos(dec) cos(H). Morning 12 degrees up, shadows 4.7 m per metre of height; noon 59 "
                 "degrees, 0.6 m; evening 6 degrees, 8.9 m and orange light. At night the moon takes over and the "
                 "lamps, neon and windows come on. Shadows come from two shadow maps, 3 cm per texel near the "
                 "camera. The weather blends over 10 seconds: clouds with drifting shadows, then rain, wet roads "
                 "and puddles that mirror the sky by Fresnel's law.")

    # 9. Ray tracing --------------------------------------------------------------------
    slide = new_slide(prs, "Ray Tracing (Bonus)", "Partial, through the depth buffer")
    pair_w, pair_h = Inches(6.1), Inches(3.43)
    picture(slide, "rt_off_rain_night", Inches(0.45), Inches(1.3), pair_w, pair_h)
    picture(slide, "rt_on_rain_night", Inches(6.8), Inches(1.3), pair_w, pair_h)
    label(slide, Inches(0.65), Inches(1.45), Inches(1.4), "OFF", size=16, color=WHITE,
          back=RGBColor(0xB0, 0x2A, 0x2A))
    label(slide, Inches(7.0), Inches(1.45), Inches(1.4), "ON", size=16, color=NAVY,
          back=RGBColor(0x5A, 0xD1, 0x6A))
    small_w, small_h = Inches(2.9), Inches(1.63)
    picture(slide, "tour_rt_question", Inches(0.45), Inches(4.95), small_w, small_h)
    picture(slide, "tour_reflections_1", Inches(3.55), Inches(4.95), small_w, small_h)
    caption(slide, Inches(0.45), Inches(6.6), small_w, "The RAY TRACING ON? question", size=12)
    caption(slide, Inches(3.55), Inches(6.6), small_w, "Lamps and neon in the wet road", size=12)
    text(slide, Inches(6.8), Inches(4.9), Inches(6.1), Inches(2.4), [
        "Reflections: rays marched through the depth buffer, 32 steps + 5 halvings",
        "Ambient occlusion and contact shadows",
        "Soft shadows (sharp at the caster) and sun shafts",
        "About 1 ms per frame: still 72 FPS",
    ], size=16, bullets=True, spacing=5)
    notes(slide, "Ray tracing earns the bonus. OpenGL 3.3 has no ray-tracing hardware, so after the scene is "
                 "drawn, a reflected ray from each shiny pixel is marched through the depth buffer: 32 growing "
                 "steps, each 1.12 times the last, reaching about 90 m, then 5 halvings to find the hit. The wet "
                 "road then mirrors the bus's tail lights, the neon and the shops. The same passes give ambient "
                 "occlusion, contact shadows, soft shadows and sun shafts. It is switched on from the panel with a "
                 "confirmation and costs about 1 ms per frame.")

    # 10. Thank you --------------------------------------------------------------------
    slide = new_slide(prs)
    picture_cover(slide, "time_night", 0, 0, W, H)
    shade = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, 0, 0, W, H)
    fill(shade, NAVY, transparency=0.35)
    text(slide, 0, Inches(2.5), W, Inches(1.4), ["Thank You"], size=66, bold=True, align=PP_ALIGN.CENTER)
    text(slide, 0, Inches(3.9), W, Inches(0.6), ["Questions?"], size=28, color=YELLOW, align=PP_ALIGN.CENTER)
    text(slide, 0, Inches(5.6), W, Inches(0.5), [TITLE], size=20, align=PP_ALIGN.CENTER, bold=True)
    text(slide, 0, Inches(6.1), W, Inches(0.5), [AUTHOR + "  |  " + COURSE], size=16, color=GREY,
         align=PP_ALIGN.CENTER)
    notes(slide, "Thank you. I can run the project live: keys 1, 2, 3 for shading, O for the time presets, K for "
                 "the weather, F3 for ray tracing, C to drive my car.")

    prs.save(OUT)
    print(f"Wrote {OUT} ({len(prs.slides)} slides)")


if __name__ == "__main__":
    main()
