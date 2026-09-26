# 3D Smart City with Digital Traffic Signals

An interactive city traffic simulation in modern OpenGL.

**Delivery:** the printed report is [`delivery/report/report.pdf`](delivery/report/report.pdf)
(23 pages, with worked calculations, every object's dimensions and the test
results); the 10 slides and the 2-minute demo video are rebuilt from the
scripts in `delivery/` (see *Delivery* below).

**CSE 4102 — Computer Graphics and Image Processing Laboratory**
Asif Jawad · Roll 2107007 · Section A

A small closed city, 400 m across, laid out like a maze: a core of six junctions
inside a ring road, joined to it by six links. Every junction has its own
permanent type: two **signalised crossroads**, two **roundabouts** (one with an
animated Bezier fountain), a **signalised T-junction**, seven **give-way
T-junctions**, two more signalised T-junctions on the ring, and five bends. No
road leaves town: the same cars drive round the city for ever, choosing a new
turn at every junction, with nobody appearing or disappearing.

You have a car of your own, a yellow one parked beside the central crossroads.
Press `C` and the camera follows it; drive it from a chase view above and behind
it or from the driver's view over the bonnet, get out (`F`) and walk. The AI
traffic yields to you, and you bump into buildings, trees, posts and cars
instead of passing through them.

The blocks are lived in: 164 buildings in six styles (shop rows, blocks of
flats, office towers, hotels, houses and warehouses), 103 shop fronts with
awnings and signs, a park with a pond and a plaza, a petrol station, a car park,
255 trees that sway in the wind, benches, bins and planters, and a hazy skyline
beyond the fields. At night thousands of rooms light up behind the windows.

A day–night cycle drives the sun, 194 street and park lamps, 47 neon signs,
lit shop windows and eight billboards, and the shading model can be switched
between flat, Gouraud and Phong while the simulation runs. The sun follows a
real mid-latitude path and the moon the opposite arc; five time presets
(Morning, Noon, Afternoon, Evening, Night) are a key press or a click away, and
the sun glides to them so that every shadow in the city sweeps round with it.
The weather is Clear, Cloudy or Rain (`K`, or a click): clouds drift over the
sky and their shadows over the streets, and in the rain the streets get wet,
puddles fill, people put up umbrellas and drivers slow down with their
headlights on. A **ray-tracing button** (or `F3`) asks "RAY TRACING ON?" in
the middle of the screen; *Yes* turns on partial ray tracing: rays marched
through the depth buffer give reflections in the wet streets, puddles, paint
and glass, ambient occlusion and contact shadows, soft sun shadows and sun
shafts.

The traffic is **collision-free by construction**: every place where two routes
could touch is measured once at start-up, and a vehicle only enters a junction
after claiming all of those places on its way (see *How the traffic works*). A
headless soak test drives the traffic for half an hour of simulated time and
checks the real vehicle outlines every step.

---

## Building

Requires Visual Studio 2022 or later with the C++ desktop workload. Dependencies
(GLFW, GLM, GLAD configured for OpenGL 3.3 Core, stb) come from the vcpkg
manifest `vcpkg.json` and are restored automatically by the build.

Open `OpenGLMiniProject.slnx` and build **x64 / Debug** or **x64 / Release**, or
from a terminal:

```
MSBuild.exe OpenGLMiniProject.vcxproj -p:Configuration=Release -p:Platform=x64
```

Run the executable **from the project directory**, not from `x64\Release`, so
that `shaders/` and `assets/` resolve. `--cars N` sets the number of cars
(default 36, at most 40).

---

## Controls

| Key | Action |
| --- | --- |
| `W` `A` `S` `D` | Move the free camera (or drive / walk, see below) |
| `Q` / `E` | Move down / up |
| `Shift` | Move the free camera four times faster |
| Mouse | Look around |
| **`C`** | **Lock the camera onto your car (or you on foot); press again to leave it** |
| Arrows (or `W` `A` `S` `D`) | Drive your car, or walk |
| `Space` | Handbrake |
| `Shift` (driving / on foot) | Boost to 80 km/h / run |
| `V` | On your car: chase view ↔ driver view over the bonnet. Otherwise: the driver view of the AI car being followed |
| `B` (hold) | Look back, in the driver view |
| `F` | Get out of the car / get back in (stand next to it) |
| `M` | Top view of the whole city, and back |
| `Tab` | Follow the next AI car |
| `Shift`+`Tab` | Walk behind the next person; `V` then looks through their eyes |
| `P` | Pause / resume |
| `1` `2` `3` | Flat / Gouraud / Phong shading |
| **`O`** | **Next time preset: Morning 07:00, Noon 12:00, Afternoon 15:30, Evening 18:30, Night 22:00** (the sun glides there) |
| **Click a time button** | The same presets, from the panel in the top-right corner |
| **`[` / `]`** | **Time back / on by one hour** (the sun glides) |
| `T` | Toggle the automatic day–night cycle |
| `Y` / `N` | Jump to noon / 22:00 |
| `L` | Toggle the night lights (lamps, neon, billboards) |
| **`K`** | **Weather: Clear → Cloudy → Rain → Clear** (every change blends over 10 s) |
| **Click a weather button** | The same, from the row under the time buttons |
| `R` | Reset camera, traffic, time and weather |
| `H` | Show / hide the control panel |
| **`F2`** | **Shadows: high (two maps) / low (the city map only) / off** |
| **`F3`** or **click the ray-tracing button** | **Asks "RAY TRACING ON?" in the middle of the screen: `Y` / `Enter` / *Yes* turns it on, `N` / *No* turns it off, `Esc` (or a click beside it) closes it unchanged** |
| `Alt` (hold) | Show the cursor while the mouse is looking round (free camera, on foot, driver view), to click the time, weather and ray-tracing buttons |
| `F5` | Frame-time graph (last 240 frames) |
| `F6` | Resolution: automatic / always native / always 720p inside the window |
| `F7` | Frame pacing: steady (every second refresh on a 120 Hz+ screen) / full rate |
| `F11` | Fullscreen |
| `Esc` | Exit |

The cursor is free in the views that do not look with the mouse (top, follow,
chase, the AI driver's seat, following a person), so the time, weather and
ray-tracing buttons can be clicked there directly. While the ray-tracing
question is open the cursor is free in every view.

The window opens at 1920×1080, or maximised when the screen is only 1080p tall
(`F11` then gives true fullscreen 1080p; `--fullscreen` starts that way). The old
`M` key that switched one junction between signals and a roundabout is gone
(every junction now keeps its own type); `M` is now the top view.

---

## Where each lab topic is used

| Lab | Topic | Where it lives |
| --- | --- | --- |
| **1** | 2D primitives, 2D transformations | `Route::rotated` and `Route::translated` (`Route.cpp`) apply the 2D rotation and translation that place one authored northbound route onto every arm of every junction. Lane markings, zebras, arrows and stop lines in `RoadRenderer.cpp` are rectangles rotated onto the direction of their road. |
| **2** | 3D drawing, camera, model / view / projection | `Mesh.cpp` builds indexed VAO/VBO/EBO geometry (with a colour per vertex) and `MeshBuilder` bakes the whole road network into six meshes and the whole city dressing into a few dozen; `Camera.cpp` provides seven camera modes (free, top, AI follow and driver, your chase view, driver view and your own eyes) using `glm::lookAt` and `glm::perspective`; `Scene::render` uploads `uModel`, `uView` and `uProjection` every frame. |
| **3** | Illumination model and shading | `shaders/scene.frag` implements ambient + one directional light (the sun by day, the moon by night), **shadowed through shadow maps** (`ShadowMap.cpp`, `shaders/shadows.glsl`) + attenuated point lights (`k_c = 1`, `k_l = 0.09`, `k_q = 0.032`, the lab's constants) + **one spot light** with cosine cut-off angles: the lamp on an arm under the billboard at the central crossroads, which lights its picture at night. The four Lab 3 lamps at the central crossroads are always lit; the other street lamps, the neon spill and the billboard glow share a budget of 32 lights per frame (`LightManager.cpp`, `shaders/lights.glsl`), and at night the headlights of the 8 vehicles that matter most are spot lights with the same cosine cut-offs. `uShadingMode` selects flat, Gouraud or Phong from one shader pair. |
| **4** | Texture mapping | `Texture::fromFile(path, wrapS, wrapT, minFilter, magFilter)` mirrors the lab's `loadTexture` signature, so wrapping and filtering are explicit at every call site. The roadside crates carry the lab's own **diffuse + specular map pair** (`container2.png`, `container2_specular.png`), sampled as `uDiffuseTexture` and `uSpecularTexture`; grass and leaves use a dim one-texel specular map so they stay matte. The trees' leaf cards sample an **RGBA** leaf picture and are **alpha-tested** (`Texture::fromRgba` builds its mipmaps so the leaves keep their coverage with distance). |
| **4b** | Texture sources | `assets/asphalt-photoreal.png` and the container pair are real image files. `assets/grass.png` and `assets/sidewalk.png` are optional: if present they are loaded, and if absent the matching procedural generator in `Texture.cpp` is used instead, so the project runs with no assets at all. The billboard pictures and the leaf picture are drawn at start-up; building windows are drawn by the fragment shader. |
| **5** | Bezier curves and surfaces | `Mesh::makeBezierRevolution` (`Mesh.cpp`) ports `nCr` and the Bernstein evaluation from the Lab 5 curve program and sweeps the resulting profile about the Y axis. Control points are written in source (top of `Scene.cpp`) instead of picked with the mouse. It generates the **fountain basin and column, the street lamp posts and sign posts, the bins, bollards, planter shrubs and water-tank roofs, and the plaza's monument**. The tree trunks and branches are tubes swept along 3D Bezier curves (`TreeGenerator.cpp`, the same curve by de Casteljau's construction), and the vehicle bodies are lofted from Bezier profiles. |

---

## The city

```
  z=+200  NWc ----- G4 ------ ST2 ------------------ NEc
           |         |         |                      |
  z=+100   |        G2 ------- X1 ----- NE bend       |
           |         |         |           |          |
  z=   0   |         G ------- X0 ------- R1 ------- G5
           |         |         |           |          |
  z=-100  G7 ------- ST ------ R2 -------- G3 ------ G6
           |                   |                      |
  z=-200  SWc --------------- ST3 ------------------ SEc

        x=-200    -100         0         +100       +200
```

`X` = signalised crossroads, `R` = roundabout (R1 has the fountain), `ST` =
signalised T-junction, `G` = give-way T-junction (the straight road through it
has priority), `NWc`… = bends. `RoadNetwork::makeCity` (`RoadNetwork.cpp`)
describes this once, as pure geometry; the traffic builds its routes from it and
the renderer builds its meshes from it, so the painted lanes and the driven
lanes cannot disagree.

Every road has two lanes each way (3.5 m), kerbs, 4.5 m sidewalks and street
lamps every 15 m on alternate sides. The ten **blocks** between the roads are
described as the grid cells they cover: squares, long rectangles, and an L that
wraps round the outside of the NE bend. `RoadNetwork::traceOutline` walks the
boundary of the cells and turns each corner into the right kerb shape: a fillet
at an intersection, the inside or the outside of a bend, or the circle of a
roundabout. The same tracing, run the other way round the whole city, gives the
kerb and sidewalk outside the ring road. Lawns are triangulated by ear clipping,
since an L-shaped block is not convex.

---

## The city dressing

Everything in the blocks is placed by a seeded generator (`WorldCity.cpp`), so
the same city comes out every run, and baked at start-up into one mesh per
material (`PropRenderer.cpp`), so the whole city costs a few dozen draw calls.

* **Streets of buildings.** Both sides of every road are walked plot by plot.
  Each block has a land use (downtown shops, offices, flats, houses, mixed) and
  the row outside the ring has one per side (flats, towers, warehouses,
  houses). A plot is kept only if it lies on the lawn behind the sidewalk,
  clear of every other building, of the billboards, bus shelters, signals and
  signs, and of the corners of the junctions.
* **Six styles:** shop rows (shops on a 4.2 m ground floor, one or two storeys
  above), blocks of flats (3–6 storeys, a door and canopy), office towers
  (6–12 storeys of curtain wall, a glazed lobby, planters), hotels (a lobby, a
  canopy, palms, and the name in neon along the top), houses (a pitched roof,
  a porch, a drive, often a car on it) and warehouses (roller doors and an
  apron). Roofs carry parapets, water tanks and air-conditioning units; two
  carry billboards.
* **Windows without geometry.** A wall's texture coordinates count window bays
  across and storeys up; the fragment shader draws a framed window in every
  cell, varies the glass by day, and at night lights a share of the rooms, warm
  or cool, chosen by a hash of the cell and the building's own seed. Far away,
  where a window is smaller than a pixel, the pattern fades to its average
  instead of shimmering.
* **Shops:** a glass front with mullions and a door, a striped awning, and a
  sign: 43 in neon (every stroke of the text a glowing tube, which blooms and
  lights the pavement), the others painted on a lit box. The windows glow at
  night, each shop in its own light.
* **The park** (the north-west block): a stone-edged pond, a paved plaza with a
  bronze monument, palms and benches, paths out to all four sides, lamps,
  picnic tables and 34 trees. **The petrol station** (east of R1): a lit
  canopy over two pump islands, a kiosk and a price sign. **The car park**
  (south of X0): painted bays and parked cars.
* **Trees** (`TreeGenerator.cpp`): broadleaf, conifer and palm, three shapes of
  each. Trunks and branches are Bezier tubes; the foliage is leaf cards, whose
  normals lean outward from the middle of the crown so it is lit as one
  shape. Every vertex carries how far it sways, and the vertex shader moves it
  in the wind. Street trees line the roads wherever there is room.
* **Street furniture:** benches, bins, planters, bollards, picnic tables and
  50 km/h signs on the links out to the ring; 49 parked cars, baked with the
  vehicles' own bodies into four meshes.
* **The skyline:** 90 plain towers 400–700 m out, drawn with extra haze.
* Everything solid is solid: you bump into buildings, benches, pumps and
  parked cars. `--self-test` checks that everything stands on the lawn, clear
  of the buildings and off the road.

---

## How the geometry works

### Routes are lines and arcs

`Route` (`Route.h` / `Route.cpp`) stores a path as a chain of straight segments
and circular arcs and samples it **by arc length**. Two formulas do all the work:

* straight: `P(s) = A + s·d̂`, heading constant;
* arc: `θ(s) = θ₀ ± s / r` (because `s = r·Δθ`), `P = C + r·(cos θ, sin θ)`, with
  the heading taken from the tangent.

The simulation therefore integrates exactly one number per vehicle — the distance
travelled. Position, heading, and whether the car is turning left or right all
fall out of sampling the route, so **a turning car is not a special case**; it is
the same update evaluated on a curved segment. Each sample also returns the
curvature `1/r`: the front wheels steer to `atan(wheelbase / r)` and the
cornering speed is `v = √(a_lat · r)`.

A route runs from the middle of one road, through a junction, to the middle of
the next road, where the next junction's routes begin. A car simply chains from
route to route.

### Four-fold symmetry

Every route is authored once for the northbound approach and rotated by 90°, 180°
and 270° onto the junction's real arms, then moved to the junction. That removes
three quarters of the geometry and three quarters of the chances to get it wrong.

### Turn radii come from tangency, not from taste

A right turn must touch the incoming lane centre and the outgoing lane centre at
the road edge, which forces `radius = edge − lane`. The wide left turn gives
`radius = edge + lane`.

For the roundabout, the entry arc curves right while the circulating ring curves
left, so their circles are **externally tangent**: the distance between centres is
`ringRadius + entryRadius`. Fixing the entry centre one entry radius to the
driver's right of the approach lane pins its `x`, and tangency solves for its `z`.
The merge point is then simply the point on the line joining the two centres.

A **lane change** is an S of two opposite arcs of equal radius. Each covers half
the length `l` and half the sideways shift `d`, which fixes the radius
`r = (l² + d²) / 2d`; the heading never jumps.

---

## How the traffic works

### Lanes and route choice

At a crossroads or T-junction the inner lane goes straight on or left and the
outer lane straight on or right; at a roundabout the outer lane takes the first
and second exits and the inner lane the second and third. A car going straight on
may also move over into the other lane once it is through the junction (between
16 and 40 m past its centre). That is the only place cars change lane, and in a
closed city it matters: round the ring road one lane only ever goes straight on,
so without it a car there could never leave. At every junction a car picks its
next route at random, weighted away from exits many cars are already heading for,
and half as likely when it means changing lane.

### Conflict zones (measured once, `TrafficBuild.cpp`)

For every pair of routes of the same junction that do not start in the same lane,
both routes are sampled every 25 cm near the middle, a car-sized box (plus a
30 cm margin) is placed at each sample, and every pair of positions at which the
two boxes overlap is marked. Each connected patch of marks is a **conflict
zone**, stored as an interval of car-centre distance on each route. The key
property: while a car's centre is outside its interval, no car on the other route
can touch it, wherever that car is. The city has 220 routes and 822 zones; the
lane changes are measured the same way as any crossing.

Routes that run along the same line — a shared approach lane, a shared exit lane,
a shared stretch of the ring — are found the same way and stored as **shared
spans**. On a shared span the cars simply follow each other.

The stop line of each route is moved, if needed, to just before its first zone,
so a waiting car can never be touched by crossing traffic, and the painted line
is drawn exactly there.

### Commit and claim (`Simulation.cpp`)

A car approaching the junction **commits** only when, in one check:

* its signal allows it (on yellow, only if it cannot stop comfortably — or if it
  is a car or van already waiting at the front to turn, clearing the junction;
  a truck or bus pulls away too slowly and waits for the next green);
* the car in front of it in its lane has already committed (first in, first through);
* there is room for it beyond the junction, so it never blocks the box;
* nobody on a crossing route holds a claim on any zone on its way — unless the car
  is certain to be out of that zone before the claimant could possibly arrive;
* no car with priority could reach a shared zone within 3.5 s. Priority goes to
  straight on over a right turn over a left turn, to the major road at a give-way
  T-junction, to circulating traffic at a roundabout, and to a car keeping its
  lane over one moving into it. Equal priority is served in turn: the car that has
  waited longer goes first, and after 20 s at the line a car makes crossing
  traffic hold back for it.

On commit it **claims every zone on its way at once**, and releases each one as
soon as its centre has left that zone. A committed car never waits for a claim —
it only follows the car in front — so there is no hold-and-wait and therefore no
deadlock; and two cars on crossing routes are never inside a shared zone together,
so there is no collision. A car that committed on green but has not reached the
line when the light changes gives its claims back and stops, if it comfortably can.

### Car following

Speed comes from the **Intelligent Driver Model**: acceleration
`a·[1 − (v/v₀)⁴ − (s*/s)²]` with `s* = s₀ + vT + vΔv / 2√(ab)`, where the gap `s`
is measured bumper to bumper **along the route**, across shared spans and on into
the next route. An unclaimed stop line acts as a stationary car. Corners ahead
are braked for evenly, from `v² = v_c² + 2ad`. The **sigmoid easing** limits how
quickly the acceleration itself may change (the jerk), and a hard clamp keeps
every car at least 0.6 m behind the one in front whatever the model says.

### Smooth motion

The simulation always advances in fixed 1/60 s steps, so traffic behaves the
same on any machine. Drawing that state directly made cars step unevenly on a
144 Hz screen (moving on some frames, standing still on others). Each vehicle
now keeps its pose from the previous step, and every frame draws the blend
`previous + (current − previous)·α`, where `α` is how far the clock has run into
the next step (**render interpolation**). The follow camera rides a critically
damped spring, and the driver camera is rigidly attached to the blended pose.

On a 120 Hz+ screen the default **steady pacing** draws on every second refresh:
on this hybrid-GPU laptop, full-rate frames at 144 Hz often miss a refresh and
alternate between 6.9 and 13.9 ms, which reads as stutter; every-second-refresh
gives an even 72 FPS. `F7` switches to full rate. The scene can also be rendered
at 67 % (720p inside a 1080p window) and scaled up with light sharpening; in
automatic mode that happens only if the frame rate stays below 55 FPS for 3 s.

### Signals

Every signalised junction has its own controller, and they start on alternating
axes so the city does not change colour all at once. Each axis gets a protected **left-turn arrow** (only when someone at the
front of a lane wants to turn left), then green for everyone with left turns
giving way, then yellow and a 2.5 s **all-red** clearance. Green is actuated: it
ends early once its own queue is empty and someone waits across, and it never
runs past 16 s while anyone waits.

### Night lights

The city is lit by street lamps, signboards and neon; there is no floodlight.

* **Street lamps** every 15 m along every road. The four Lab 3 lamps of the
  central crossroads are always lit.
* **Neon signs** above the doors round the central crossroads (HOTEL, CAFE,
  PIZZA, CINEMA, BAR, 24H) and over the shops along the streets, and hotel
  names along the tops of the hotels: 47 in all. The lettering is geometry:
  every stroke of the `stb_easy_font` text becomes a thin glass tube, which
  glows and blooms at night (a few tired tubes stutter). Each sign by a door
  throws a coloured light onto the pavement below it.
* **Lit rooms and shop windows**: at night a share of the windows light up,
  shop windows glow with the light inside, and sign boxes shine.
* **The petrol station's canopy** and the lamps of the park and the car park.
* **Billboards** on the lawns beside the roads, their pictures generated at
  start-up (a gradient, a frame and two lines of text). At night five of them
  glow from behind in their own colours and light the ground in front; the one
  at the central crossroads is lit from the front by the Lab 3 spot light on
  an arm below it.

All of these share a budget of 32 lights per frame, chosen by distance among
those whose reach is on screen. Each light's reach ends in a smooth window at
exactly 22 m (11–13 m for the signs), and the farthest chosen lights fade out
over the last fifth of the lighting distance, so lights come and go without
popping; `--light-test` measures this. Lamps too far away to light the ground
still glow and bloom.

### Sun, moon and shadows

The sky is that of a city at 40° north in late spring (`DayNight.cpp`). The
sun's position comes from its hour angle and declination: it rises in the
east, climbs to 59° in the south at noon, and sets in the west just before
19:00, so even noon has short shadows. The moon is full and runs the opposite
arc, up all night.

| Preset | Time | Sun or moon | Shadows |
| --- | --- | --- | --- |
| Morning | 07:00 | sun 12° up in the east | long, pointing west (4.7 m per metre of height) |
| Noon | 12:00 | sun 59° up in the south | short, pointing north (0.6 m per metre) |
| Afternoon | 15:30 | sun 40° up in the south-west | pointing north-east (1.2 m per metre) |
| Evening | 18:30 | sun 6° up in the west; lamps and neon on | very long, pointing east (8.9 m per metre) |
| Night | 22:00 | moon 25° up in the south-east | faint, soft, blue |

Changing the time never jumps. The sun and moon glide to the new time over
3 s (4.5 s at most for a jump from night to noon, 1.2 s for an hour), eased
in and out, and each clock minute counts in the glide by how much it moves
the sun *and* how much it changes the light, so the glide slows down over
sunrise and sunset instead of flashing through them. Sunlight is warm and
dim while the sun is low. It passes through black at the horizon, and the
moon takes over as the light that casts shadows only below that. The two
never shine at once, so the switch cannot be seen.

**Shadow maps.** Each frame the city is drawn from the light into two depth
maps (`ShadowMap.cpp`, `shaders/shadow.vert/.frag`):

* **near**: 2048×2048 over a 64 m square round the camera, 3 cm a texel. It
  follows the camera in steps of exactly one texel, so every texel always
  covers the same patch of ground and shadow edges never crawl as you move;
* **city**: 4096×4096 over the whole city, about 12 cm a texel. It only
  changes when the light moves, and covers everything else, out to the top
  view.

A point uses the near map where it has one and fades to the city map across
the near map's edge. Sixteen hardware depth-compare taps on a grid fixed in the
map give a soft, stable edge (wider in moonlight). The point is pushed out
along its normal by about a texel first, more where the light grazes it, which
stops acne without lifting shadows off their casters. The ground, roads and
paving are never drawn into the maps; they only receive shadows. Leaf cards
are alpha-tested in the shadow pass too, so trees throw dappled shadows. The
wind that sways them and the ripples on the fountain move the shadows as
well, because both passes place every vertex with the same code
(`shaders/placement.glsl`). A vehicle or person just out of view is still
drawn into the maps when its shadow falls into view. You on foot cast a
shadow even when looking through your own eyes, and so does your whole car
in the driver view. Shadows work in all three shading modes. In Gouraud the
sun is lit at the vertices but shadowed per pixel. `F2` switches between both
maps, the city map only, and none. The HUD shows what they cost.

**Headlights.** After dusk, and in the rain even by day, every vehicle's
headlights are one cone of light, aimed just below level. The 8 cones nearest the camera that reach into view
light the road (your own car first when you drive it); the farthest fade out
as others come nearer, like the lamps.

### Weather: clouds and light rain

`Weather.cpp` holds the state (Clear, Cloudy or Rain) and blends every value
to a new one over 10 s, eased, so the sky never snaps. Going towards rain the
clouds gather first and the rain starts once they have; going away from it
the rain stops first. There is no storm and no fog.

| | Clear | Cloudy | Rain |
| --- | --- | --- | --- |
| Cloud cover | a few wisps | broken cloud | overcast |
| Sunlight | full | full between the clouds | a fifth of it, everywhere |
| Cloud shadows | none | drifting patches | none (all grey) |
| Rain | — | — | 5,000 streaks, wet streets, puddles |

**Clouds (`shaders/clouds.glsl`, `sky.frag`).** The clouds are a flat sheet
1.5 km up. For each sky pixel the view ray meets the sheet, and fractal noise
(six octaves of value noise, fewer far away) there gives the cloud's
thickness; the cover lowers the threshold the noise must pass. The wind
carries the sheet slowly east-north-east. A second sample, taken 220 m
towards the sun, says how much cloud the light passes through: edges facing
the sun are bright, thick middles dark underneath, and thin edges near the
sun get a silver lining. The light on them is the sun's, which reaches them a
little after sunset for the street (pink and orange at Evening), or the
moon's, which turns them blue-grey at night. They hide the sun, the moon and
the stars behind them, and far away they thin into the horizon haze.

**Cloud shadows.** Every lit surface looks up at the same sheet along the
light and dims by the cloud it finds there, so a cloud's shadow lies exactly
under the cloud as seen from the sun and drifts with it. Under an overcast
sky the sun is dimmed everywhere instead, the sky's soft light rises and
greys, shadows soften, and the horizon haze thickens a little in the rain
(`DayNight::setWeather`).

**Rain (`Rain.cpp`, `shaders/rain.vert/.frag`).** Up to 5,000 streaks, one
instanced draw call, in a 36 m box that follows the camera. Each drop's place
is fixed in the world and only wraps round the box, so moving through the
rain passes the drops instead of carrying them along. A streak is stretched
along its fall and leans with the wind. It is lit by the sky and, per drop,
by the lamps, neon and headlights in the light budget, so rain glitters under
a street lamp and in a headlight beam. Streaks are added onto the scene and
tested against its depth.

**The wet world (`shaders/scene_body.glsl`).** Wetness builds over about
40 s of rain and dries over two minutes after it; puddles fill over about a
minute and a half and are the last to go (four minutes). Wet surfaces are
darker and glossier, the more so the more they face the sky, and only as far
as their material allows (grass and leaves stay dull). Puddles lie in the
dips of the roads and paving (a noise mask): nearly black, flat, and
mirroring the sky and the cloud sheet by Fresnel's law, from 2 % looking
straight down to nearly all of it at a grazing angle. The lamps' and
headlights' highlights stay on top of the mirror, so they shine in the
puddles at night. Near the camera, rings spread where drops land, on the
road, the pavements and the car roofs alike. The weather's shading lives in a
second program (`scene_wet.frag`): the extra code makes the shader slower even
where it is skipped, so dry weather under a clear sky uses the smaller one.

**Life in the rain.** People put up umbrellas one after another as it starts
and walk up to 10 % faster. Drivers drive 15 % slower and keep gaps 35 %
longer, and switch their headlights on. In the driver view the wipers sweep
the lower part of the view (the view sits at the top of the windscreen, so
they sweep a pane just in front of the eyes, from pivots under the bonnet's
rear edge).

### Ray tracing (Enhanced mode): partial, through the depth buffer

OpenGL 3.3 has no ray-tracing hardware, and tracing rays through the whole
city in software would not hold 60 FPS. So this is *partial* ray tracing:
after the city is drawn, rays are marched through its **depth buffer**, which
holds the nearest surface under every pixel. What the screen shows, the rays
can hit; what it does not (the back of a building, anything off screen),
they cannot. `Enhanced.cpp` runs the passes; `F3` or the button under the
weather asks first.

**The question.** Clicking the button (or `F3`) dims the screen and asks
"RAY TRACING ON?" in the middle of it, with what it adds and what it costs.
*Yes* (`Y`, `Enter`) turns it on, *No* (`N`) turns it off, and `Esc` or a
click beside the box closes it with nothing changed. While it is open the
cursor is free in every view.

| Effect | Shader | Resolution | How |
| --- | --- | --- | --- |
| Reflections | `ssr.frag` | half | A ray reflected off every surface that is partly a mirror, marched in 32 growing steps until it passes just behind the depth buffer, then refined by halving the last step five times. |
| Ambient occlusion | `ssao.frag`, `ssao_blur.frag` | half | 12 points over the half ball above each surface; those behind the depth buffer are shut in. A 5 × 5 depth-aware blur smooths the grain. |
| Contact shadows | `ssao.frag` | half | 10 steps up the sunbeam, under a metre: the thin shadows under tyres, feet and kerbs that the shadow map is too coarse to hold. |
| Soft shadows (PCSS) | `shadows.glsl` (`SOFT_SHADOWS`) | full | A search in the near shadow map finds how far above a point its casters are; the edge is then filtered that wide, so a shadow is sharp where it touches its caster and softens away from it. |
| Sun shafts | `sunshafts_mask.frag`, `sunshafts.frag` | quarter | The open sky near a low sun is gathered along the line from each pixel to the sun; buildings and trees in the way cast dark beams through it. |
| Bloom | `PostProcess.cpp` | — | 35 % stronger, so neon and lamps glow a little more. |

**What is a mirror.** The scene shader writes into the colour's spare alpha
channel how much of each pixel is a mirror: polished paint and glass a
little (more at a grazing angle, by Fresnel's law), window panes more, wet
roads and puddles as much as they already mirror the sky. The reflection
pass reads it, so no second render target is needed; the sky and the rain
write 0. Where a ray hits, what it found replaces the sky the surface was
reflecting; where it leaves the screen, turns back towards the camera or
reaches its end, the reflection fades out and the sky stays. On a wet road
(a rough mirror) the reflection is smeared up and down, which gives the long
streaks of lamps and neon on a wet street at night; a puddle stays sharp.

**Normals from depth.** The passes rebuild each pixel's position and normal
from the depth buffer alone. On each axis the neighbour nearer in depth is
used, so at an object's edge the normal is the object's own.

**Depth-aware upsampling.** The half-resolution results are brought up in the
composite pass (`enhance_composite.frag`): of the four half-resolution pixels
round a pixel, the ones at its own depth count, so the shade under a car never
bleeds onto the road seen past it. Occlusion and contact shadows darken lit
surfaces but not what glows, so neon in a corner stays as bright as it is.

**Soft shadows and a low sun.** The PCSS taps lie in one fixed pattern
(turning it per pixel left grain along every soft edge), and the widest edge
follows the sun's height: at a low sun one shadow-map texel stretches up to
seven times its length along the ground, so there the edge is kept as narrow
as the plain filter's, or leaf shadows smear into haze. Under an overcast sky
the shadows are faint and soft already, and the cheaper program is used. The
soft shadows are two more programs (`scene_soft.frag`, `scene_wet_soft.frag`),
used only while ray tracing is on, so the plain ones stay as small and fast as
before.

**Cost.** The corner panel shows each pass's GPU time while it is on.
Measured at 1080p on the RTX 3050 laptop GPU, the passes take 0.6–0.9 ms in
all (reflections 0.2–0.3, occlusion and contact shadows 0.2–0.35, sun shafts
0.1 when shown, composite 0.2), and the soft shadows add 0.2–0.35 ms to the
scene pass: about 1 ms, against a budget of 3. The heaviest view (the
T-junctions G and ST at Evening under cloud, with its long shadows) goes from
9.3–9.4 to 10.0–10.3 ms of the 13.9 ms frame, and every view tested, day and
night, clear and rain, stays at a steady 72 FPS.

---

## Your car, and you on foot

`Player.cpp` moves your car and you in the same fixed 1/60 s steps as the
traffic, and they are drawn blended between steps just like the AI cars.

* **Driving** is a kinematic bicycle model: the car turns about its rear axle
  at `yaw rate = v / wheelbase · tan(steer)`. The steering lock shrinks from 34°
  when slow to 7° at 80 km/h and the wheel turns at a limited rate, so the car
  never twitches. Up to 50 km/h normally and 80 km/h with boost; brakes, then
  reverse; a handbrake that lets the rear step out a little.
* **Kerbs are bumps, not walls**: the car rides up onto sidewalks and lawns
  smoothly, and can park off the road.
* **Collisions**: buildings, crates, tree trunks, lamp posts, signal poles,
  sign and billboard posts, the roundabout islands and the AI cars are solid.
  The car moves in sub-steps of at most 20 cm, so it cannot tunnel through
  anything, and is pushed out of anything it touches along the shortest way.
  The part of its speed going into the obstacle is taken away (with a little
  bounce); a glancing blow on a wall swings the nose round so the car scrapes
  along it, while a head-on hit just stops it.
* **The AI yields to you**: every AI car slides its own body along its path
  ahead, into its next route, and brakes for you as for a car in front if it
  would touch you there. It also never enters a junction you are standing in.
  If you stop in a lane, the cars behind wait for you (they cannot overtake).
* **On foot**, `F` gets you out beside the driver's door (only when the car has
  nearly stopped) and back in when you stand next to it. You walk (or run with
  `Shift`) where you look, step up onto the kerbs, and slide along walls, cars
  and posts.
* **Cameras**: `C` locks onto you. The chase view rides a stiff spring above and
  behind the car; the driver view looks over the bonnet, turns your head with
  the mouse (up to 70° either way), looks back while `B` is held, and leans a
  little under braking and cornering. On foot you see through your own eyes.
* **HUD**: a speedometer, short messages, and a minimap of the city with the
  signals' colours, every car, and you.

---

## The people

80 people (`--pedestrians N`, up to 150) walk the sidewalks round every block,
keeping to the right, and cross at the 54 zebra crossings (`Pedestrians.cpp`).

* **Walking lines:** each sidewalk has two, one per direction, laid out at
  start-up and bent round lamp posts, signal poles and shelters. Crossing ends
  join them into one network that reaches every block.
* **Crossing:** at signalised junctions people wait for WALK (lights on poles at
  both ends: green figure, then the red one flashing while the last people are
  still over). With traffic waiting across, WALK ends 8 s before the green can
  run out, but only once nobody is still waiting to start, so the turning
  traffic gets the end of the green and nobody misses their turn. At zebras they go when the traffic lets them. Over a roundabout
  arm they cross in two halves with a wait on the splitter island. They step
  out only when no vehicle is on the crossing or already turning onto it, and
  every vehicle heading for it can still stop comfortably. Once on it, they
  never stop.
* **The traffic gives way:** a vehicle never enters a junction while people
  are on a crossing on its way through, or waiting at one it could stop for.
  On the approach to a zebra, drivers who can stop comfortably let waiting
  people over. After 7 s a waiting driver stops giving way and the people
  let it go first. That patience only runs while the driver's own light would
  let it go, so a car standing at red never keeps anyone from walking.
  Someone who has already waited 45 s lets it go first only if it could
  really go now: a car still waiting for a gap in a roundabout keeps nobody
  back, or a stream of such cars could. Every route over a crossing has a place to wait on its
  approach lane, where the traffic behind can see it.
* **The figure (`Mannequin.cpp`):** a mannequin of Bezier-revolution parts on a
  small skeleton. Its motion is authored as keyframed curves (idle, walk, jog,
  waiting at the kerb and looking both ways, crossing). Walk and jog are sampled
  at the same gait phase and blended by speed. States change through a small
  animation graph with 0.25 s cross-fades. The phase follows the distance
  walked, and a planted foot is locked where it landed. The legs are placed by
  two-bone IK on the ground under each foot, so people step up and down kerbs.
* **Drawing (`PedestrianRenderer.cpp`):** one instanced draw call per body
  shape (13 for the whole crowd). Far away, poses are refreshed every second or
  fourth frame. Umbrellas go up in the rain.
* **You on foot** are drawn as the same figure.

## The vehicles

Eleven kinds drive the city (`VehicleTypes.cpp`): sedan, hatchback, SUV, taxi,
police car, van, pickup, ambulance, box truck, 12 m city bus, and a motorbike
with its rider. Each has its own size, cruising speed, acceleration, braking,
time gap to the car in front, paint and driver's seat. The default mix of 36
is two line buses and a repeating pattern that is mostly cars, with a few vans,
trucks, motorbikes and emergency vehicles. Your own car is a yellow hatchback.

* **Lofted bodies** (`VehicleRenderer.cpp`): the side profile is straight runs
  joined by Bezier corners (the Lab 5 curve, `Mesh::bezier`), swept across the
  width. The glass house leans inward, the corners are rounded seen from above,
  and arches are cut round the wheels. Each band decides whether it is paint or
  glass, which gives windscreens, rear windows and A-, B- and C-pillars.
  Lamps, bumpers, the cargo box, the light bars and the signs are baked into one
  mesh per material, so a vehicle costs a handful of draw calls.
* **Lights**: headlights at night; tail lights that brighten when braking or
  standing; indicators that blink from well before a turn or lane change until
  it is done; the taxi's roof sign; the bus's "1 CITY LOOP" display. Police
  cars and ambulances flash red and blue, and at night their light bars light
  the road round them, joining the same 32-light budget as the lamps.
* **Long vehicles swing**: the front axle follows the lane and the rear axle is
  dragged behind it a wheelbase back (a tractrix), so a bus's rear wheels cut
  inside a corner and its nose swings out. `--plot` writes `turns.png`, bodies
  drawn every metre through three turns at X0.
* **Where each size may drive** is worked out from the geometry: cars and vans
  keep their whole body off the kerbs and may drive everywhere. Trucks and
  buses keep their wheels on the road and may swing their overhangs up to
  0.8 m over a kerb (the lamp posts stand further back). Trucks cannot take the
  tight 6.75 m right turns from the kerb lane, so they have their own wide
  right turn into the far lane, and at a roundabout they only go straight on
  from the kerb lane. That leaves them 113 of the 220 ordinary routes, one
  connected network.
* **The bus line** runs round the block between X0, G, G2 and X1, turning left
  from the kerb lane into the kerb lane (a bus-only movement). It has four stops
  with a shelter, a lit advertising panel, a BUS sign and a yellow box on the
  road. A bus stands 8 s at each stop with its doors open, and indicates before
  pulling away.
* **Junctions stay collision-free with long vehicles**: conflict zones are now
  measured along the lane, per width tier (ordinary and wide), and each vehicle
  adds its own length when it asks whether its body is over a zone. Where it
  waits and when it has cleared the junction depend on its size class.

---

## Verification

These command-line modes run without opening a window:

```
OpenGLMiniProject.exe --self-test
OpenGLMiniProject.exe --plot
OpenGLMiniProject.exe --soak 30 1 [--cars 36] [--pedestrians 80] [--stop-limit 60] [--trace [T]] [--trace-people]
                      [--trace-crossing C FROM TO] [--weather rain]
OpenGLMiniProject.exe --motion-test
OpenGLMiniProject.exe --light-test
OpenGLMiniProject.exe --player-test
OpenGLMiniProject.exe --walk-test
OpenGLMiniProject.exe --sun-test
OpenGLMiniProject.exe --weather-test
OpenGLMiniProject.exe --dimensions
```

`--weather-test` checks the weather. `K` must step Clear → Cloudy → Rain →
Clear. Every change (and a `K` pressed half-way through one) is replayed at
60 Hz at Morning, Evening and Night: it must take 10 s, and from one frame to
the next the light may change by at most 0.03, the sky and ambient by 0.01,
the exposure by 0.005, and the cover and the rain by 0.01. The clouds must be
at least 60 % gathered when the rain starts, and at most 40 % cleared when it
stops. Rain at noon must dim the sunlight to under 40 % and raise the sky's
light. Last, two minutes of rain and six of clear sky: the streets must be wet
after 30–70 s, the puddles full later, and afterwards the puddles must be the
last to dry.

`--sun-test` checks the sky. At each preset the sun (or moon) must stand at
the right height with shadows pointing the right way, and the lamps must be
on or off as they should. Every glide (`O` from each preset, an hour each
way, a click from night to noon) and one whole automatic day are replayed
at 60 Hz. The light may turn at most 3° and its colour change at most 0.03
in one frame, and it must be black where it passes from the sun to the moon.
Last, a camera walks, turns, rises and falls for 30 s with the noon sun, and a
grid of points on the ground must keep exactly their place within their texel
of the near shadow map.

`--walk-test` checks the walking figure: at speeds from a stroll to a jog, and
stepping down and up a kerb, and speeding up from standing to jogging, a foot on
the ground must not slide (under 2 % of the distance walked), no sole may sink
into the ground or float above it, the leg must always reach its foot, and no
foot may jump from one frame to the next.

`--self-test` checks the whole network (16282 checks), that every bus shelter
stands on the sidewalk clear of everything else, and the city dressing: every
building stands on the lawn, clear of the sidewalks and of every other
building, and every tree, bench, parked car and post stands clear of the
buildings and off the road (it also prints how many of each were placed). The
network checks are:

* every junction has as many arms as its type needs, and the whole network is
  one piece: from any route, in either lane, a car can reach every other route
  and get back again;
* every route begins where others end and ends where others begin;
* walking each route in 5 cm steps never jumps in position or in heading, which
  proves the segments actually join up tangentially;
* for every size class, on every route it may drive, the body swings without
  jumps, lines up with the lane at both ends of the route, and stays on the road
  (for trucks and buses, their wheels); cars and vans may drive every ordinary
  route, trucks keep a connected network, and every size class can reach every
  route it may drive and get back again;
* every kind of vehicle fits inside its size class's outline;
* the bus line is a closed loop, and every stop is on a straight, well short of
  the queue at the line and outside every conflict zone;
* every size class waits before all of its route's conflict zones;
* every conflict is listed once by each of its two routes, and a left turn gives
  way to the opposing straight-on car;
* no two conflicting movements are ever allowed together unless one clearly gives
  way, and crossing axes are never allowed together at all;
* `Route::reversed()` and `Route::translated()` behave.

`--plot` prints every junction's routes (stop line, zone count, successors) and
writes `network.png`: a top-down picture of the kerbs, every route (inner lanes
cyan, outer orange, bus and truck turns magenta), the stop lines, the conflict
zones and the bus stops. It also writes `turns.png`, a close-up of X0 with bus,
truck and car bodies drawn every metre through a turn.

`--soak <minutes> <seed>` runs the traffic headless at the real 60 Hz step and
tests every pair of real vehicle outlines (oriented boxes, separating-axis test)
every step. It passes when there are no overlaps, no car stood still longer than
the stop limit (60 s), no junction carries more than 35 % of all traffic, and
every line bus stopped at a stop at least once a minute. `--weather rain`
runs it all in the rain (slower drivers, longer gaps, faster walkers). With people (80 by
default) it also tests every person against every vehicle body each step (there
must be no touch), counts how long anyone stood waiting (at most 90 s), and
checks nobody ever stepped out against the lights.
`--trace` prints the junction state and why each waiting car is waiting;
`--trace-people` prints everyone at a crossing, the lights, and which vehicle
keeps it from being clear, when someone has waited a minute.
`--trace-crossing C FROM TO` prints crossing C every half second between the
two simulated times: its junction's phase, the walk light, who waits and who
is on it, and which vehicle keeps it from being clear (and why).
The HUD shows the same overlap count live.

`--motion-test` replays the frame loop at 144, 60 and 75 Hz with realistically
uneven frame times and measures judder (how much a car's on-screen speed jumps
from frame to frame); it must stay below 0.01 with interpolation.

`--light-test` drives a camera along every road at night (street level, follow
height, and a 30 m fly-over) and turns it on the spot in every junction. It
records how much any light on screen (lamp, neon or billboard) changes strength
from one frame to the next; a change over 0.1 counts as a pop, and there must be
none.

`--player-test` checks your car and you on foot: a head-on crash into a building
at boost speed and a glancing one (the car must never sink more than 5 cm into
the wall, and the glancing one must slide along it), walking into a wall at 45°
(never inside it, sliding along), and laps of the ring road among 36 AI cars
driven by an autopilot. No AI car may ever move into your car, and the chase
and driver-view cameras, replayed at 144 Hz with uneven frame times, must move
without judder.

`--capture out.png` renders a fixed view and saves it, and reports frame timing:
average, 99th percentile, worst frame, frames over 25 ms, GPU time, and for each
slow frame whether the time went into our own work or into the buffer swap.
Options: `--view 0..26 --time H --glide H --shadows high|low|off --shading 0..2
--weather clear|cloudy|rain --wet W --weather-to clear|cloudy|rain
--no-hud --frames N --size 1920x1080 --fullscreen --scale 0.67 --full-rate
--graph --warm S --enhanced --confirm`. `--glide H` starts the sun gliding to H with the
first frame, and `--warm S` runs the city for S seconds before the first frame.
`--weather` sets the sky at once (rain: streets soaked, umbrellas up), `--wet W`
sets how wet the streets are (0..1), and `--weather-to` starts a blend with the
first frame. `--enhanced` turns ray tracing on (the report then adds each
pass's GPU time), and `--confirm` shows the ray-tracing question on screen.
The views are
0 the central crossroads, 1 street level, 2 roundabout R1 and its fountain, 3 the
whole city, 4 the T-junctions G and ST, 5 straight down, 6 roundabout R2,
7 the ring road, 8 your car from the chase view, 9 the driver view over its
bonnet, 10 on foot beside it, 11 and 13 a line-up of every vehicle kind from
the front and from behind, 12 a bus at its stop with its doors open, 14 and
15 the chase view and the driver's seat of the first line bus, 16 the park's
plaza and pond, 17 the petrol station, 18 a shopping street, 19 houses and
flats, 20 X0's south crossing and its far walk light, 21 the zebra over G's
east arm, 22 the crossing of R1's west arm and its island, 23 following a
person, 24 through their eyes, 25 people waiting at X0's north crossing and
26 the sky over the city, looking north from the ring road.
At start-up the program prints how long the traffic, the city and the meshes
took to build.

---

## Demonstration order

1. **The city** — top view (`M`), then the maze of junction types, and a car
   followed (`Tab`) round several of them without ever leaving town.
2. **Your car** — `C`, drive round the block in the chase view, `V` for the
   driver view over the bonnet, stop in a lane and watch the traffic wait, bump
   a building, then `F` to get out and walk.
3. **Cameras** — free (with `Shift`), top, follow, driver (`M`, `Tab`, `V`).
4. **The city dressing** — a shopping street, the park and its pond, the petrol
   station; trees swaying; then night falls and the windows, shops and neon
   light up.
5. **Hierarchical car model** — body, cabin, four wheels; wheel rotation derived
   from distance travelled (`angle += distance / wheelRadius`).
6. **Traffic signals** — the left-turn arrow, green, yellow and all-red phases,
   stopping at the line, left turns giving way, the sigmoid jerk limit that makes
   braking and acceleration smooth, and `OVERLAPS: 0` on the HUD.
7. **Roundabouts and give-way junctions** — cars giving way on entry, turning
   along arcs and steering their front wheels into the corners; lane changes after
   a junction.
8. **The Bezier fountain** — show the control-point list in `Scene.cpp`, then the
   surface of revolution it generates.
9. **Textures** — the road's `GL_REPEAT` tiling, the crates' diffuse map next
   to their specular map, and the alpha-tested leaf cards.
10. **Day–night** (`O`, `[` `]`, the corner buttons, `T`, `L`) — step through the
   five presets from the top view and at street level and watch the shadows
   sweep round; street lamps, neon, billboards and headlights at night, and
   the spot-light cone on the billboard at the central crossroads. `F2` turns
   the shadows off and on again.
11. **Weather** (`K`, the weather buttons) — from street level at Afternoon:
   Clear to Cloudy (clouds gather and their shadows drift over the street),
   then Rain (headlights on, umbrellas up, the road darkening, puddles filling
   and rings where drops land). Then Night in the rain: rain glittering under
   the lamps, lamps shining in the puddles, and the wipers from the driver view.
12. **Ray tracing** (the button or `F3`, then *Yes*) — at Night in the rain on
   the shopping street (view 18): neon, shop windows and tail lights appear in
   the wet road and the puddles. Then Afternoon: soft contact shading under
   the cars and along the kerbs, and shadows sharp at their casters and softer
   away from them. Then Evening facing the sun: shafts of light past the
   buildings. Click again and answer *No* to compare.
13. **Shading comparison** (`1` / `2` / `3`) — flat, Gouraud and Phong, best seen on
   the curved fountain, the lamp posts and the tree trunks.

---

## Delivery

Everything for the project show lives in `delivery/`, and all of it is made
by the program itself or by a script, so it can be rebuilt after any change.

| File | What | How to rebuild |
| --- | --- | --- |
| `report/report.pdf` | The printed report (23 pages): each lab topic with worked calculations, the four light types and their values, the objects' dimensions, ray tracing, test and frame-rate results, a screenshot gallery | `pdflatex report.tex` twice, in `delivery/report` (MiKTeX) |
| `report/dimensions.txt` | Every object's size, printed from the program's own tables | `OpenGLMiniProject.exe --dimensions` |
| `report/logs/` | The final test runs: every self-test, and the 32 soaks (seeds 1–16, clear and rain) | see *Verification* |
| `slides/Smart_City_Digital_Traffic_Signals.pptx` | 10 slides: title, demo video, features, 3D transformations, illumination model, shading, textures and Bezier surfaces, day–night and weather, ray tracing, thank you. All text is editable; the speaker notes hold what to say | `python delivery/slides/build_slides.py` (`pip install python-pptx`) |
| `video/demo.mp4` | The 2-minute 1080p demo video | `python delivery/video/make_video.py` (`pip install imageio-ffmpeg`, about 3 minutes) |
| `video/people_crossing.mp4` | A 10.5 s clip of people waiting for WALK and crossing the zebra at the central crossroads, to cut in over the video's people part (50–58 s) | `python delivery/video/make_people_clip.py` (about 1 minute) |
| `figures/jpg/` | The figures used by the report and the slides | `sh delivery/figures/capture_all.sh`, the tour stills (below), then `python delivery/figures/make_figures.py` |

The PPTX (about 120 MB, the video is inside it) and the MP4 are not in git;
the full-size PNG screenshots are not either, only the JPEG figures made
from them.

**`--dimensions`** prints the size of every kind of object: the 11 vehicle
kinds (length, width, height, wheelbase, wheel radius, speeds), the road and
junction constants, the buildings by style (count, footprint, height,
storeys), the trees by species, the street furniture, signals and signs,
people, the camera, and the lights (point-light attenuation, spot cut-offs,
and the sun or moon's colour and height at each preset).

**`--tour video.mp4`** plays the scripted two-minute tour (`Tour.cpp`) and
pipes every frame to ffmpeg; each frame advances the city exactly 1/30 s, so
the video is smooth whatever the PC's speed. Your car in it is steered by the
city's own AI driver (`Player::followVehicle`), so it stops at red and keeps
to its lane. The people's part shows the signalised crossing with the most people
walking over it. People walk only while their WALK light is on: during the
parallel green, never during the all-red or the other road's left-turn arrow
(whose cars turn across the zebra), so a red light for the cars beside them
does not by itself mean it is their turn. `--tour-people clip.mp4` records
only a 10.5 s clip: the scout run finds the moment a WALK light turns on
with the most people waiting and then walking, and the camera looks down on
that zebra from above the middle of the road.
`--tour-stills DIR T1,T2,...` saves only those moments as PNGs
(the report's tour figures are `8,16,26,30,44,54,60,63,66.5,72,80,88,103,108,112,116`).

---

## Deliberately not included

Storms, fog, imported models, physics and full (hardware) ray tracing are not
part of this version.
The scene is authored geometry throughout: there is no model file anywhere in
this project.
