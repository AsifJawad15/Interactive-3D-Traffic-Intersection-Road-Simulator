# 3D Smart Traffic Intersection

An interactive traffic roundabout simulation in modern OpenGL.

**CSE 4102 — Computer Graphics and Image Processing Laboratory**
Asif Jawad · Roll 2107007 · Section A

A four-arm intersection that runs in two modes. In **signalised mode** the lights
cycle and vehicles queue, stop and turn. Pressing `M` sinks the signals and
raises a central island out of the road: the same vehicles now **circulate around
a roundabout**, giving way on entry and leaving by their chosen exit, with an
animated fountain at the centre. A day–night cycle drives the sun, four street
lamps and a floodlight, and the shading model can be switched between flat,
Gouraud and Phong while the simulation runs.

The traffic is **collision-free by construction**: every place where two routes
could touch is measured once at start-up, and a vehicle only enters the junction
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
that `shaders/` and `assets/` resolve.

---

## Controls

| Key | Action |
| --- | --- |
| `W` `A` `S` `D` | Move the free camera |
| `Q` / `E` | Move down / up |
| Mouse | Look around |
| `C` | Cycle camera: Free → Top → Follow → Driver |
| `V` | Driver view / free view |
| `Tab` | Select the next vehicle to follow |
| **`M`** | **Switch between signalised and roundabout mode** |
| `G` | Advance the traffic signal phase manually |
| `P` | Pause / resume |
| `1` `2` `3` | Flat / Gouraud / Phong shading |
| `T` | Toggle the automatic day–night cycle |
| `Y` / `N` | Force noon / midnight |
| `L` | Toggle the street lamps and floodlight |
| `R` | Reset camera, traffic and time |
| `H` | Show / hide the control panel |
| `F5` | Frame-time graph (last 240 frames) |
| `F6` | Resolution: automatic / always native / always 720p inside the window |
| `F7` | Frame pacing: steady (every second refresh on a 120 Hz+ screen) / full rate |
| `F11` | Fullscreen |
| `Esc` | Exit |

The window opens at 1920×1080, or maximised when the screen is only 1080p tall
(`F11` then gives true fullscreen 1080p; `--fullscreen` starts that way).

---

## Where each lab topic is used

| Lab | Topic | Where it lives |
| --- | --- | --- |
| **1** | 2D primitives, 2D transformations | `Route::rotated` (`Route.cpp`) applies the 2D rotation matrix that generates the east, south and west approaches from the single northbound route. Lane markings and crossings in `Scene::drawRoadMarkings` are transformed quads. |
| **2** | 3D drawing, camera, model / view / projection | `Mesh.cpp` builds indexed VAO/VBO/EBO geometry; `Camera.cpp` provides four camera modes using `glm::lookAt` and `glm::perspective`; `Scene::render` uploads `uModel`, `uView` and `uProjection` every frame. |
| **3** | Illumination model and shading | `shaders/scene.frag` implements ambient + one directional sun + four attenuated point lights (`k_c = 1`, `k_l = 0.09`, `k_q = 0.032`, the lab's constants) + **one spot light** with cosine cut-off angles. `uShadingMode` selects flat, Gouraud or Phong from one shader pair. |
| **4** | Texture mapping | `Texture::fromFile(path, wrapS, wrapT, minFilter, magFilter)` mirrors the lab's `loadTexture` signature, so wrapping and filtering are explicit at every call site. The roadside crates carry the lab's own **diffuse + specular map pair** (`container2.png`, `container2_specular.png`), sampled as `uDiffuseTexture` and `uSpecularTexture`. |
| **4b** | Texture sources | `assets/asphalt-photoreal.png` and the container pair are real image files. `assets/grass.png`, `assets/sidewalk.png` and `assets/facade.png` are optional: if present they are loaded, and if absent the matching procedural generator in `Texture.cpp` is used instead, so the project runs with no assets at all. |
| **5** | Bezier curves and surfaces | `Mesh::makeBezierRevolution` (`Mesh.cpp`) ports `nCr` and the Bernstein evaluation from the Lab 5 curve program and sweeps the resulting profile about the Y axis. Control points are written in source (top of `Scene.cpp`) instead of picked with the mouse. It generates the **fountain basin and column, the tree trunks and canopies, the street lamp posts and the sign posts**. |

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

### Four-fold symmetry

Every route is authored once for the northbound approach and rotated by 90°, 180°
and 270°. That removes three quarters of the geometry and three quarters of the
chances to get it wrong.

### Turn radii come from tangency, not from taste

A right turn must touch the incoming lane centre and the outgoing lane centre at
the road edge, which forces `radius = edge − lane`. The wide left turn gives
`radius = edge + lane`.

For the roundabout, the entry arc curves right while the circulating ring curves
left, so their circles are **externally tangent**: the distance between centres is
`ringRadius + entryRadius`. Fixing the entry centre one entry radius to the
driver's right of the approach lane pins its `x`, and tangency solves for its `z`.
The merge point is then simply the point on the line joining the two centres.

---

## How the traffic works

### Conflict zones (measured once, `TrafficBuild.cpp`)

For every pair of routes from different approaches, both routes are sampled every
25 cm near the middle, a car-sized box (plus a 30 cm margin) is placed at each
sample, and every pair of positions at which the two boxes overlap is marked.
Each connected patch of marks is a **conflict zone**, stored as an interval of
car-centre distance on each route. The key property: while a car's centre is
outside its interval, no car on the other route can touch it, wherever that car
is. The signal routes have 30 zones, the roundabout routes 60.

Routes that run along the same line — a shared approach lane, a shared exit lane,
a shared stretch of the ring — are found the same way and stored as **shared
spans**. On a shared span the cars simply follow each other.

The stop line of each route is moved, if needed, to just before its first zone,
so a waiting car can never be touched by crossing traffic.

### Commit and claim (`Simulation.cpp`)

A car approaching the junction **commits** only when, in one check:

* its signal allows it (on yellow, only if it cannot stop comfortably — or if it
  is a left-turner already waiting at the front, clearing the junction);
* the car in front of it in its lane has already committed (first in, first through);
* there is room for it beyond the junction, so it never blocks the box;
* nobody on a crossing route holds a claim on any zone on its way;
* no car with priority (straight on over a right turn over a left turn;
  circulating traffic over entering traffic) could reach a shared zone within
  3.5 s. Equal priority is served in turn: the car that has waited longer goes first.

On commit it **claims every zone on its way at once**, and releases each one as
soon as its centre has left that zone. A committed car never waits for a claim —
it only follows the car in front — so there is no hold-and-wait and therefore no
deadlock; and two cars on crossing routes are never inside a shared zone together,
so there is no collision. A car that committed on green but has not reached the
line when the light changes gives its claims back and stops, if it comfortably can.

### Car following

Speed comes from the **Intelligent Driver Model**: acceleration
`a·[1 − (v/v₀)⁴ − (s*/s)²]` with `s* = s₀ + vT + vΔv / 2√(ab)`, where the gap `s`
is measured bumper to bumper **along the route**, across shared spans. An
unclaimed stop line acts as a stationary car. Corners ahead are braked for
evenly, from `v² = v_c² + 2ad`. The **sigmoid easing** limits how quickly the
acceleration itself may change (the jerk), and a hard clamp keeps every car at
least 0.6 m behind the one in front whatever the model says.

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

Each axis gets a protected **left-turn arrow** (only when someone at the front of
a lane wants to turn left), then green for everyone with left turns giving way,
then yellow and a 2.5 s **all-red** clearance. Green is actuated: it ends early
once its own queue is empty and someone waits across, and it never runs past 16 s
while anyone waits.

---

## Verification

Four command-line modes run without opening a window:

```
OpenGLMiniProject.exe --self-test
OpenGLMiniProject.exe --plot
OpenGLMiniProject.exe --soak 30 1 --cars 12 [--mode signals|roundabout|both] [--trace [T]]
OpenGLMiniProject.exe --motion-test
```

`--motion-test` replays the frame loop at 144, 60 and 75 Hz with realistically
uneven frame times and measures judder (how much a car's on-screen speed jumps
from frame to frame); it must stay below 0.01 with interpolation.

`--capture out.png` renders a fixed view and saves it, and reports frame timing:
average, 99th percentile, worst frame, frames over 25 ms, GPU time, and for each
slow frame whether the time went into our own work or into the buffer swap.
Options: `--view 0..3 --time H --shading 0..2 --roundabout --no-hud --frames N
--size 1920x1080 --fullscreen --scale 0.67 --full-rate --graph`.

`--self-test` checks all 24 routes, the conflict table and the signal logic:

* every route starts and ends on a lane centre at the edge of the scene;
* walking each route in 5 cm steps never jumps in position or in heading, which
  proves the segments actually join up tangentially;
* roundabout routes stay clear of the raised island;
* every stop line lies before all of its route's conflict zones;
* every conflict is listed once by each of its two routes, and a left turn gives
  way to the opposing straight-on car;
* no two conflicting movements are ever allowed together unless one clearly gives
  way, and crossing axes are never allowed together at all;
* `Route::reversed()` and `Route::translated()` behave.

`--plot` prints a top-down ASCII map of both route families with the conflict
zones marked `*`, followed by each route's stop line, zone count and shared lanes.

`--soak <minutes> <seed>` runs the traffic headless at the real 60 Hz step and
tests every pair of real vehicle outlines (oriented boxes, separating-axis test)
every step. It reports overlaps (must be 0), the closest gap, the longest time any
car stood still (must be at most 60 s) and trips per approach (all must flow).
`--trace` prints the junction state and why each waiting car is waiting.
The HUD shows the same overlap count live.

---

## Demonstration order

1. **Cameras** — free, top, follow, driver (`C`, `Tab`, `V`).
2. **Hierarchical car model** — body, cabin, four wheels; wheel rotation derived
   from distance travelled (`angle += distance / wheelRadius`).
3. **Traffic signals** — the left-turn arrow, green, yellow and all-red phases,
   stopping at the line, left turns giving way, the sigmoid jerk limit that makes
   braking and acceleration smooth, and `OVERLAPS: 0` on the HUD.
4. **`M` — roundabout mode** — the island rises, the signals go dark, and cars
   turn along arcs, steering their front wheels into the corners.
5. **The Bezier fountain** — show the control-point list in `Scene.cpp`, then the
   surface of revolution it generates.
6. **Textures** — the road's `GL_REPEAT` tiling, and the crates' diffuse map next
   to their specular map.
7. **Day–night** (`T`, `Y`, `N`, `L`) — sun, point lights, and the spot-light cone
   on the road at night.
8. **Shading comparison** (`1` / `2` / `3`) — flat, Gouraud and Phong, best seen on
   the curved fountain and tree canopies.

---

## Deliberately not included

Pedestrians, weather and rain, shadow mapping, imported models and physics are
not part of this version; `ENHANCEMENT_PLAN.md` lists the phases that add them.
The scene is authored geometry throughout: there is no model file anywhere in
this project.
