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
| `Esc` | Exit |

---

## Where each lab topic is used

| Lab | Topic | Where it lives |
| --- | --- | --- |
| **1** | 2D primitives, 2D transformations | `Route::rotated` (`Route.cpp`) applies the 2D rotation matrix that generates the east, south and west approaches from the single northbound route. Lane markings and crossings in `Scene::drawRoadMarkings` are transformed quads. |
| **2** | 3D drawing, camera, model / view / projection | `Mesh.cpp` builds indexed VAO/VBO/EBO geometry; `Camera.cpp` provides four camera modes using `glm::lookAt` and `glm::perspective`; `Scene::render` uploads `uModel`, `uView` and `uProjection` every frame. |
| **3** | Illumination model and shading | `shaders/scene.frag` implements ambient + one directional sun + four attenuated point lights (`k_c = 1`, `k_l = 0.09`, `k_q = 0.032`, the lab's constants) + **one spot light** with cosine cut-off angles. `uShadingMode` selects flat, Gouraud or Phong from one shader pair. |
| **4** | Texture mapping | `Texture::fromFile(path, wrapS, wrapT, minFilter, magFilter)` mirrors the lab's `loadTexture` signature, so wrapping and filtering are explicit at every call site. The roadside crates carry the lab's own **diffuse + specular map pair** (`container2.png`, `container2_specular.png`), sampled as `uDiffuseTexture` and `uSpecularTexture`. |
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
the same update evaluated on a curved segment. The front wheels steer by the sign
of the curvature and the cornering speed drops on arcs.

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

### Giving way cannot deadlock

One forward scan (`closestLeaderGap`) covers queueing, turning cars crossing each
other, and circulating traffic. On entry to the roundabout a vehicle yields to
anything already on the ring that is closing on its merge point — and traffic
already circulating never yields to anything, so the rule has no cycle.

---

## Verification

Two command-line modes run without opening a window:

```
OpenGLMiniProject.exe --self-test
OpenGLMiniProject.exe --plot
```

`--self-test` checks all 24 routes and the signal logic:

* every route starts and ends on a lane centre at the edge of the scene;
* walking each route in 5 cm steps never jumps in position or in heading, which
  proves the segments actually join up tangentially;
* roundabout routes stay clear of the raised island;
* north–south and east–west are never green at the same time.

`--plot` prints a top-down ASCII map of both route families so the arcs can be
checked by eye against the island.

---

## Demonstration order

1. **Cameras** — free, top, follow, driver (`C`, `Tab`, `V`).
2. **Hierarchical car model** — body, cabin, four wheels; wheel rotation derived
   from distance travelled (`angle += distance / wheelRadius`).
3. **Traffic signals** — the phase cycle, stopping at the line, and the sigmoid
   easing that makes braking and acceleration smooth.
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

Pedestrians, weather and rain, fog, a sky dome, shadow mapping, imported models
and physics are all out of scope, matching the "Excluded from the Initial
Version" section of the project proposal. The scene is authored geometry
throughout: there is no model file anywhere in this project.
