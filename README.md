# 3D Smart Traffic City

An interactive city traffic simulation in modern OpenGL.

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

A day–night cycle drives the sun, 182 street lamps, six neon signs and six lit
billboards, and the shading model can be switched between flat, Gouraud and
Phong while the simulation runs.

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
| `G` | Advance every traffic signal by one phase |
| `P` | Pause / resume |
| `1` `2` `3` | Flat / Gouraud / Phong shading |
| `T` | Toggle the automatic day–night cycle |
| `Y` / `N` | Force noon / midnight |
| `L` | Toggle the night lights (lamps, neon, billboards) |
| `R` | Reset camera, traffic and time |
| `H` | Show / hide the control panel |
| `F5` | Frame-time graph (last 240 frames) |
| `F6` | Resolution: automatic / always native / always 720p inside the window |
| `F7` | Frame pacing: steady (every second refresh on a 120 Hz+ screen) / full rate |
| `F11` | Fullscreen |
| `Esc` | Exit |

The window opens at 1920×1080, or maximised when the screen is only 1080p tall
(`F11` then gives true fullscreen 1080p; `--fullscreen` starts that way). The old
`M` key that switched one junction between signals and a roundabout is gone
(every junction now keeps its own type); `M` is now the top view.

---

## Where each lab topic is used

| Lab | Topic | Where it lives |
| --- | --- | --- |
| **1** | 2D primitives, 2D transformations | `Route::rotated` and `Route::translated` (`Route.cpp`) apply the 2D rotation and translation that place one authored northbound route onto every arm of every junction. Lane markings, zebras, arrows and stop lines in `RoadRenderer.cpp` are rectangles rotated onto the direction of their road. |
| **2** | 3D drawing, camera, model / view / projection | `Mesh.cpp` builds indexed VAO/VBO/EBO geometry and `MeshBuilder` bakes the whole road network into six meshes; `Camera.cpp` provides seven camera modes (free, top, AI follow and driver, your chase view, driver view and your own eyes) using `glm::lookAt` and `glm::perspective`; `Scene::render` uploads `uModel`, `uView` and `uProjection` every frame. |
| **3** | Illumination model and shading | `shaders/scene.frag` implements ambient + one directional sun + attenuated point lights (`k_c = 1`, `k_l = 0.09`, `k_q = 0.032`, the lab's constants) + **one spot light** with cosine cut-off angles: the lamp on an arm under the billboard at the central crossroads, which lights its picture at night. The four Lab 3 lamps at the central crossroads are always lit; the other street lamps, the neon spill and the billboard glow share a budget of 32 lights per frame (`LightManager.cpp`, `shaders/lights.glsl`). `uShadingMode` selects flat, Gouraud or Phong from one shader pair. |
| **4** | Texture mapping | `Texture::fromFile(path, wrapS, wrapT, minFilter, magFilter)` mirrors the lab's `loadTexture` signature, so wrapping and filtering are explicit at every call site. The roadside crates carry the lab's own **diffuse + specular map pair** (`container2.png`, `container2_specular.png`), sampled as `uDiffuseTexture` and `uSpecularTexture`; grass and leaves use a dim one-texel specular map so they stay matte. |
| **4b** | Texture sources | `assets/asphalt-photoreal.png` and the container pair are real image files. `assets/grass.png`, `assets/sidewalk.png` and `assets/facade.png` are optional: if present they are loaded, and if absent the matching procedural generator in `Texture.cpp` is used instead, so the project runs with no assets at all. |
| **5** | Bezier curves and surfaces | `Mesh::makeBezierRevolution` (`Mesh.cpp`) ports `nCr` and the Bernstein evaluation from the Lab 5 curve program and sweeps the resulting profile about the Y axis. Control points are written in source (top of `Scene.cpp`) instead of picked with the mouse. It generates the **fountain basin and column, the tree trunks and canopies, the street lamp posts and the sign posts**. |

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
  is a left-turner already waiting at the front, clearing the junction);
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
  PIZZA, CINEMA, BAR, 24H). The lettering is geometry: every stroke of the
  `stb_easy_font` text becomes a thin glass tube, which glows and blooms at
  night (the BAR sign's tired tube stutters). Each sign throws a coloured light
  onto the pavement below it.
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
OpenGLMiniProject.exe --soak 30 1 [--cars 36] [--stop-limit 60] [--trace [T]]
OpenGLMiniProject.exe --motion-test
OpenGLMiniProject.exe --light-test
OpenGLMiniProject.exe --player-test
```

`--self-test` checks the whole network (15228 checks), and that every bus
shelter stands on the sidewalk clear of everything else:

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
every line bus stopped at a stop at least once a minute.
`--trace` prints the junction state and why each waiting car is waiting.
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
Options: `--view 0..15 --time H --shading 0..2 --no-hud --frames N
--size 1920x1080 --fullscreen --scale 0.67 --full-rate --graph`. The views are
0 the central crossroads, 1 street level, 2 roundabout R1 and its fountain, 3 the
whole city, 4 the T-junctions G and ST, 5 straight down, 6 roundabout R2,
7 the ring road, 8 your car from the chase view, 9 the driver view over its
bonnet, 10 on foot beside it, 11 and 13 a line-up of every vehicle kind from
the front and from behind, 12 a bus at its stop with its doors open, and 14 and
15 the chase view and the driver's seat of the first line bus.

---

## Demonstration order

1. **The city** — top view (`M`), then the maze of junction types, and a car
   followed (`Tab`) round several of them without ever leaving town.
2. **Your car** — `C`, drive round the block in the chase view, `V` for the
   driver view over the bonnet, stop in a lane and watch the traffic wait, bump
   a building, then `F` to get out and walk.
3. **Cameras** — free (with `Shift`), top, follow, driver (`M`, `Tab`, `V`).
4. **Hierarchical car model** — body, cabin, four wheels; wheel rotation derived
   from distance travelled (`angle += distance / wheelRadius`).
5. **Traffic signals** — the left-turn arrow, green, yellow and all-red phases,
   stopping at the line, left turns giving way, the sigmoid jerk limit that makes
   braking and acceleration smooth, and `OVERLAPS: 0` on the HUD.
6. **Roundabouts and give-way junctions** — cars giving way on entry, turning
   along arcs and steering their front wheels into the corners; lane changes after
   a junction.
7. **The Bezier fountain** — show the control-point list in `Scene.cpp`, then the
   surface of revolution it generates.
8. **Textures** — the road's `GL_REPEAT` tiling, and the crates' diffuse map next
   to their specular map.
9. **Day–night** (`T`, `Y`, `N`, `L`) — sun, street lamps, neon and billboards, and the
   spot-light cone on the billboard at the central crossroads at night.
10. **Shading comparison** (`1` / `2` / `3`) — flat, Gouraud and Phong, best seen on
   the curved fountain and tree canopies.

---

## Deliberately not included

AI pedestrians, weather and rain, shadow mapping, imported models and physics
are not part of this version; `ENHANCEMENT_PLAN.md` lists the phases that add
them (section 5.1 sets out how the pedestrians will be animated).
The scene is authored geometry throughout: there is no model file anywhere in
this project.
