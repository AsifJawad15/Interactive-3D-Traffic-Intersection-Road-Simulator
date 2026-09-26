#!/bin/sh
# Re-makes every screenshot used by the report and the slides, with the
# program's own --capture mode (1920x1080). Run from the project folder:
#   sh delivery/figures/capture_all.sh
# The views (--view N) are listed in the README under --capture.
set -e
exe=./x64/Release/OpenGLMiniProject.exe
out=delivery/figures/shots
mkdir -p "$out"

shot() {
    name=$1
    shift
    "$exe" --capture "$out/$name.png" --size 1920x1080 --frames 60 "$@" > /dev/null
    echo "$name"
}

# The city and 3D transformations
shot city_overview      --view 3  --time 15.5 --no-hud --warm 20
shot top_view           --view 5  --time 12   --no-hud --warm 20
shot lineup_front       --view 11 --time 12   --no-hud
shot lineup_back        --view 13 --time 15.5 --no-hud
shot crossroads_day     --view 0  --time 15.5 --no-hud --warm 30
shot bus_stop           --view 12 --time 15.5 --no-hud
shot street_level       --view 1  --time 12   --no-hud --warm 20
shot ring_road          --view 7  --time 15.5 --no-hud --warm 20

# Lighting: directional (sun, moon), point (lamps), spot (billboard lamp,
# headlights) and emissive (neon, windows)
shot crossroads_night   --view 0  --time 22   --no-hud --warm 30
shot street_level_night --view 1  --time 22   --no-hud --warm 20
shot shopping_night     --view 18 --time 22   --no-hud --warm 20
shot chase_night        --view 8  --time 22   --no-hud --warm 20
shot sky_night          --view 26 --time 22   --no-hud

# Shading on the Bezier fountain
shot fountain_flat      --view 2  --time 15.5 --no-hud --shading 0
shot fountain_gouraud   --view 2  --time 15.5 --no-hud --shading 1
shot fountain_phong     --view 2  --time 15.5 --no-hud --shading 2

# Textures and curved surfaces
shot park_plaza         --view 16 --time 15.5 --no-hud
shot petrol_station     --view 17 --time 12   --no-hud
shot houses             --view 19 --time 15.5 --no-hud
shot on_foot            --view 10 --time 15.5 --no-hud

# Day and night from one view
shot time_morning       --view 4  --time 7    --no-hud --warm 20
shot time_noon          --view 4  --time 12   --no-hud --warm 20
shot time_evening       --view 4  --time 18.5 --no-hud --warm 20
shot time_night         --view 4  --time 22   --no-hud --warm 20

# Weather
shot weather_clear      --view 18 --time 15.5 --no-hud --weather clear
shot weather_cloudy     --view 18 --time 15.5 --no-hud --weather cloudy
shot weather_rain       --view 18 --time 15.5 --no-hud --weather rain
shot rain_night_driver  --view 9  --time 22   --no-hud --weather rain --warm 20

# People and signals
shot people_waiting     --view 25 --time 15.5 --no-hud --warm 30
shot walk_signal        --view 20 --time 15.5 --no-hud --warm 30
shot zebra              --view 21 --time 15.5 --no-hud --warm 30
shot roundabout_r2      --view 6  --time 15.5 --no-hud --warm 30

# Ray tracing off and on
shot rt_off_rain_night  --view 18 --time 22   --no-hud --weather rain
shot rt_on_rain_night   --view 18 --time 22   --no-hud --weather rain --enhanced
shot rt_off_noon        --view 4  --time 12   --no-hud --warm 20
shot rt_on_noon         --view 4  --time 12   --no-hud --warm 20 --enhanced
shot rt_off_evening     --view 26 --time 18.5 --no-hud
shot rt_on_evening      --view 26 --time 18.5 --no-hud --enhanced
shot rt_question        --view 18 --time 22   --weather rain --confirm

# With the HUD: speedometer, minimap, time and weather buttons
shot hud_chase          --view 8  --time 15.5 --warm 20
shot hud_driver         --view 9  --time 15.5 --warm 20
