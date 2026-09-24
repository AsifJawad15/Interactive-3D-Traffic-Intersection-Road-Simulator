# Enhancement Plan: OpenGLMiniProject → Open-World Smart City Traffic Simulator

> Status: **Phases 0 to 4 complete. Phase 5 (vehicle variety) is next and waits for your go-ahead.** Written 2026-09-23.
> **Revised 2026-09-24 (Phase 4):** the player car has two views, a chase view above and behind it and a driver view over the bonnet (no cockpit interior); `C` locks the camera onto it and lets go again. The floodlight mast is gone: the city is lit by street lamps, signboards and neon. Pedestrian animation is planned in section 5.1.
> **Revised 2026-09-24:** every junction keeps its own permanent type (T-junction, signalised intersection or roundabout), and no intersection is ever turned into a roundabout (section 3.2).
> **Revised 2026-09-24 (later):** the city is closed. No road leads out of town: the 3×3 core sits inside a ring road, like a maze, and the same cars drive round it for ever with no respawning (section 3.2).
> **Scope revised 2026-09-23:**
> - A smaller 3×3 city.
> - Clouds and light rain only, with no storm and no fog weather.
> - Time-of-day presets with a moving sun, moon and shadows.
> - An "Enhanced" corner toggle replaces real ray tracing.
> - A driver-seat view.
> - 60 FPS at 1080p with no jitter.
> - No clustered lighting.
>
> Work goes **one phase at a time**: build → run → check → report → next phase only after review.
> Nothing is implemented all at once.

---

## 0. Progress checkpoints

| Phase | Status | Date | Branch |
|---|---|---|---|
| 0. Rendering foundations | ✅ Done and verified | 2026-09-23 | `enhancement/phase-0-rendering` |
| 1. Traffic core and collision fix | ✅ Done and verified | 2026-09-23 | `enhancement/phase-1-traffic` |
| 2. Smooth motion and 1080p | ✅ Done and verified | 2026-09-24 | `enhancement/phase-2-smooth` |
| 3. Road network | ✅ Done and verified | 2026-09-24 | `enhancement/phase-3-network` |
| 4. Player car, driver view, on foot | ✅ Done and verified | 2026-09-24 | `enhancement/phase-4-player` |
| 5 to 11 | Not started | | |

### ✅ Checkpoint 4: your car, on foot, and the night lights (2026-09-24)

**What changed**
- **The player (`Player.h/.cpp`):** a car of your own and yourself on foot, stepped in the same fixed 1/60 s steps as the traffic and drawn blended between steps.
  - **Driving:** a kinematic bicycle model. The steering lock shrinks from 34° when slow to 7° at 80 km/h, and the wheel turns at a limited rate. Top speed is 50 km/h, or 80 km/h with boost (`Shift`). Brakes, then reverse; a handbrake (`Space`) that lets the rear step out a little. Arrows or `W A S D`.
  - **Kerbs are bumps, not walls:** the car rides up onto sidewalks and lawns smoothly, so it can park off the road. It starts parked on the sidewalk south of X0.
  - **Collisions:** buildings, crates, tree trunks, lamp posts, signal poles, sign and billboard posts, the roundabout islands and AI cars are solid. Movement is sub-stepped (at most 20 cm per sub-step), so nothing can be tunnelled through. The car is pushed out along the shortest way, and the part of its speed going into the obstacle is removed with a little bounce. A glancing blow on a wall swings the nose round so the car scrapes along it; a head-on hit just stops it.
  - **On foot (`F`):** out through the driver's door once the car has nearly stopped, and back in when standing next to it. Walk or run where you look, step up onto kerbs, and slide along walls, cars and posts.
- **The AI yields to you (`Simulation.*`):** you are a `Guest` of the traffic. Every AI car slides its body (0.3 m larger all round) along its own path and into its next route, and brakes for you as for a car in front, stopping short of the first place it would touch you. It never enters a junction you stand in. If you stop in a lane, the cars behind wait (they cannot overtake).
- **Cameras, as you asked:**
  - `C` locks the camera onto your car (or onto you on foot) and follows it; `C` again lets go and returns to the free camera where it was.
  - `V` switches between two views of your car: the **chase view** above and behind it (a stiff spring) and the **driver view over the bonnet**. As you asked, there is no cockpit interior; only the bonnet is drawn. The driver view keeps a mouse head-turn of up to ±70°, `B` to look back, and a gentle lean under braking and cornering.
  - On foot you see through your own eyes. `M` is the top view (it replaced the old `C` cycle); `Tab` and `V` still follow an AI car and ride in it.
  - A single mouse jump of more than 300 px (the window taking the cursor back) is ignored, and captures ignore the mouse, so the view never snaps sideways.
- **Night lights, as you asked: street lamps, signboards and neon; the floodlight mast is removed.**
  - **Neon signs** above six doors at X0: HOTEL, CAFE, PIZZA, CINEMA, BAR and 24H. Every stroke of the `stb_easy_font` text becomes a thin glass tube that glows and blooms at night; the BAR sign stutters. Each throws a coloured spill light onto the pavement.
  - **Six billboards** on the lawns, with pictures generated at start-up (gradient, frame, two lines of text). At night five glow from behind in their own colours (a new `uEmissiveTextured` shader switch) and light the ground in front.
  - The one at X0 is lit from the front by a small lamp on an arm under it: this is now the **Lab 3 spot light** (cosine cut-offs 30° and 42°), aimed up at the picture so the cone lands on it and not on the grass.
  - All of these join the street lamps in the 32-light budget.
- **HUD:** a speedometer (with reverse), short messages that fade, and a **minimap** in the corner. It shows the roads, the roundabouts, every signal's two axis colours, every car, and your car (with its heading) or you. It is drawn looking down with north up, the same way round as the 3D top view.
- **`World.h/.cpp` (new):** one list of everything standing in the city (buildings, trees, crates, signs, signal heads, give-way signs, billboards, neon signs, the spot lamp), used both for drawing and for collisions, plus the ground height (road or kerb top) at any point.
- **Tools:**
  - `--player-test` is new (crashes, a walk into a wall, and laps of the ring among the AI with a camera judder check).
  - `--light-test` now includes the neon and billboard lights.
  - Capture views 8 (chase), 9 (driver view) and 10 (on foot).
- **Pedestrian animation** was researched but not built, as you asked: section 5.1 sets out the approach from the four reference projects.

**New files:** `Player.h/.cpp`, `World.h/.cpp`.
**Edited:** `Camera.h/.cpp`, `Scene.h/.cpp`, `Simulation.h/.cpp`, `Collision.h/.cpp` (push-out for boxes and circles), `Overlay.h/.cpp`, `shaders/scene.frag`, `main.cpp`, `README.md` and both project files.

**Verification results**
- **Build:** Release and Debug x64 build with no errors and no warnings.
- **`--player-test`:**

  | Check | Result |
  |---|---|
  | Head-on crash into a building at boost speed (47 km/h) | 0.000 m into the wall, stopped against it: PASS |
  | 35° crash (48 km/h) | 0.000 m into the wall, slid 6.6 m along it: PASS |
  | Walking (running) into a wall at 45° | 0.000 m inside, slid 7.2 m along it: PASS |
  | Autopilot laps of the ring road among 36 AI cars (240 s) | 1.5 laps; **AI into player 0**; overlaps left 0; judder 0.0003 in both the chase and the driver view (limit 0.02): PASS |

- **Traffic unchanged:** `--self-test` passes all 5721 checks. The 30-minute soaks with 36 cars give exactly the Phase 3 numbers on seeds 1 to 8 (0 overlaps, longest stop 28–48 s, busiest junction 8–11 %).
- **`--motion-test`:** judder 0.0007 to 0.0014, all PASS.
- **`--light-test`:** 193 lights (lamps, neon and billboards), 109,008 frames, largest change of one light in one frame 0.011, no pops: PASS.
- **1080p:** the chase view at night over 400 frames runs at 71–72 FPS, 99th percentile 16.1 ms, 2.6 ms of GPU time. The one frame near 108 ms is the known one-time driver stall.
- **Screenshots:** chase, driver and on-foot views by day and night, the neon and billboards at night, the spot-lit billboard (the cone on the picture, no pool on the grass), the minimap and the speedometer all look right.

**Known leftovers**
- AI cars cannot overtake, so a car you leave stopped in a lane holds up the traffic behind it until you move it (park on the sidewalk instead).
- You on foot are a simple block figure when seen from outside; the animated mannequin comes with the pedestrians (Phase 7, section 5.1).
- The car has no suspension: it glides up the 15 cm kerbs rather than bouncing over them.

### ✅ Checkpoint 3: the closed city road network (2026-09-24)

**What changed**
- **A closed maze instead of roads out of town (your request mid-phase):** there are no roads out of town and no respawning. The same cars drive round the city for ever.
  - The tested 3×3 core stays where it was. The four roads that used to leave town now run 100 m into a **ring road** at ±200 m, and two more links join the core to the ring (G2–G4 and G3–G6). The city is 400 m across with 19 junctions.
  - **Junction types:** X0 and X1 are signalised crossroads; R1 (fountain) and R2 are roundabouts; ST, ST2 and ST3 are signalised T-junctions; G and G2 to G7 are give-way T-junctions, where the straight road through has priority; and there are five bends (the NE bend and the ring's four corners). The old NW and SE bends became the T-junctions G2 and G3.
  - `RoadNetwork` now describes the **blocks as grid cells** (10 blocks: squares, long rectangles, and an L round the NE bend). `traceOutline` walks each block's boundary and turns every corner into the right kerb shape: a fillet, the inside or outside of a bend, or a roundabout circle. The same tracing, run the other way round the whole city, gives the outer kerb, a sidewalk and a lawn out to 900 m.
  - **Removed:** `leaveTown`, spawning (`trySpawn`), town-entry routes and the "left town" statistic. Cars are placed once at the start.
- **Lane changes after a junction:** on a closed ring, one lane per direction can only ever go straight on (every side road is on the other side). Without lane changes, 10 routes per lane formed a loop no car could enter or leave, and the self-test caught it.
  - At every crossroads and T-junction, a car going straight on may now move into the other lane 16–40 m past the centre, along an S of two equal arcs (`laneChangeRoute`, r = (l² + d²) / 2d).
  - These S-curves are measured as conflict zones like any crossing, so they stay collision-free by construction. Conflict sampling at intersections now reaches 44 m out.
  - A car keeping its lane has priority over one moving into it. Of two cars swapping lanes, the one moving towards the kerb goes first. Lane changes are half as likely in the route choice.
- **Default traffic:** 36 cars (was 24); at most 40 with `--cars`.
- **Rendering fixes from the screenshot review:**
  - **Washed-out top view:** a sun highlight on grass turned the whole city white from above. Lawns, the ground, the island grass and tree canopies now use a dim one-texel specular map (`Texture::makeGrey(20)`); the white tree tops are gone too.
  - **Depth fighting seen from high up:** the grass showed through the asphalt, and the R2 island had radial "spokes". With a 0.1 m near plane the 24-bit depth step at 430 m is 11 cm. The near plane now grows with camera height (0.1 m + 1.2 % of the height, at most 6 m), and the ground slab sits 1 m below the city.
  - **X0 props:** four of the old trees stood inside the new signal heads; they moved back onto the lawns, clear of the signals, lamps and buildings.
  - The HUD still listed the retired `M` key; it now lists Shift (fast camera), and `G` is described as advancing every signal.
- **Tools:**
  - `--light-test` is new: a headless night drive along every road plus a full turn in every junction, checking that no light on screen changes strength by more than 0.1 in one frame. `LightBudget` was split out of `LightManager` for it, with no OpenGL.
  - Capture views 6 (R2) and 7 (the ring road). Views 3 and 5 and the top camera are raised for the bigger city.

**New or rewritten:** `RoadNetwork.h/.cpp` (layout, cell blocks, outline tracing), `LightManager.h/.cpp` (`LightBudget`, `PointLight::fromStreetLamp`), `README.md`.
**Edited:** `TrafficBuild.cpp`, `Simulation.h/.cpp`, `RoadRenderer.cpp`, `MeshBuilder.h/.cpp` (ear clipping, inward walls), `Scene.h/.cpp`, `Camera.cpp`, `Texture.h/.cpp`, `Overlay.cpp`, `main.cpp`.

**Verification results**
- **Build:** Release x64 builds with no errors and no warnings.
- **`--self-test`:** all 5721 checks pass (19 junctions, 220 routes, 822 conflict zones). The whole network is strongly connected, every route has a successor and a predecessor, and every body stays clear of kerbs, islands and the outer kerb.
- **30-minute soaks, seeds 1 to 8:**

  | Cars | Overlaps | Longest stop | Busiest junction | Result |
  |---|---|---|---|---|
  | 25 | 0 | 22–31 s | 8–9 % | 8 of 8 pass |
  | 36 (default) | 0 | 28–48 s | 8–11 % | 8 of 8 pass |
  | 40 (the cap) | 0 | 29–43 s | 8–11 % | 8 of 8 pass |

  The old weak spot is gone: 40 cars used to fail one seed in three (a 70 s wait at R2). At 60 cars R2 saturates (waits of 62–104 s, 3 of 6 seeds fail), so the cap stays at 40.
- **`--motion-test`:** judder 0.0007 (144 Hz), 0.0014 (60 Hz), 0.0011 (75 Hz). All pass.
- **`--light-test`:** 182 lamps, 109,008 frames. The largest change of one visible light in one frame is 0.011, so nothing pops. Within a lamp's reach there are never more than 32 lamps, so the budget never fills and the fade is purely by distance.
- **1080p, 400 frames per view:** a steady 72 FPS, 99th percentile 15–19 ms, and 1.5–2.8 ms of GPU time. Each run has one ~70 ms frame: the known one-time driver stall (Checkpoint 2).
- **Screenshots, day and night, views 0 to 7:** roads, kerbs, sidewalks, markings, arrows, stop lines and signal heads look correct, including the L-shaped block, the outer sidewalk and both roundabouts. Night roads are lit.

**Known leftovers**
- **Top view orientation:** the top view shows +x ("east") on the left. That is the existing right-handed Y-up convention with +z called north, unchanged since Phase 1; only the labels would change.
- **Buildings:** only the X0 core has buildings; the other blocks are lawn until Phase 6 (city dressing).
- **Lane changes:** these only happen just past a crossroads or T-junction, not along a road or at a roundabout.

### ✅ Checkpoint 2: smooth motion and 1080p (2026-09-24)

**What changed**
- **Render interpolation:** each vehicle keeps its pose from the previous 60 Hz step. Every frame draws the blend with α = backlog / step, and headings, wheel spin and steering blend along the short way round. The island animation is blended the same way. A car placed or re-entering starts with previous = current, so it never streaks across the scene.
- **Cameras:** the follow camera rides a critically damped spring (the exact "SmoothDamp" form, stable at any frame time). The driver camera is rigidly attached to the blended pose.
- **Frame timing:** the real frame time drives everything; only stalls over 0.25 s are cut short. Up to 8 simulation steps run per frame, and any leftover beyond that is dropped rather than allowed to spiral.
- **Frame pacing (new, measured):**
  - On this laptop's 144 Hz screen, full-rate frames often miss a refresh and alternate between 6.9 and 13.9 ms. The average is about 105–128 FPS, but it reads as stutter.
  - The new default *steady* pacing draws on every second refresh when the screen is 120 Hz or faster, giving an even 72 FPS with every frame 13.9 ms. A 60 Hz screen keeps every refresh.
  - `F7` switches to full rate.
- **1080p:** the window opens at 1920×1080, or maximised when the screen itself is only 1080p tall. `F11` (or `--fullscreen`) gives true fullscreen at the monitor's own mode.
- **Render scale:** the scene renders at 100 % or 67 % (720p inside 1080p), and the tonemap pass scales it up with light sharpening done in display space.
  - Automatic mode drops to 67 % after 3 s below 55 FPS (or over 14.5 ms of GPU time), and returns after 10 s of comfortable headroom.
  - `F6` cycles Auto, Native and 720p.
- **Measurement:**
  - OpenGL timer queries measure GPU time without stalling.
  - `F5` shows a graph of the last 240 frame times.
  - The HUD shows the render size, pacing and GPU time.
- **No hitches from our side:**
  - The window stays hidden until one full warm-up frame has been drawn.
  - The simulation step, HUD text and uniform names no longer allocate.
  - `Shader` looks uniforms up by `std::string_view` with a transparent hash, so a literal never builds a string.
- **New tools:**
  - `--motion-test` measures judder headlessly.
  - `--capture` now reports average, 99th-percentile and worst frame times, GPU time, and for each slow frame whether the time went into events, our work or the buffer swap.
  - New capture options: `--size`, `--fullscreen`, `--scale`, `--full-rate` and `--graph`.

**New files:** `FrameStats.h/.cpp` (frame and GPU timing, render scaler).
**Edited files:** `Simulation.h/.cpp`, `Camera.h/.cpp`, `Scene.h/.cpp`, `PostProcess.h/.cpp`, `shaders/tonemap.frag`, `Overlay.h/.cpp`, `Shader.h/.cpp`, `main.cpp`, `README.md` and both project files.

**Verification results**
- **Build and self-test:** the Release x64 build is clean, with no errors and no warnings. `--self-test` passes all 642 checks.
- **Traffic unchanged:** the 30-minute soaks give the same numbers as Checkpoint 1 in both modes, and 0 overlaps over 5 seeds. The simulation is untouched and still deterministic.
- **`--motion-test`** (judder: 0 means perfectly even motion, 1 or more means visible stepping):

  | Display | Without interpolation | With interpolation |
  |---|---|---|
  | 144 Hz | 1.997 | **0.0015** |
  | 60 Hz | 1.076 | **0.0031** |
  | 75 Hz | 0.521 | **0.0025** |

- **Fullscreen 1920×1080 on the RTX 3050, steady pacing:**

  | Scene | Average | 99th percentile | GPU time |
  |---|---|---|---|
  | Noon overview, 60 s | 72 FPS (13.91 ms) | 14.87 ms | 2.5 ms |
  | Night street, roundabout | 72 FPS | 14.84 ms | 2.4 ms |
  | 720p render scale | 72 FPS | 14.95 ms | 2.0 ms |
  | Full-rate pacing | 128 FPS | 16.9 ms | 2.6 ms |

  The GPU uses about 2.5 ms of the 16.7 ms that 60 FPS allows, so there is a large margin for Phases 3 to 10.
- **Screenshots:** 1080p fullscreen, 720p-scaled and night views all look correct. The frame graph is flat green.

**Known leftovers (outside the program)**
- **One-time stall at launch:** once, about 6 s after launch, `SwapBuffers` stalls for about 105 ms, in every mode, windowed and fullscreen, and with the GPU timers turned off.
  - Our own work in that frame is 0.5 ms, so it is the NVIDIA hybrid-graphics driver or the Windows compositor settling in.
  - Apart from that, a 60 s steady run had one 32 ms frame.
- **Window size:** with a title bar, a windowed 1920×1080 is clamped by Windows to 1920×1055 on this 1080p screen. Use `F11` for exact 1080p.

### ✅ Checkpoint 1: traffic core and collision fix (2026-09-23)

**What changed**
- **Conflict zones, measured rather than guessed:**
  - At start-up, every pair of routes from different approaches is sampled every 25 cm near the middle. A car-sized box, plus a 30 cm margin, is placed at each sample.
  - Each connected patch of overlapping positions becomes one zone, stored as a car-centre interval on each route.
  - The result is 30 zones for the signal routes and 60 for the roundabout routes.
  - This replaces the plan's fixed 2.8 m distance test, which was too small for the tight 3 m right turn, where a car body swings well off its path.
- **Shared spans:** routes that run along the same line (a shared approach lane, a shared exit lane, a shared stretch of ring) are found the same way. Cars on them follow each other.
- **Commit and claim:**
  - A car enters only after claiming every zone on its way at once.
  - It releases each zone as its centre leaves it.
  - Committed cars never wait on a claim; they only follow the car in front. So there is no deadlock and no collision.
  - A car commits only when it is first in its lane, has room at the exit, and has no priority traffic within 3.5 s.
- **Stop lines** move back automatically so each lies before its route's first zone. This fixes the roundabout entry, where the old give-way position left the car's nose inside the ring lane.
- **Car following:**
  - The IDM, with gaps measured along the route and across shared spans.
  - Braking ahead of corners from `v² = v_c² + 2ad`.
  - The sigmoid easing kept as a jerk limiter.
  - A 0.6 m hard clamp as a safety net.
- **Signals:**
  - A protected left-turn arrow, shown only when a left-turner is at the front of a lane. It lasts up to 12 s so the two opposing lefts, whose arcs cross, can go one after the other.
  - Green, yellow and a 2.5 s all-red.
  - Actuated green: 6 s minimum, 16 s maximum, and it rests on green when nobody waits across.
- **Realistic edge rules:**
  - A car committed on green that has not reached its line when the light changes stops if it comfortably can, giving its claims back.
  - A left-turner waiting at the front clears on yellow.
  - At equal priority, the car that waited longer goes first.
- **Safe re-entry:** a car leaving the scene waits hidden until its approach has a 16 m gap.
- **Routes:** `Route` gains `curvature`, `translated()` and `reversed()`. Front-wheel steer is now `atan(wheelbase / r)`.
- **Scene:** each signal head has a small left-arrow lens. The HUD shows `OVERLAPS`, and the app runs 8 cars by default (`--cars N`).
- **New tools:**
  - `--soak <minutes> <seed> [--cars N] [--mode ...]` is a headless endurance test.
  - `--trace [T]` prints why each waiting car is waiting.
  - `--plot` now marks the conflict zones.

**New files:** `Collision.h/.cpp` (oriented boxes, separating-axis test) and `TrafficBuild.cpp` (routes, shared spans, conflict zones, `--self-test`, `--plot`).
**Edited files:** `Simulation.h/.cpp` (rewritten), `Route.h/.cpp`, `Scene.h/.cpp`, `Overlay.h/.cpp`, `main.cpp`, `README.md` and both project files.

**Verification results**
- **Build:** Release x64 is clean, with no errors and no warnings.
- **Self-test:** `--self-test` passes all 642 route, conflict-table and signal-safety checks.
- **Soak, 30 simulated minutes, 12 cars, seeds 1 to 5, both modes:** all 10 runs pass.

  | Mode | Body overlaps | Closest body gap | Longest stop | Trips per 30 min |
  |---|---|---|---|---|
  | Signals | **0** | 1.94 m | 40.9 to 49.0 s | 455 to 486, even across N/E/S/W |
  | Roundabout | **0** | 1.02 m | 45.8 to 51.9 s | 369 to 397, even across N/E/S/W |

- **Other car counts:**
  - 8 cars (the app default), seeds 1 to 3: all pass.
  - 16 cars (stress), seeds 1 to 3: signals all pass. The roundabout has 0 overlaps in every run, but one seed reached a 63 s stop. At 16 cars the single-lane roundabout is at capacity (trips level off at about 390).
- **Before and after:** the unchanged code gridlocked within 63 s (signals) and 355 s (roundabout).
- **In the app:** captures at noon in both modes show cars queuing behind the crosswalks, entries holding at the roundabout, `OVERLAPS: 0` on the HUD, and 144 FPS.

**Known limitations, handled later**
- **Conservative roundabout:** a circulating car claims every entry on its way when it commits. Entering cars sometimes wait while a circulating car is still some way off. This is the price of the deadlock-free guarantee. Phase 3's wide roundabouts, with their bigger ring, can take a finer claim rule.
- **Opposing left turns:** on this narrow junction the two left arcs cross each other, so they cannot turn together. The 4-lane junctions in Phase 3 have separate turn lanes.
- **Fixed-step motion:** the motion is still drawn at the 60 Hz simulation step, so there is some stepping on a 144 Hz screen. Phase 2 adds render interpolation.

### ✅ Checkpoint 0: rendering foundations (2026-09-23)

**What changed**
- **Discrete GPU:** the program now asks the drivers for the discrete GPU. It reports `NVIDIA GeForce RTX 3050 Laptop GPU` at start-up.
- **HDR pipeline:** the scene renders into a 4× MSAA RGBA16F target. It is resolved, then bloom, ACES tone mapping and sRGB encoding are applied.
- **Colour space:** colour textures load as sRGB, and base colours are decoded to linear light in the shader. This fixes the crushed, black-looking dark areas.
- **Lighting:** hemisphere ambient (sky above, ground bounce below), a stronger sun, emissive surfaces boosted so lamps and signal lenses bloom, and exposure that adapts from day to night.
- **Sky and atmosphere:** a sky gradient that follows the time of day, a sun disc and glow, a moon, twinkling stars, and height fog. The sky and fog share `shaders/atmosphere.glsl`, so distant ground fades into the horizon.
- **World edge:** the ground now reaches 2 km, and the far plane is 1200 m.
- **Engine:** uniform locations are cached, shaders support `#include`, the simulation runs at a fixed 60 Hz step, and the HUD reuses one text buffer.
- **New verification tool:** a capture mode that renders a fixed view and saves a PNG.
  ```
  OpenGLMiniProject.exe --capture out.png --view 0..3 --time 22 --shading 0..2 --roundabout --no-hud --frames 60
  ```

**New files:** `Framebuffer.h/.cpp`, `PostProcess.h/.cpp`, `Sky.h/.cpp`, `Screenshot.h/.cpp`, `shaders/post.vert`, `shaders/bloom_down.frag`, `shaders/bloom_up.frag`, `shaders/tonemap.frag`, `shaders/sky.frag`, `shaders/atmosphere.glsl`.
**Edited files:** `main.cpp`, `Shader.*`, `Texture.*`, `DayNight.*`, `Camera.*`, `Scene.cpp`, `Overlay.*`, `shaders/scene.vert`, `shaders/scene.frag`, and both project files.

**Verification results**
- The Release x64 build is clean, with no errors and no warnings.
- `--self-test` passes: all route and signal checks.
- Captures were taken at noon, dawn, dusk and night from four views. Roads read as grey asphalt, and night is dark but readable.
- Flat, Gouraud and Phong all render. The frame rate sits at the 144 Hz vsync cap.

**Known leftovers, handled by later phases**
- The round tree canopies look glossy under the brighter sun. Phase 6 replaces them.
- The ground beyond the city is a flat green plane. The outskirts and skyline in Phases 3 and 6 replace it.

**Collision bug: measured, not guessed**
A throwaway headless probe ran the *unchanged* traffic code for 30 simulated minutes. It checked car bodies as oriented boxes every step.

| Mode | Body overlaps | Closest body gap | Gridlock starts | Cars frozen at the end |
|---|---|---|---|---|
| Signals | 0 | 0.35 m | about 63 s | 6 of 6, all within 4.4 m of the centre, on crossing headings |
| Roundabout | 0 | 0.85 m | about 355 s | 6 of 6, four of them locked on the ring |

**Conclusion:** it is a **logic bug** in the traffic rules, not a rendering problem. The cars never pass through each other. They enter the box together, stop centimetres apart nose-to-side, and each waits forever for the other. On screen that permanent gridlock looks like a crash. The causes are listed in section 2.2, and the fix is Phase 1 (section 4).

---

## 1. Context

The project is a working OpenGL 3.3 Core lab project (CSE 4102). It has one four-arm intersection
that can switch between signals and a roundabout (`M`), six cars, a day/night cycle, four street
lamps, one floodlight, Flat/Gouraud/Phong switching, Bezier surfaces and textures.

You want it to become a small but lively city that runs smoothly:
- **Roads:** a closed road network (a 3×3 core inside a ring road, with no roads out of town) and wider 4-lane roads. It mixes signalised intersections, roundabouts and T-junctions, each in its own permanent place.
- **Vehicles:** car, taxi, SUV, van, pickup, bus, truck, motorbike, police and ambulance.
- **Pedestrians:** walking on footpaths and crossing at signals and zebra crossings.
- **City dressing:** enough buildings, shops, props and trees that the city never looks blank, but well short of GTA density. No black-looking roads.
- **Player:** a player car driven with the arrow keys, with a **driver-seat (cockpit) view**, an on-foot view, and real collision.
- **Time of day:** named presets (Morning, Noon, Afternoon, Evening, Night). The sun and moon move and the shadows move with them.
- **Weather:** **clouds** in the sky and **light rain**. No storm and no fog weather.
- **"Enhanced" corner button:** one button in a screen corner. It switches on a ray-traced *look* (screen-space reflections, ambient occlusion, soft shadows, sun shafts) with no real ray tracing.
- **Smoothness:** **60 FPS at 1080p** with no jitter, and never below 50 FPS. A 720p internal resolution is used as a fallback.
- **Collision fix:** a fix for the bug where cars collide in the middle of the intersection.

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
3. **Straight-ray conflict detection.** `closestLeaderGap` ([Simulation.cpp:402](Simulation.cpp#L402)) only sees cars within 2.2 m sideways of a straight line along the current heading. A crossing car becomes visible only once it is already in front. Travel is then clamped to 0 and both cars freeze, stopped centimetres apart. Each waits for the other forever, a deadlock that looks like a collision (measured in Checkpoint 0).
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

### 2.5 Why motion can look jittery
- **No interpolation:** the simulation steps at a fixed 60 Hz ([main.cpp:325](main.cpp#L325)), but rendering draws the latest simulation state with no interpolation. On the 144 Hz display, some frames repeat a car's position and others jump by a full step, so cars appear to stutter.
- **Hidden stalls:** frame `dt` is clamped to 0.05 s ([main.cpp:338](main.cpp#L338)), which hides stalls instead of revealing them.
- **Window size:** the window opens at 1280×720 ([main.cpp:253](main.cpp#L253)).
- **Cursor:** the mouse cursor is always locked ([main.cpp:304](main.cpp#L304)), so nothing on screen can be clicked yet.

---

## 3. Key design decisions

### 3.1 "Enhanced" mode: a ray-traced look without ray tracing
OpenGL has **no hardware ray-tracing API**. A full software BVH ray tracer is expensive and would put the 60 FPS target at risk. So real ray tracing is dropped. Instead, one toggle gives the *look* of partial ray tracing, using screen-space techniques. These still march rays, but against the depth buffer instead of against scene geometry.

**The toggle**
- **Button:** a button in the **top-right corner** labelled `ENHANCED: OFF` / `ENHANCED: ON`, drawn by `Overlay`.
- **Clicking:** it is clicked with the mouse, through a new `glfwSetMouseButtonCallback` and a hit test in framebuffer pixels (DPI-aware).
- **Cursor:** the cursor is free in Follow, Top, Chase and Driver camera modes. In Free-cam mode, holding **`Left Alt`** releases it so the button can be clicked.
- **Shortcut:** **`F3`** does the same thing from the keyboard.
- **Time buttons:** small **Morning / Noon / Afternoon / Evening / Night** buttons sit under it in the same corner panel.

**What turns on when Enhanced is ON (all on OpenGL 3.3)**
| Effect | What you see |
|---|---|
| **Screen-space reflections (SSR)** | Wet roads, puddles, car paint and shop glass reflect neon, lamps, lit windows and the sky. Rays march against the depth buffer. |
| **SSAO (ambient occlusion)** | Soft contact darkening under cars, along kerbs and walls, and around props. |
| **Soft shadows (PCSS)** | Shadows are sharp near the object and soften with distance, instead of a uniform blur. |
| **Screen-space contact shadows** | A short ray march towards the sun gives thin, crisp contact shadows that the shadow map is too coarse for. |
| **Sun shafts** | Light rays from a low sun in the morning and evening, blocked by buildings and trees. |
| **Stronger bloom** | Neon and lamps glow a little more. |

**How it works**
- **Scene pass:** it writes a second render target (MRT) holding view-space normals and roughness.
- **Resolve:** the multisampled buffers are resolved with a nearest-filter blit.
- **Cost:** SSR and SSAO run at **half resolution** with a depth-aware (bilateral) upsample. The budget for Enhanced is **3 ms or less at 1080p**.
- **When OFF:** plain forward shading with PCF shadow maps. This is the fast default.

### 3.2 City layout: a closed maze, a 3×3 core inside a ring road (400 × 400 m)
**Every junction has one permanent type** (revised 2026-09-24). T-junctions, signalised intersections and roundabouts each have their own place in the city. A roundabout never replaces an intersection, so the city keeps a mix of all three.
**The city is closed** (revised later on 2026-09-24). No road leads out of town. The four roads that used to leave it now run into a ring road, so cars never despawn or respawn: they drive round the maze for ever.

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
```

| Mark | Type | Where |
|---|---|---|
| **X0** | Signalised 4-way intersection: the main crossroads, and the Phase 1 junction with its left-turn arrows, all-red and pedestrian signals | Centre |
| **X1** | Signalised 4-way intersection | Top middle of the core |
| **R1** | 4-arm roundabout with the Bezier **fountain** island | Middle right of the core |
| **R2** | 4-arm roundabout | Bottom middle of the core |
| **ST**, **ST2**, **ST3** | Signalised T-junctions | Bottom left of the core; top and bottom of the ring |
| **G**, **G2** to **G7** | Give-way T-junctions (the straight road through has priority) | Round the core and on the ring |
| **NE bend**, **NWc**, **NEc**, **SWc**, **SEc** | Bends | The core's NE corner and the ring's four corners |

- **Junctions:** 2 signalised intersections, 2 roundabouts, 3 signalised T-junctions, 7 give-way T-junctions and 5 bends: 19 in all.
- **The `M` key:** retired in Phase 3. The island-rising animation belonged to the single-junction demo; in the city, the roundabouts simply are roundabouts.
- **Blocks:** 10 blocks made of grid cells (squares, long rectangles and an L round the NE bend). Phase 6 dresses them:
  - a park with a small plaza;
  - a gas station with a short row of shops;
  - blocks of shops with apartments above.
- **Scale:** the spacing (100 m) is a single constant, and blocks are lists of grid cells, so the maze can change shape by editing `RoadNetwork::makeCity`.
- **Open-world feel:** vehicles never despawn. They random-walk the network forever, with density-aware turn choices so no single area clogs. Buses run a fixed loop line.
- **Outskirts:** a sidewalk runs round the outside of the ring, then lawn out to 900 m, where the Phase 0 horizon haze blends the world edge into the sky. Later phases may add a row of low buildings and a distant skyline.

### 3.3 Road cross-section (wider roads)
- **Lanes:** 2 lanes per direction, each 3.5 m wide. Lane centres are at ±1.75 m and ±5.25 m, with a double yellow centre line.
- **Width:** the carriageway half-width is 7.0 m. Kerbs are 0.15 m high. Sidewalks are 4.5 m wide. Buildings are set back 12 m from the centreline.
- **Signalised junctions:** the corner kerb radius is 5 m. The right turn uses radius 6.75 (outer lane) and the left turn uses radius 13.75 (inner lane). Both come from the existing tangency formulas with the new lane offsets.
- **Roundabouts:** a single wide circulating lane of radius 13, an island of radius 9.5 with an apron, and entry and exit arcs of radius 8. Entry and exit arcs are built for both approach lanes, with the existing tangent-circle construction. Splitter islands and zebra crossings sit on every arm.
- **Lane discipline:** the inner lane goes straight or left, and the outer lane goes straight or right. Any lane may enter a roundabout. A car going straight on through a crossroads or T-junction may move into the other lane just past it (an S of two equal arcs, measured as a conflict zone). The closed ring needs this: there, one lane per direction can only go straight on.

### 3.4 Lighting: a simple light budget (no clustered lighting)
The renderer stays **forward**, which keeps the Flat/Gouraud/Phong demo working.
- **Light list:** lights live in a UBO with room for **up to 32 point lights and 8 spot lights**.
- **Per frame:** the CPU frustum-culls all lights, then keeps the ones that matter most, ranked by intensity over distance to the camera.
- **Always in the list:** the four central "lab" lamps, which keep the Lab 3 constants k_c=1, k_l=0.09, k_q=0.032. The Lab 3 spot light is the lamp under the billboard at X0 (since Phase 4; the floodlight mast is gone). Neon spill and billboard glow share the budget.
- **Lamps beyond about 80 m:** drawn as emissive bulbs only. Bloom and the horizon haze hide the missing light.
- **Falloff:** lights use a windowed falloff, so a light switching in or out of the list never pops.

---

## 4. Traffic and collision design (the core fix)

### 4.1 Routes and shared spans (built in Phase 1)
- **Movements:** each junction movement is one `Route`, running from edge to edge through the junction. The single junction has 24 of them.
- **Shared spans:** where two routes run along the same line, the stretch is found once and stored with its distance offset. This covers a shared approach lane, a shared exit lane, and a shared stretch of the roundabout ring.
- **Following:** cars follow each other across shared spans. A few metres of "tail" after a split keep the two cars aware of each other while they are still close.
- **Phase 3 network:** consecutive junction movements chain end to start, so following simply continues into the next junction.
- **`Route` helpers:** `translated()` and `reversed()`, plus curvature per sample.

### 4.2 Conflict zones (precomputed once)
- **Detection (as built):**
  - For each pair of routes from different approaches, sample both every 0.25 m within 20 m of the junction.
  - Place a car-sized oriented box, plus a 0.3 m margin, at each sample, and mark every pair of positions where the boxes overlap. Pairs that are just following each other on a shared span are skipped.
  - Each connected patch of marks is one **conflict zone**, with an [in, out] interval of car-centre distance on each route.
  - While a car's centre is outside its interval, nothing on the other route can touch it.
- **Why not a distance threshold:** the original 2.8 m test was dropped. It under-covers tight turns, where the body swings well off its path.
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

### 4.7 Fixed timestep with render interpolation
- **Fixed step:** the simulation runs at a fixed 60 Hz with an accumulator. It is deterministic and seeded, so soak tests are reproducible.
- **Interpolation:** each vehicle, pedestrian and the player keeps its **previous and current pose** (position and heading). Rendering blends the two with α = backlog / step, using slerp for heading.
- **Cameras:** they follow the blended pose.
- **Result:** motion is smooth at any refresh rate (60, 144 or anything else), while the simulation stays fixed-step.

---

## 5. World, props and visuals

- **Vehicles (`VehicleTypes.h/.cpp`):**
  - A *lofted body generator*: a Bezier side profile is swept across the width with the roof tapered inward (ties in Lab 5).
  - Types: sedan, hatchback, SUV, taxi (roof sign), van, pickup, 12 m bus, box truck, motorbike with rider, police car and ambulance (flashing light bars that are real lights at night).
  - Each type has its own length, width, IDM parameters and colour palette.
  - Brake lights respond to deceleration. Indicators blink *before* turns. Headlights come on at night and in rain.
- **Player car interior (for the driver-seat view):**
  - The model: a dashboard, a steering wheel that turns with the steering input, a speedometer with a moving needle, A-pillars, the bonnet visible through the windscreen, and a rear-view mirror frame.
  - It is only drawn when the camera is inside the car.
- **City generator (`World.h/.cpp`, seeded), minimal but never blank:**
  - **Buildings:** about **30 to 40 buildings** in 4 to 5 prebuilt styles (low shop row, 3 to 5 storey apartment, office tower, hotel, small house), with varied heights and colours.
  - **Shops:** ground-floor shops on the main streets only, with glass fronts, striped awnings and lit interiors at night. About half have **neon signs**. The sign text is geometry from stb_easy_font, emissive, with bloom, a gentle flicker and a coloured spill light.
  - **Shop types:** cafe, pharmacy, bank, pizza, 24/7 and hotel.
  - **Upper floors:** a procedural window grid in the shader, with random windows lit at night.
  - **Rooftops:** some carry tanks, AC units or a billboard. Not every roof has clutter.
  - **Special blocks:** a park with a pond and small plaza, and a gas station with a lit canopy.
- **Street furniture:** lamps every 30 m, and a few benches, bins, bus stops with lit ad panels, stop, yield and speed signs, parked cars and planters per block. The Lab 4 crates stay at the central junction.
- **Realistic trees (`TreeGenerator`):**
  - Structure: a Bezier trunk plus 2 to 3 levels of branches.
  - Leaves: clusters of **alpha-tested leaf cards** with an RGBA procedural leaf texture, normals bent outward, and wind sway in the vertex shader.
  - Species: broadleaf, conifer, and palm on the plaza.
  - Placement: street trees along the main roads and a cluster in the park. There are fewer than a dense city would have.
  - Rendering: 4 to 6 prebuilt variants, drawn with instancing.
- **Pedestrians (`Pedestrians.h/.cpp`):**
  - Model: hierarchical, with hips, torso, head, two-part arms and two-part legs.
  - Walk cycle: limb phase is driven by distance walked, the same idea as wheel rotation.
  - Variety: different skin, shirt, trousers and hair colours. Umbrellas appear in rain.
  - Movement: a sidewalk graph that loops around each block and connects blocks through crossings.
  - Rendering: 60 to 100 walkers, **instanced per body part**, about 10 draw calls for all of them.
  - **Animation (added 2026-09-24, plan only):** see section 5.1.

### 5.1 Pedestrian animation: approach and references (added 2026-09-24, not yet built)
Four projects were reviewed as references. What each offers, and whether it can be used here:

| Reference | What it is | Use in this project |
|---|---|---|
| [ozz-animation](https://github.com/guillaumeblanc/ozz-animation) | C++17 runtime for skeletal animation: sampling, blending (including partial and additive), local-to-model, two-bone and aim IK, skinning helpers. Offline tools convert glTF / FBX into compact `.ozz` files. Renderer-agnostic, CMake, **MIT licence**. | The best candidate **if** we ever import rigged characters: its runtime would do sampling and blending, and our own shader would do the skinning. It is a dependency, though, and needs an asset pipeline. |
| [colonelsalt/animation-blending](https://github.com/colonelsalt/animation-blending) | C++ / OpenGL skinned character (Mixamo) with an **animation graph**: idle, walk, jog, jump, roll, with smooth cross-fades between them. Built on LearnOpenGL's skeletal animation chapter. No licence stated. | **Reference only** (no licence, so no code copied): the state-machine design and the cross-fade timing. |
| [Futuramistic/Animation](https://github.com/Futuramistic/Animation) | C++ / GLUT player for **BVH** motion capture (walk, jog, jump) with forward kinematics in matrix and quaternion form, a Jacobian-inverse **IK** arm, and a "mannequin" drawn from simple shapes on the bones. No licence stated. | **Reference only**: BVH parsing, quaternion joints, and the mannequin idea. The mannequin fits this project's rule of authored geometry with no model files. |
| [SMPL-Scene-Viewer](https://github.com/climbingdaily/SMPL-Scene-Viewer) | Python / Open3D viewer for SMPL body-model sequences and point-cloud scenes. The SMPL model needs registration; **CC BY-NC-SA** (non-commercial). | **Not used at runtime.** At most an offline way to look at motion data. |

**Proposed design (for Phase 7):**
- **Body:** a mannequin of the hierarchical parts above (hips, torso, head, two-part arms and legs), built from our own Bezier and box meshes and hung on a ~15-joint skeleton. No imported character mesh, so there is no skinning pass and every part can be instanced.
- **Motion:** a few short BVH clips (idle, walk, jog, wait at the kerb, look both ways) from a freely usable motion-capture source, loaded by a small BVH reader of our own. Joints are stored as quaternions, resampled to 30 Hz, with the root motion taken out.
- **Blending:**
  - Idle, walk and jog blend by speed, synchronised by the gait phase so the feet stay in step.
  - States change through a small animation graph (after colonelsalt): Idle → Walk → WaitAtKerb → Cross → Walk, with 0.2 to 0.3 s cross-fades.
  - The playback rate follows the distance walked, so the feet never slide (the same idea as wheel rotation).
- **Feet on kerbs:** two-bone leg IK lifts a foot onto the 0.15 m kerb and the island steps (the analytic two-bone solution, as in ozz and Futuramistic's IK chapter).
- **Crowds:** 60 to 100 walkers, with distant ones animated at a lower rate and far ones frozen on a pose. Poses are blended between simulation steps like the cars.
- **Traffic:** pedestrians are `Guest`s to the AI traffic, the same mechanism the player uses since Phase 4, so cars already yield to them. Crossings claim conflict zones as in section 4.6.
- **The player on foot** uses the same mannequin when seen from outside, replacing the Phase 4 block figure.
- **If rigged glTF characters are ever wanted instead:** switch the runtime to ozz-animation (MIT), with skinning matrices in a uniform buffer. That would end the "no model files" rule, so it is a decision for later.
- **Sky and atmosphere:** a sky gradient that follows sun elevation, sun glow, moon, stars, and a faint horizon haze coloured by the sky (all done in Phase 0). There is no fog weather. The haze only blends the far edge of the world into the sky.

- **Sun, moon and time presets (`DayNight.*`):**
  - **Presets:**

    | Preset | Time | Look |
    |---|---|---|
    | Morning | 07:00 | Low sun in the east, warm light, long shadows pointing west |
    | Noon | 12:00 | High sun to the south, white light, short shadows |
    | Afternoon | 15:30 | Sun sinking to the south-west, shadows lengthening towards the north-east |
    | Evening | 18:30 | Sunset in the west, orange and pink sky, very long shadows pointing east, lamps and neon switching on |
    | Night | 22:00 | Moon up, stars, street lamps, neon and lit windows |

  - **HUD:** it shows the preset name and the clock, for example `Afternoon 15:30`.
  - **Sun path:** today the sun moves on a circle straight through the zenith ([DayNight.cpp:79](DayNight.cpp#L79)), so at noon it is directly overhead and shadows vanish. The new path is tilted like a real mid-latitude sky. The sun rises in the east, peaks at about **60° elevation to the south**, and sets in the west, so noon still has visible short shadows.
  - **Moon:** it follows the opposite arc and gives faint blue moonlight with soft shadows at night.
  - **Changing preset:** the sun and moon **glide** to the new position over about 3 s instead of snapping, so shadows sweep smoothly across the city.
  - **Shadow map:** it always follows the current sun, or the moon at night.
  - **Colour:** sunrise and sunset tint the sky, the clouds and the sunlight colour.
  - **Automatic cycle:** it still works (`T`), and passes through all the presets.

- **Weather (`Weather.h/.cpp`):** the states are **Clear → Cloudy → Rain**, cycled with `K`. Every change blends over about 10 s, so the sky never snaps. There is no storm state and no fog state.

- **Clouds (built on the Phase 0 sky shader):**
  - **Where they live:** a cloud layer at about 1.5 km altitude, drawn inside `sky.frag`. Each sky pixel finds where its view ray meets the cloud plane and samples fractal noise (fbm) there.
  - **Motion:** the noise scrolls with the wind, so clouds drift slowly across the top of the sky.
  - **Coverage and density:** two uniforms set by the weather. Clear has a few wisps, Cloudy is broken cloud, and Rain is full overcast.
  - **Lighting:** cloud edges facing the sun get a bright silver lining and the thick middles are darker underneath. The colour follows the time of day: white at noon, orange and pink at sunset, and dark blue-grey lit by the moon at night. Clouds hide the stars and moon behind them.
  - **Effect on the scene:** overcast weakens direct sunlight and raises the soft sky ambient, which also softens shadows in Phase 8. It greys the horizon colour a little, which the haze picks up automatically through `atmosphere.glsl`.
  - **Cloud shadows:** the same noise is sampled at each ground point along the sun direction. Soft shadow patches then drift across the city on partly cloudy days.

- **Light rain:**
  - **Falling drops:** up to about 5,000 instanced, motion-stretched streaks in a cylinder around the camera, which follows the camera. The streaks lean slightly with the wind. Drops are brightened by nearby street lamps and neon, so rain glitters under lights at night.
  - **Splashes:** small expanding rings on the road, sidewalks and car roofs near the camera.
  - **Wetness:** a value from 0 to 1 that rises over about 60 s of rain and dries slowly afterwards. Wet surfaces get darker albedo and sharper, stronger highlights.
  - **Puddles:** a world-space noise mask adds near-mirror puddles. With Enhanced OFF they reflect the sky colour. With Enhanced ON they get screen-space reflections of neon, lamps and lit windows, like the reference image.
  - **Haze:** rain slightly thickens the horizon haze, but never turns into fog.
  - **Life in the rain:** pedestrians open umbrellas and walk a little faster. AI drivers slow down and keep longer gaps. Headlights switch on even by day. The driver-seat view gets animated wipers.

---

## 6. Controls and cameras

| Key | Action |
|---|---|
| `W A S D`, `Q/E`, Mouse | Free camera (unchanged) |
| **Arrow keys** | Drive the player car, or walk when on foot |
| `Space` / `Left Shift` | Handbrake / run or boost |
| **`F`** | Switch between on foot and driving (spawns or enters the player car) |
| `C` | Lock the camera onto the player (your car, or you on foot) and follow; press again to let go (built in Phase 4) |
| **`V`** | On your car: **chase view ↔ driver view over the bonnet**. Otherwise: driver view of the followed AI car (unchanged). |
| `Tab` / **`Shift+Tab`** | Next AI vehicle / follow an AI pedestrian (pedestrian-eye view) |
| **Mouse (driver seat)** | Head look, limited to ±70°. **`B`** looks back. |
| `M` | Top view of the whole city (Phase 4). The old roundabout toggle was retired in Phase 3. |
| **`O`** | Cycle time presets: Morning → Noon → Afternoon → Evening → Night |
| **`[` / `]`** | Move the time of day back or forward by 1 hour (the sun glides) |
| `T`, `Y/N` | Automatic day cycle on or off / jump to day or night (unchanged) |
| **`K`** | Cycle weather: Clear → Cloudy → Rain |
| **`F3`** or **corner button** | Enhanced mode on or off |
| **`Left Alt` (hold)** | Show the cursor in Free-cam mode so the corner buttons can be clicked |
| `F2` | Shadows on or off |
| `F5` / `F6` | Debug overlay (conflict zones, claims, collision count, **frame-time graph**) / quality preset (Low, Medium, High). Phase 2 built the graph and a resolution cycle (Auto, Native, 720p) on `F6`. |
| **`F7`** | Frame pacing: steady (every second refresh on 120 Hz+ screens) or full rate (added in Phase 2) |
| **`F11`** | Fullscreen on or off |
| `G`, `P`, `1/2/3`, `L`, `R`, `H`, `Esc` | Unchanged |

**Corner panel (top-right, clickable):** `ENHANCED: ON/OFF`, and below it the Morning, Noon, Afternoon, Evening and Night buttons. The active preset is highlighted.

**HUD:** speed in km/h, time preset and clock, weather, mode, a **minimap** of the network with the player, vehicles and junction states, and performance stats (FPS, frame time, draw calls, active lights, and GPU time per pass from GL timer queries).

---

## 7. Performance and smoothness budget

**Targets:** **60 FPS or more at 1920×1080** on the RTX 3050 with the High preset. The hard floor is **50 FPS**. If the machine cannot hold that, the internal resolution drops to 720p.

**Resolution**
- **Window:** it opens at 1920×1080. `F11` switches to borderless fullscreen.
- **Render scale:** 1.0 (native) or 0.67 (720p internal). The scene renders at the scale, and the tonemap pass upscales it to the window, so the HUD stays sharp.
- **Auto-fallback:**
  - If the average FPS stays below 55 for 3 s, the render scale drops to 0.67.
  - If it stays above 75 for 10 s, it goes back to 1.0.
  - The HUD shows the current scale.

**No jitter**
- **Render interpolation:** AI cars, pedestrians and the player car use the blended poses from section 4.7.
- **Camera smoothing:** follow, chase and driver cameras use a critically damped spring, so they never shake or lag in steps.
- **Frame timing:** vsync stays on. Frame `dt` is measured properly, without the 0.05 s clamp hiding stutter. The simulation keeps its own spiral-of-death guard.
- **No hitches:**
  - no per-frame heap allocations;
  - all shaders compiled and all textures uploaded at start;
  - one warm-up frame of every pass before the first visible frame.
- **Frame-time graph (`F5`):** it shows the last 240 frames, so any spike is visible.

**Cost control**
- `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance` exports force the discrete GPU (done in Phase 0).
- Uniform locations are cached, and per-frame data goes in a UBO.
- **Static batching:** a `MeshBuilder` bakes transformed primitives with per-vertex colour into one VBO per block per material. Draw calls drop to about 100.
- **Instancing** for trees, lamps, pedestrians, rain, parked cars and props.
- **Culling:** frustum culling plus distance culling. LOD for distant vehicles and pedestrians.
- **Shadows:** one 2048² cascade on Low and Medium, and two cascades on High.
- **Enhanced mode:** 3 ms or less at 1080p, with SSR and SSAO at half resolution.

---

## 8. Phases (one at a time, each ends buildable and verified)

After **every** phase:
1. Build Release x64 and fix all errors and warnings.
2. Run `--self-test`, plus `--soak` once it exists.
3. Launch the app and check it by eye.
4. Report the results with screenshots or observations.
5. **Wait for your go-ahead before starting the next phase.**

Each new `.cpp`, `.h` and shader file is registered in `OpenGLMiniProject.vcxproj` and `.filters` within the same phase. Checkpoint commits are your call; I will not commit unless you ask.

### Phase 0: Rendering foundations ✅ DONE (see Checkpoint 0)
- Discrete-GPU export, a uniform-location cache, the fixed-timestep loop, and the Overlay buffer fix.
- An HDR framebuffer: RGBA16F with 4× MSAA, resolved to a single sample. Then bloom, ACES tone mapping, exposure keyed to daylight, and sRGB output.
- Colour textures loaded as sRGB. Specular maps and data textures stay linear.
- Hemisphere ambient light, the sky with sun, moon and stars, and height fog. The far plane moved to 1200 m and the ground to 2 km.
- The `--capture` screenshot mode, used to check every later phase.
- **Files:** see Checkpoint 0.
- **Check (passed):** roads read as grey asphalt by day. Night is dark but not crushed. Flat, Gouraud and Phong all still work.

### Phase 1: Traffic core rewrite and collision fix (on the existing single junction) ✅ DONE (see Checkpoint 1)
- The lane graph with connectors, conflict-point precompute, commit-and-claim, IDM following along paths, and all-red plus protected-left signal phases.
- Roundabout merges handled through conflicts. Safe spawning with a gap check.
- The OBB overlap counter, and a new **`--soak <minutes> <seed>`** headless test.
- **Files:** new `Collision.*` and `TrafficBuild.cpp`. `Simulation.*` rewritten. Edits to `Route.*` (translated, reversed, curvature), `main.cpp` (soak flag), and the `--plot` output.
- **Check (passed):** a 30-minute soak over 5 seeds on both modes with 12 cars gives **0 overlaps, no car stopped longer than 60 s, and every approach keeps flowing**. The collision in the middle is gone when viewed in the app.

### Phase 2: Smooth motion and 1080p ✅ DONE (see Checkpoint 2)
- **Motion:** render interpolation for vehicles (previous and current pose, α blend), and the critically damped camera spring.
- **Resolution:** the 1920×1080 window, `F11` borderless fullscreen, and the render scale (1.0 or 0.67) with auto-fallback.
- **Timing:** the frame-time graph in the `F5` overlay, shader and texture prewarm, and the `dt` clamp removed from the render path.
- **Files:** edits to `main.cpp`, `Simulation.*` (pose history), `Camera.*`, `Framebuffer.*` (scaled target), `PostProcess.*` (upscale in tonemap) and `Overlay.*` (graph).
- **Check (passed):**
  - **No stepping:** `--motion-test` shows judder of 0.0015 to 0.0031 at 144, 60 and 75 Hz, against 0.5 to 2.0 without interpolation.
  - **Frame rate:** 1080p runs at a steady 72 FPS, or 128 FPS at full rate, with 2.5 ms of GPU time.
  - **Spikes:** the only frame over 25 ms not caused by the launch or the fullscreen switch is a single driver stall about 6 s after launch, inside `SwapBuffers`. Our own work in that frame is 0.5 ms; see Checkpoint 2.
  - **720p:** forcing the render scale to 0.67 still looks clean.

### Phase 3: Road network (closed maze), wider roads, roundabouts, the ring ✅ DONE (see Checkpoint 3)
- **Network:** `RoadNetwork` with the closed layout of section 3.2.
  - The generic junction generator builds signalised 4-way intersections, signalised T-junctions, give-way T-junctions, bends and 4-arm roundabouts.
  - Each junction keeps its own permanent type.
  - Also: 4-lane roads, lane changes after a junction, no roads out of town, and density-aware random routing.
- **Road meshes:** generated from the network: roads, kerbs with corner fillets, sidewalks, markings (lane dashes, double yellow, stop lines, turn arrows, zebras, yield teeth), splitter islands and islands. Markings are batched.
- **Lighting:** street lamps along every road through the **simple light budget** (section 3.4). The central four lab lamps and the floodlight spot light are kept.
- **Other:** the `M` key is retired, because no junction changes type. The fountain moves to roundabout R1, and the camera presets are updated.
- **Files:** new `RoadNetwork.*`, `LightManager.*` (nearest-N budget, no clusters), `MeshBuilder.*`, `RoadRenderer.*`. `Scene.cpp` is split into smaller renderers.
- **Check:**
  - `--self-test` confirms that:
    - every lane is tangent-continuous;
    - the network is connected;
    - lanes clear islands and kerbs;
    - the conflict table is symmetric;
    - no conflicting movements are ever green together.
  - `--plot` writes `network.png` of all lanes and conflict points with stb_image_write.
  - Soak: 25 vehicles, 0 overlaps, and traffic spread across junctions (no junction holds more than 35% of cars).
  - Night: roads are lit, with no light popping as the camera moves.

### Phase 4: Player car, driver view, on foot ✅ DONE (see Checkpoint 4)
- **As built (revised 2026-09-24):**
  - two views of the player car, the chase view (above and behind) and the driver view over the bonnet, with no cockpit interior;
  - `C` locks the camera onto the car and lets go;
  - the floodlight mast is replaced by neon signs and lit billboards;
  - the items below are the original plan, kept for reference.
- **Driving:** a player car with a kinematic bicycle model on the arrow keys, plus handbrake and boost. It is fixed-step and interpolated like the AI. It collides with the world and AI, and the AI yields to the player.
- **Cameras:** Chase, **Driver seat** and Hood, switched with `V`.
- **Driver seat:**
  - the camera at the driver's eye position;
  - the interior model (dashboard, turning steering wheel, speedometer needle, A-pillars, mirror frame);
  - mouse head look limited to ±70°, and `B` to look back;
  - a slight smoothed lean under braking and cornering.
- **On foot:** first person with wall collision, and `F` to switch between driving and walking. Also the pedestrian-eye view, the minimap and a speedometer.
- **Files:** new `Player.*`, `CockpitRenderer.*`. Edits to `Camera.*`, `main.cpp`, `Overlay.*`.
- **Check:**
  - Drive the whole loop in the driver-seat view, with no camera shake or jitter.
  - Crash into buildings and cars: you get pushed back, with no tunnelling.
  - Walk into walls: you slide along them.

### Phase 5: Vehicle variety
- The lofted-body generator and the 11 vehicle types. Lights, indicators and brake lights. The bus loop line with bus-stop dwell time. Emergency flashers.
- **Files:** new `VehicleTypes.*`, `VehicleRenderer.*`. Edits to `Mesh.*` and the simulation spawn mix.
- **Check:** a mix of types is visible. Buses swing properly on turns and stop at stops. Soak with long vehicles still gives 0 overlaps.

### Phase 6: City dressing (minimal: buildings, shops, props, trees)
- **Buildings:** the world generator places the 30 to 40 buildings in 4 to 5 styles, shops with neon text on the main streets, and the lit-window facade. Also the park, plaza and gas station.
- **Street:** a modest amount of street furniture and parked cars. The instanced trees with leaf cards and wind. The single outer building row and the distant skyline.
- **Files:** new `World.*`, `TreeGenerator.*`, `NeonText.*`, `PropRenderer.*`. RGBA support in `Texture.*`. Instancing attributes in `Mesh.*` and the shaders.
- **Check:**
  - Street-level and top views look like a small, lived-in city with no empty-looking stretches, but not crowded.
  - Neon glows through bloom at night.
  - The frame rate stays at 60 FPS or more at 1080p, and the HUD shows draw calls.

### Phase 7: Pedestrians
- The sidewalk graph, crossings (signalised and zebra), the walker model and animation (section 5.1: mannequin, BVH clips, speed- and phase-synchronised blending, a small animation graph, leg IK on kerbs), instanced rendering, vehicles yielding, and umbrellas.
- **Files:** new `Pedestrians.*`, `PedestrianRenderer.*`.
- **Check:** soak shows 0 vehicle-pedestrian overlaps on crossings and no pedestrian stuck for more than 90 s. You can see people waiting for WALK and then crossing.

### Phase 8: Sun, moon, time presets and shadows
- **Sky motion:** the tilted sun path, the moon's opposite arc, and the five presets with the 3 s glide. `O`, `[`, `]` and the corner time buttons (the Enhanced button comes in Phase 10).
- **Shadows:** a stable shadow map (1 cascade, 2 on High) with PCF and alpha-tested leaf shadows. It follows the sun, or the moon at night.
- **Night lighting:** faint blue moonlight shadows, and headlights as spot lights in the light budget. Also neon spill lights and night lighting polish.
- **Files:** new `ShadowMap.*`, `shadow.vert/frag`. Edits to `DayNight.*`, `Sky.*`, `sky.frag`, `scene.frag`, `Overlay.*`.
- **Check:**
  - Captures at all five presets from the same view: shadows point west in the morning, are short at noon, and are long and pointing east in the evening.
  - Pressing `O` makes shadows sweep smoothly, with no snapping.
  - No acne, no peter-panning, and no swimming while the camera moves.
  - Streets are well lit at night.

### Phase 9: Weather: clouds and light rain
Built in this order, each step checked with captures before the next:
1. **Weather state machine and `K` key:** Clear, Cloudy and Rain, with smooth blended parameters and a HUD readout.
2. **Clouds:** the cloud layer in `sky.frag` (fbm noise, wind drift, coverage, sun-side lighting, time-of-day colour). Overcast dims the sun and lifts the ambient, and cloud shadows drift over the ground.
3. **Rain particles:** instanced streaks around the camera with a slight wind slant, plus splashes on the ground and on car roofs.
4. **Wet world:** a wetness value that builds and dries, darker and glossier surfaces, and puddles with sky reflection. Puddles get screen-space reflections in Phase 10.
5. **Behaviour:** umbrellas, slower and more careful AI drivers, headlights on in rain, and wipers in the driver-seat view.
- **Files:** new `Weather.*`, `Rain.*`, `shaders/rain.vert/frag`. Edits to `sky.frag`, `atmosphere.glsl`, `scene.frag`, `DayNight.*` (overcast factors) and `Overlay.*`.
- **Check:**
  - Cycle every state with `K` at Morning, Evening and Night.
  - Clouds drift across the top of the sky and change with the weather.
  - Rain is visible under street lamps at night.
  - Roads darken and puddles appear as rain continues, then dry afterwards.
  - Transitions are smooth, and FPS stays at 60 or more.
- **Can move earlier:** clouds depend only on the Phase 0 sky, so steps 1 and 2 can run straight after Phase 1 if you want them sooner.

### Phase 10: Enhanced mode and the corner button
- **Button and input:** the corner panel button with mouse hit testing, the cursor rule (free outside Free-cam, `Left Alt` in Free-cam), and `F3`.
- **Scene pass:** a second render target for normals and roughness, with a nearest-filter resolve.
- **Effects:**
  - **SSR:** half resolution, Fresnel- and wetness-weighted, fading at screen edges.
  - **SSAO:** half resolution, with a bilateral upsample.
  - **PCSS** soft shadows, **screen-space contact shadows**, **sun shafts** for a low sun, and a slight bloom boost.
- **Timing:** GPU timings for each effect in the HUD.
- **Files:** new `Enhanced.*` (or inside `PostProcess.*`), `shaders/ssr.frag`, `shaders/ssao.frag`, `shaders/sunshafts.frag`. Edits to `Framebuffer.*`, `scene.frag`, `Overlay.*`, `main.cpp`.
- **Check:**
  - An A/B comparison with the button at Evening and at Night in the rain: neon and lit windows reflect in the wet road, and cars sit on the ground with soft contact shading.
  - Sun shafts appear at Morning and Evening.
  - 60 FPS or more at 1080p with Enhanced ON, or at least 50 FPS with the 720p fallback.
  - Enhanced costs 3 ms or less.

### Phase 11: Delivery
- The README update: the new lab-topic mapping, controls, and how Enhanced mode fakes a ray-traced look. The Overlay help panel. Final soak runs across seeds. A performance pass. A viva demo script.

---

## 9. Verification (summary)
- **Build:**
  ```
  "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" OpenGLMiniProject.vcxproj -p:Configuration=Release -p:Platform=x64
  ```
- **Self-test:** `x64\Release\OpenGLMiniProject.exe --self-test`, run from the project folder. It covers geometry, network connectivity, conflict tables and the signal-plan safety matrix.
- **Soak:** `--soak 30 <seed>` over several seeds. It reports AI-AI overlaps (must be 0), vehicle-pedestrian overlaps (must be 0), the longest stop, throughput per junction and the density spread.
- **Plot:** `--plot` writes an ASCII map plus `network.png`.
- **Capture:** it renders a fixed view and saves it, which gives before and after images for every phase.
  ```
  --capture out.png --view 0..3 --time H --shading 0..2 [--roundabout] [--no-hud]
  ```
  - **Time presets:** use `--time 7`, `12`, `15.5`, `18.5` or `22`.
  - **New flags:** `--weather clear|cloudy|rain` (from Phase 9) and `--enhanced` (from Phase 10).
- **Smoothness:** the `F5` frame-time graph and the FPS readout are checked at 1080p every phase. The FPS must stay at 60 or more, with no spike above 25 ms.
- **Enhanced A/B:** the same capture with and without `--enhanced`.
- **Visual:** a phase-specific checklist is run in the app every phase, with the HUD's performance stats and GPU timer queries.

## 10. Risks and mitigations
| Risk | Mitigation |
|---|---|
| Scope is large | Strict phase order. Each phase ships on its own. The city is a 3×3 core inside a ring road, and `RoadNetwork::makeCity` can reshape it. |
| Jitter from the fixed simulation step | Render interpolation (section 4.7) and critically damped cameras, checked in Phase 2 before anything else is added. |
| Frame rate drops below 60 at 1080p | Batching, instancing, culling, quality presets, and the auto render-scale fallback to 720p. Timer queries show where the time goes. |
| Enhanced mode too slow | SSR and SSAO at half resolution, fewer ray-march steps on Medium, and Enhanced OFF on Low. |
| Screen-space reflections miss off-screen objects | Fade reflections at screen edges and fall back to the sky colour. This is expected for a "partial ray tracing" look. |
| Gridlock in a closed network | The exit-room check, a vehicle cap well below road capacity, and a deadlock detector in the soak test. |
| Shadow-map artefacts | Texel snapping, normal-offset bias, and PCSS in Enhanced mode. |
| Lab grading features lost | Flat/Gouraud/Phong, the Lab 3 lamp constants and spot light, the Lab 4 crates and specular map, and the Lab 5 Bezier meshes are all kept, and the README mapping is updated. |
| iGPU selected by default | Optimus and PowerXpress exports in `main.cpp`. |
