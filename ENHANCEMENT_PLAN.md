# Enhancement Plan: OpenGLMiniProject → Open-World Smart City Traffic Simulator

> Status: planned, not yet implemented. Written 2026-09-23.
> Work goes **one phase at a time**: build → run → check → report → next phase only after review.
> Nothing is implemented all at once.

---

## 1. Context

The project is a working OpenGL 3.3 Core lab project (CSE 4102). It has one four-arm intersection
that can switch between signals and a roundabout (`M`), six cars, a day/night cycle, four street
lamps, one floodlight, Flat/Gouraud/Phong switching, Bezier surfaces and textures.

You want it to become a large, lively, open-world-feeling city:
- A bigger road network. More roundabouts and intersections. A loop road. Wider 4-lane roads.
- Many vehicle types: car, taxi, SUV, van, pickup, bus, truck, motorbike, police and ambulance.
- Pedestrians walking on footpaths and crossing at signals and zebra crossings.
- Shops with neon lights, street props, realistic trees, and no black-looking roads.
- A player car driven with the arrow keys, an on-foot pedestrian view, and real collision.
- Natural lighting, many night lights, shadows, weather, and ray tracing.
- A fix for the bug where cars collide in the middle of the intersection.

Constraints kept on purpose:
- **OpenGL 3.3 Core stays.** It is the course requirement, and the custom `x64-windows-gl33` triplet depends on it.
- **`vcpkg.json` does not change.** GLFW, GLM, GLAD and stb (stb_image, stb_easy_font, stb_image_write) cover everything.
- **Every lab feature stays demonstrable.** That covers Lab 1 to Lab 5: transforms, cameras, Flat/Gouraud/Phong and the spot light, texture wrap/filter and the specular map, and the Bezier surfaces.

Hardware found: **NVIDIA RTX 3050 Laptop GPU (4 GB)**, an AMD Radeon iGPU, and a Ryzen 7 4800H.
Build tool: `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`.

---

## 2. Analysis of the current project

### 2.1 What exists (3,947 lines)
| File | Role |
|---|---|
| `Route.h/.cpp` | Paths built from lines and arcs, sampled by arc length. `rotated()` gives four-fold symmetry. **Reusable as-is for the whole network.** |
| `Simulation.h/.cpp` | `TrafficSystem`: 24 hard-coded routes (12 signal, 12 roundabout), 6 vehicles, sigmoid speed easing, forward-ray following, and the ring give-way rule. `--self-test` and `--plot` live here. |
| `Scene.h/.cpp` | Draws everything with one shader. It makes one draw call per box, with hard-coded positions for 8 buildings, 12 trees, 6 crates, 4 lamps and 4 signals. |
| `shaders/scene.*` | Ambient, sun, 4 point lights and 1 spot light. Flat/Gouraud/Phong from `uShadingMode`, and a fountain water ripple. |
| `Camera`, `DayNight`, `Overlay`, `Mesh`, `Texture`, `Shader` | Camera modes (Free/Top/Follow/Driver), time of day, HUD, meshes (cube, beveled cube, cabin, cylinder, Bezier revolution), and textures. |

### 2.2 Bug: cars collide in the middle (root causes)
1. **No all-red clearance.** `advancePhase` ([Simulation.cpp:541](Simulation.cpp#L541)) goes straight from N/S yellow to E/W green. A car allowed through on yellow (up to 4.8 m out, [Simulation.cpp:479](Simulation.cpp#L479)) needs about 5 s to cross the 14 m left-turn arc at 55% speed, but yellow lasts only 2 s. Cross traffic starts while it is still in the box.
2. **Unprotected left turns with no yield rule.** North and south are green together. The wide left arc crosses the opposing straight lane, and nothing makes the left-turner give way.
3. **Straight-ray conflict detection.** `closestLeaderGap` ([Simulation.cpp:402](Simulation.cpp#L402)) only sees cars within 2.2 m sideways of a straight line along the current heading. A crossing car becomes visible only once it is already in front. Travel is then clamped to 0 and both cars freeze while overlapping: a collision followed by a deadlock.
4. **Roundabout merge hole.** `ringConflict` ([Simulation.cpp:429](Simulation.cpp#L429)) yields only to cars already past their merge distance. A car on the upstream entry arc is ignored, so two cars can merge into the same spot.
5. **Late braking on the ring.** On the 7.5 m ring, the straight ray loses the car ahead beyond about 5.7 m of arc. Followers skip the 6.4 to 14 m slow-down bands and brake hard.
6. **Unsafe respawn.** `leastCrowdedApproach` spawns a car even when there is less clearance than one car gap. With more vehicles, cars would spawn inside each other.

### 2.3 Why the side roads look black
- The asphalt texture averages about 0.22 brightness. It is multiplied by flat ambient light (0.24 by day, 0.035 at night).
- There is **no gamma correction or tone mapping**. Linear values go straight to a non-sRGB framebuffer, which crushes dark tones.
- Only 4 lamps exist, all at ±11 m, with 1/(1+0.09d+0.032d²) falloff. The arms at 20 to 40 m get about 6% lamp light, so at night they are black.

### 2.4 Scaling blockers
- `Shader::set*` calls `glGetUniformLocation` by string on every call. That is about 10 lookups per draw, plus `std::to_string` names every frame.
- Every prop is its own draw call. There is no batching, instancing or culling.
- The shader has a fixed limit of 4 point lights and 1 spot light.
- On a hybrid-GPU laptop the app may run on the **AMD iGPU** unless it exports `NvOptimusEnablement`.
- `Overlay::drawText` allocates a 128 KB buffer on every call.

---

## 3. Key design decisions

### 3.1 Ray tracing: how it can be achieved
OpenGL has **no hardware ray-tracing API**, even though the RTX 3050 has RT cores. The options are:

| Option | Verdict |
|---|---|
| Rewrite in Vulkan (`VK_KHR_ray_query`) or DirectX 12 (DXR) for hardware RT | Rejected. It means a full renderer rewrite and leaves the OpenGL 3.3 course requirement. |
| Screen-space reflections | Rejected as the main approach. They only reflect what is already on screen. |
| **Hybrid software ray tracing in GLSL (chosen)** | Keeps OpenGL 3.3. The CPU builds a BVH over simplified proxy geometry: boxes for buildings, cars and signs, spheres for tree canopies, and an analytic ground plane. The BVH is uploaded to float textures and traced in the fragment shader. This is the same "raster first, trace secondary rays" idea RTX games use. |

The ray tracer is used for:
1. **Ray-traced reflections** on wet roads, puddles, car paint, shop glass and water. Neon and lit windows show up in the wet street, like the reference image.
2. **Ray-traced sun shadows** as an A/B toggle against shadow maps.
3. **A photo mode (stretch goal)** that progressively accumulates ray-traced indirect light while the game is paused.

### 3.2 City layout: a 5×5 junction grid, 100 m apart (world about 400 × 400 m)
```
 B───T───T───T───B      B  = bend (corner of the outer LOOP road)
 │   │   │   │   │      T  = T-junction (main road has priority, side road gives way)
 T───S───S───R───T      S  = signalised 4-way (all-red + protected-left phases + pedestrian signals)
 │   │   │   │   │      R  = roundabout
 T───S───R0──S───T      R0 = centre showcase roundabout with the fountain (M still toggles it to signals)
 │   │   │   │   │
 T───R───S───S───T
 │   │   │   │   │
 B───T───T───T───B
```
- **Junctions:** 3 roundabouts, 6 signalised junctions, 12 T-junctions and 4 bends. The perimeter forms the loop road.
- **Blocks:** 16 city blocks, including a park, a plaza, a gas station, and shop streets.
- **Scale:** grid size and spacing are single constants, so the city can shrink to 4×4 or 3×3 if frame rate needs it.
- **Open-world feel:** vehicles never despawn. They random-walk the network forever, with density-aware turn choices so no single area clogs. Buses run a fixed loop line.
- **Outskirts:** beyond the loop there are grass, low hills, a lit distant skyline ring at 350 to 700 m, and fog. A soft invisible boundary sits 40 m outside the loop.

### 3.3 Road cross-section (wider roads)
- **Lanes:** 2 lanes per direction, each 3.5 m wide. Lane centres are at ±1.75 m and ±5.25 m, with a double yellow centre line.
- **Width:** the carriageway half-width is 7.0 m. Kerbs are 0.15 m high. Sidewalks are 4.5 m wide. Buildings are set back 12 m from the centreline.
- **Signalised junctions:** the corner kerb radius is 5 m. The right turn uses radius 6.75 (outer lane) and the left turn uses radius 13.75 (inner lane). Both come from the existing tangency formulas with the new lane offsets.
- **Roundabouts:** a single wide circulating lane of radius 13, an island of radius 9.5 with an apron, and entry and exit arcs of radius 8. Entry and exit arcs are built for both approach lanes, with the existing tangent-circle construction. Splitter islands and zebra crossings sit on every arm.
- **Lane discipline:** the inner lane goes straight or left, and the outer lane goes straight or right. Turns decide which lane a car ends up in, so no lane changes are needed. Any lane may enter a roundabout.

### 3.4 Lighting architecture
The renderer stays **forward**, which keeps the Flat/Gouraud/Phong demo working. It gains **CPU-clustered lighting**:
- The screen is split into 16×9 tiles and 16 depth slices.
- Each cluster lists up to 24 light indices, stored in an integer texture.
- Lights use windowed falloff so the light range is finite. The four central "lab" lamps keep the Lab 3 constants k_c=1, k_l=0.09, k_q=0.032, multiplied by the window term.

This supports hundreds of lamps, neon spill lights and headlight spot lights, all on OpenGL 3.3.

---

## 4. Traffic and collision design (the core fix)

### 4.1 Lane graph
- `RoadNetwork` builds the network as **lanes**, each a `Route`: edge lanes and junction-internal connector lanes.
- **Successors:** every lane lists the lanes a car may take next.
- **Roundabouts:** the ring is split into ring-lane segments between merge and diverge points. Cars on the ring follow each other instead of being treated as one big conflict.
- **New `Route` helpers:** `translated()` and `reversed()`, plus curvature per sample.

### 4.2 Conflict points (precomputed once)
- **Detection:** for each pair of lanes in a junction that do not share a start point, sample both every 0.25 m. Any place where the lanes come within 2.8 m of each other is a **conflict zone**, with an [in, out] distance on each lane. Merges extend the zone to the end of the connector.
- **Priority rule for each conflict:**
  - straight beats left;
  - pedestrians beat turning cars;
  - the ring beats the entry;
  - the main road beats the side road;
  - with equal priority, whoever arrives first goes first.

### 4.3 Commit-and-claim rule (collision-free and deadlock-free)
1. Each junction approach has a **decision point** at the stop line or give-way line.
2. A car **commits** only when, in one atomic check:
   - its signal allows it (on yellow, it goes only if it cannot stop: d < v²/2b);
   - no car from the *other side* of any conflict on its path has claimed that conflict;
   - no higher-priority car will reach a shared conflict within the gap time (about 3.5 s);
   - **its exit lane has room for it**, so it never blocks the box.
3. On commit, the car **claims every conflict on its path through the junction**, including the exit crossing. It releases each claim once its rear clears that zone.
4. Committed cars never wait on conflicts. They only follow the car ahead in the same lane, and the exit-room check guarantees they can leave. That means **no hold-and-wait, so no deadlock**, and no two cars on conflicting lanes are ever inside a shared zone, so **no collision**.
5. Cars in the same lane can share claims, which keeps a platoon moving through on one green.
6. The **all-red phase (2.5 s)** and **protected-left phase** exist for realism. The claims are what guarantee safety.

### 4.4 Car following
- **Model:** the IDM (Intelligent Driver Model).
- **Gaps:** measured along the path, not as a straight line. The car ahead is found through sorted per-lane occupancy lists, looking up to 80 m ahead through upcoming lanes.
- **Stops:** stop lines and blocked crossings act as a virtual stationary car ahead.
- **Curves:** curve speed is v = √(a_lat·R), and cars brake ahead of the curve.
- **Smoothing:** the existing sigmoid easing stays as a jerk limiter on the acceleration command, so that demo point survives.
- **Safety net:** the old hard clamp stays. A car never moves past the rear of the car ahead minus the minimum gap.
- **Long vehicles:** buses and trucks sample both the front and rear axle along the path, so their bodies swing realistically through turns.

### 4.5 Physical collision layer (`Collision.h/.cpp`)
- **Tests:** 2D OBB-vs-OBB (SAT), OBB-vs-AABB and circle-vs-box, with a spatial hash (8 m cells) for the broad phase.
- **Player car:** collides with buildings, props and AI cars. The response pushes the car out along the minimum translation vector, removes the normal velocity, and adds a small bounce.
- **Player on foot:** slides along walls.
- **AI:** AI cars treat the player car or player on foot as an obstacle projected onto their path, with emergency braking. They will not commit if the player is inside, or about to enter, one of their conflict zones.
- **Invariant check:** every simulation step counts AI-vs-AI overlaps. The debug HUD shows the count, and it must stay 0.

### 4.6 Pedestrians in the traffic system
- **Crossings:** crosswalks are pedestrian lanes that have conflicts with vehicle lanes.
- **Signalised junctions:** WALK runs with the parallel through phase, and turning cars yield.
- **Zebras:** a pedestrian starts crossing only if no car is within its stopping distance. Cars yield to pedestrians waiting at the kerb.
- **No deadlock:** a pedestrian already crossing never waits for anything.

### 4.7 Fixed timestep
The simulation runs at a fixed 60 Hz with an accumulator. It is deterministic and seeded, so soak tests are reproducible.

---

## 5. World, props and visuals

- **Vehicles (`VehicleTypes.h/.cpp`):**
  - A *lofted body generator*: a Bezier side profile is swept across the width with the roof tapered inward (ties in Lab 5).
  - Types: sedan, hatchback, SUV, taxi (roof sign), van, pickup, 12 m bus, box truck, motorbike with rider, police car and ambulance (flashing light bars that are real lights at night).
  - Each type has its own length, width, IDM parameters and colour palette.
  - Brake lights respond to deceleration. Indicators blink *before* turns. Headlights come on at night and in rain or fog.
- **City generator (`World.h/.cpp`, seeded):**
  - Buildings have varied footprints and heights.
  - Ground floors are shops with glass fronts, striped awnings, lit interiors at night, and **neon signs**. The sign text is geometry from stb_easy_font, emissive, with bloom, flicker and a coloured spill light.
  - Shop types: cafe, pharmacy, bank, fashion, pizza, 24/7, hotel and cinema.
  - Upper floors use a procedural window grid in the shader, with random windows lit at night.
  - Rooftops carry tanks, AC units and billboards.
  - Special blocks: a park with a pond, a central plaza, and a gas station with a lit canopy.
- **Street furniture:** lamps every 25 m, benches, bins, hydrants, bollards, bus stops with lit ad panels, stop, yield and speed signs, parked cars and planters. The Lab 4 crates stay at the central junction.
- **Realistic trees (`TreeGenerator`):**
  - Structure: a Bezier trunk plus 2 to 3 levels of branches.
  - Leaves: clusters of **alpha-tested leaf cards** with an RGBA procedural leaf texture, normals bent outward, and wind sway in the vertex shader.
  - Species: broadleaf, conifer, and palm on the plaza.
  - Rendering: 4 to 6 prebuilt variants, drawn with instancing.
- **Pedestrians (`Pedestrians.h/.cpp`):**
  - Model: hierarchical, with hips, torso, head, two-part arms and two-part legs.
  - Walk cycle: limb phase is driven by distance walked, the same idea as wheel rotation.
  - Variety: different skin, shirt, trousers and hair colours. Umbrellas appear in rain.
  - Movement: a sidewalk graph that loops around each block and connects blocks through crossings.
  - Rendering: 100 to 150 walkers, **instanced per body part**, about 10 draw calls for all of them.
- **Sky and atmosphere:**
  - A sky dome with a sun-elevation gradient, sun glow, moon and stars.
  - Procedural clouds whose coverage follows the weather.
  - Height fog coloured by the sky, which also hides the edge of the world.
- **Weather (`Weather.h/.cpp`):**
  - States: Clear, Cloudy, Rain, Storm and Fog, with smooth 10 s transitions.
  - Rain: instanced streak particles in a volume that follows the camera, plus ground splashes.
  - Wetness: builds up while it rains and dries slowly afterwards. It darkens albedo, raises specular, and adds puddles from a noise mask that reflect through ray tracing.
  - Storms add lightning flashes.
  - AI drivers slow down and leave longer gaps in rain.
  - The cockpit view gets animated wipers.

---

## 6. Controls and cameras

| Key | Action |
|---|---|
| `W A S D`, `Q/E`, Mouse | Free camera (unchanged) |
| **Arrow keys** | Drive the player car, or walk when on foot |
| `Space` / `Left Shift` | Handbrake / run or boost |
| **`F`** | Switch between on foot and driving (spawns or enters the player car) |
| `C` | Cycle camera: Free → Top → Follow → Driver → **Chase (player)** → **On-foot first person** |
| `V` / `Tab` | Driver view / next AI vehicle. **`Shift+Tab`** follows an AI pedestrian (pedestrian-eye view) |
| `M` | Switch the centre junction between roundabout and signals. It drains the junction, swaps lanes, then reopens. |
| `G`, `P`, `1/2/3`, `T`, `Y/N`, `L`, `R`, `H`, `Esc` | Unchanged |
| **`K`** | Cycle weather |
| `F2` / `F3` / `F4` | Shadows on/off / ray-traced reflections on/off / RT photo mode |
| `F5` / `F6` | Debug overlay (conflict zones, claims, collision count) / quality preset (Low, Medium, High) |

**HUD:** speed in km/h, weather, mode, a **minimap** of the network with the player, vehicles and junction states, and performance stats (FPS, draw calls, active lights, and GPU time per pass from GL timer queries).

---

## 7. Performance budget
The target is 60 FPS at 1080p on the RTX 3050 with the High preset. A Low preset keeps the iGPU usable.
- `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance` exports force the discrete GPU.
- Uniform locations are cached, and per-frame data goes in a UBO.
- **Static batching:** a `MeshBuilder` bakes transformed primitives with per-vertex colour into one VBO per block per material. Draw calls drop from thousands to about 150.
- **Instancing** for trees, lamps, pedestrians, rain, parked cars and props.
- **Culling:** frustum culling plus distance culling that follows fog density. LOD for distant vehicles and pedestrians.
- **Shadows:** 2 cascades at 2048². **Ray tracing:** BVH traversal capped at 64 steps and 150 m, with an optional half-resolution setting.

---

## 8. Phases (one at a time, each ends buildable and verified)

After **every** phase:
1. Build Release x64 and fix all errors and warnings.
2. Run `--self-test`, plus `--soak` once it exists.
3. Launch the app and check it by eye.
4. Report the results with screenshots or observations.
5. **Wait for your go-ahead before starting the next phase.**

Each new `.cpp`, `.h` and shader file is registered in `OpenGLMiniProject.vcxproj` and `.filters` within the same phase. Checkpoint commits are your call; I will not commit unless you ask.

### Phase 0: Rendering foundations (fixes the black roads)
- Discrete-GPU export, a uniform-location cache, the fixed-timestep loop, and the Overlay buffer fix.
- An HDR framebuffer: RGBA16F with 4× MSAA, resolved to a single sample. Then bloom, ACES tone mapping, exposure keyed to daylight, and sRGB output.
- Colour textures loaded as sRGB. Specular maps and data textures stay linear.
- Hemisphere ambient light, the sky dome with sun, moon and stars, and height fog. The far plane moves to 900 m.
- **Files:** new `Framebuffer.*`, `PostProcess.*`, `Sky.*`, and shaders `post.vert`, `bloom.frag`, `tonemap.frag`, `sky.*`. Edits to `Shader.*` (cache and a simple `#include` expander), `Texture.*`, `main.cpp`, `scene.frag`, `DayNight.*`.
- **Check:** roads read as grey asphalt by day. Night is dark but not crushed. Flat, Gouraud and Phong all still work.

### Phase 1: Traffic core rewrite and collision fix (on the existing single junction)
- The lane graph with connectors, conflict-point precompute, commit-and-claim, IDM following along paths, and all-red plus protected-left signal phases.
- Roundabout merges handled through conflicts. Safe spawning with a gap check.
- The OBB overlap counter, and a new **`--soak <minutes> <seed>`** headless test.
- **Files:** new `Collision.*`, `LaneGraph` code inside `Simulation.*` (or a new `Traffic.*`). Edits to `Route.*` (translated, reversed, curvature), `main.cpp` (soak flag), and the `--plot` output.
- **Check:** a 30-minute soak over 5 seeds on both modes with 12 cars gives **0 overlaps, no car stopped longer than 60 s, and every approach keeps flowing**. The collision in the middle is gone when viewed in the app.

### Phase 2: Road network, wider roads, more roundabouts, the loop
- `RoadNetwork` with the 5×5 layout, the generic junction generator (4-way signalised, T, bend, roundabout), 4-lane roads, and density-aware random routing.
- Mesh generation from the network: roads, kerbs with corner fillets, sidewalks, markings (lane dashes, double yellow, stop lines, turn arrows, zebras, yield teeth), splitter islands and islands. Markings are batched.
- Street lamps along every road through the **clustered light manager**. The central four lab lamps and the floodlight spot light are kept.
- `M` drain-and-swap on the centre junction. Camera presets updated.
- **Files:** new `RoadNetwork.*`, `LightManager.*`, `MeshBuilder.*`, `RoadRenderer.*`. `Scene.cpp` is split into smaller renderers.
- **Check:** `--self-test` confirms every lane is tangent-continuous, the network is connected, lanes clear islands and kerbs, the conflict table is symmetric, and no conflicting movements are ever green together. `--plot` writes `network.png` of all lanes and conflict points with stb_image_write. Soak: 40 vehicles, 0 overlaps, and traffic spread across junctions (no junction holds more than 25% of cars).

### Phase 3: Interactivity (player car, on foot, pedestrian view)
- A player car with a kinematic bicycle model on the arrow keys, plus handbrake and boost. Collision with the world and AI, AI yielding to the player, chase and cockpit cameras.
- On-foot first person with wall collision. `F` to switch, the pedestrian-eye view, the minimap and a speedometer.
- **Files:** new `Player.*`. Edits to `Camera.*`, `main.cpp`, `Overlay.*`.
- **Check:** drive the whole loop, crash into buildings and cars (you get pushed back, no tunnelling), and walk into walls (you slide along them).

### Phase 4: Vehicle variety
- The lofted-body generator and the 11 vehicle types. Lights, indicators and brake lights. The bus loop line with bus-stop dwell time. Emergency flashers.
- **Files:** new `VehicleTypes.*`, `VehicleRenderer.*`. Edits to `Mesh.*` and the simulation spawn mix.
- **Check:** a mix of types is visible. Buses swing properly on turns and stop at stops. Soak with long vehicles still gives 0 overlaps.

### Phase 5: City dressing (shops, neon, trees, props)
- The world generator, buildings, shops with neon text, the procedural lit-window facade, the park, plaza and gas station.
- Street furniture, parked cars, and the realistic instanced trees with leaf cards and wind. The distant skyline.
- **Files:** new `World.*`, `TreeGenerator.*`, `NeonText.*`, `PropRenderer.*`. RGBA support in `Texture.*`. Instancing attributes in `Mesh.*` and the shaders.
- **Check:** street-level and top views look like a city. Neon glows through bloom at night. The frame rate stays above 60 FPS and the HUD shows draw calls.

### Phase 6: Pedestrians
- The sidewalk graph, crossings (signalised and zebra), the walker model and animation, instanced rendering, vehicles yielding, and umbrellas.
- **Files:** new `Pedestrians.*`, `PedestrianRenderer.*`.
- **Check:** soak shows 0 vehicle-pedestrian overlaps on crossings and no pedestrian stuck for more than 90 s. You can see people waiting for WALK and then crossing.

### Phase 7: Shadows and night lighting polish
- Two-cascade stable shadow maps with PCF and alpha-tested leaf shadows. Headlights become clustered spot lights. Neon spill lights, contact shadows and moonlight.
- **Files:** new `ShadowMap.*`, `shadow.vert/frag`.
- **Check:** shadows have no acne, no peter-panning and no swimming while the camera moves. Streets are well lit at night.

### Phase 8: Weather
- The weather state machine, rain particles and splashes, wetness and puddles, lightning, fog, AI behaviour changes, and wipers.
- **Files:** new `Weather.*`, `Rain.*`, `rain.vert/frag`.
- **Check:** cycle through every state with `K` at day and at night. Transitions are smooth and FPS stays at or above 60.

### Phase 9: Ray tracing
- **9a:** proxy extraction, a binned-SAH BVH on the CPU, and upload to RGBA32F textures (a static BVH plus a per-frame dynamic one). A GLSL traversal library, **RT reflections** (Fresnel and wetness weighted, with roughness jitter), and an **RT sun-shadow** toggle. GPU timings in the HUD.
- **9b (stretch):** the photo mode. It uses a raster G-buffer for primary hits and ray-traced indirect light, soft shadows and reflections accumulated over frames while paused.
- **Files:** new `RayTracer.*`, `rt_common.glsl` (included), `pathtrace.frag`.
- **Check:** A/B with `F3` shows neon and lit windows reflected in the wet road. RT costs no more than 3 ms at 1080p on High. The Low preset turns it off.

### Phase 10: Delivery
- The README update: the new lab-topic mapping, controls, and how the ray tracing works. The Overlay help panel. Final soak runs across seeds. A performance pass. A viva demo script.

---

## 9. Verification (summary)
- **Build:**
  ```
  "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" OpenGLMiniProject.vcxproj -p:Configuration=Release -p:Platform=x64
  ```
- **Self-test:** `x64\Release\OpenGLMiniProject.exe --self-test`, run from the project folder. It covers geometry, network connectivity, conflict tables and the signal-plan safety matrix.
- **Soak:** `--soak 30 <seed>` over several seeds. It reports AI-AI overlaps (must be 0), vehicle-pedestrian overlaps (must be 0), the longest stop, throughput per junction and the density spread.
- **Plot:** `--plot` writes an ASCII map plus `network.png`.
- **Visual:** a phase-specific checklist is run in the app every phase, with the HUD's performance stats and GPU timer queries.

## 10. Risks and mitigations
| Risk | Mitigation |
|---|---|
| Scope is very large | Strict phase order. Each phase ships on its own. The grid size constant can shrink the city. |
| Frame rate with shadows, ray tracing and hundreds of objects | Batching, instancing, culling and quality presets from Phase 0 and Phase 2 onward. Timer queries show where the time goes. |
| Gridlock in a closed network | The exit-room check, a vehicle cap well below road capacity, and a deadlock detector in the soak test. |
| Shadow-map artefacts | Texel snapping, normal-offset bias, and the RT-shadow alternative for comparison. |
| Lab grading features lost | Flat/Gouraud/Phong, the Lab 3 lamp constants and spot light, the Lab 4 crates and specular map, and the Lab 5 Bezier meshes are all kept, and the README mapping is updated. |
| iGPU selected by default | Optimus and PowerXpress exports in `main.cpp`. |
