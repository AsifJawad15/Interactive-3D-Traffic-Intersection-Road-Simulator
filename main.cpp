#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Camera.h"
#include "DayNight.h"
#include "FrameStats.h"
#include "Framebuffer.h"
#include "LightManager.h"
#include "Overlay.h"
#include "PostProcess.h"
#include "Scene.h"
#include "Sky.h"
#include "Screenshot.h"
#include "Simulation.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

// On a laptop with both an integrated and a discrete GPU, these two exported
// symbols ask the NVIDIA and AMD drivers to run this program on the discrete
// GPU. Without them Windows usually picks the integrated one.
extern "C"
{
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace
{
    // Options for --capture, which renders a fixed view at a fixed time and
    // saves it as a PNG. It makes every change checkable with an image.
    struct CaptureOptions
    {
        bool enabled = false;
        std::string path = "capture.png";
        int frames = 90;
        int view = 0;
        float hour = -1.0f;
        bool hideHud = false;
        int shading = 2;
        int width = 1280;             // --size WxH
        int height = 720;
        float scale = 1.0f;           // --scale 0.67 renders at 720p inside 1080p
        bool graph = false;           // --graph shows the frame-time graph
        bool fullRate = false;        // --full-rate draws on every refresh
    };

    // Camera poses for --view. Yaw 90 looks along +z (north), yaw 0 along +x.
    void applyCaptureView(Camera& camera, int view)
    {
        switch (view)
        {
        case 1: camera.setFreePose({3.5f, 1.7f, -44.0f}, 90.0f, -3.0f); break;     // street level, X0 south arm
        case 2: camera.setFreePose({60.0f, 9.0f, 28.0f}, -25.0f, -14.0f); break;   // roundabout R1 with the fountain
        case 3: camera.setFreePose({0.0f, 300.0f, -480.0f}, 90.0f, -33.0f); break; // the whole city
        case 4: camera.setFreePose({-60.0f, 30.0f, -55.0f}, 60.0f, -24.0f); break; // T-junctions G and ST
        case 5: camera.setFreePose({0.0f, 430.0f, 0.01f}, 90.0f, -89.0f); break;   // straight down
        case 6: camera.setFreePose({-38.0f, 13.0f, -128.0f}, 36.0f, -16.0f); break; // roundabout R2
        case 7: camera.setFreePose({-170.0f, 22.0f, 170.0f}, -20.0f, -14.0f); break; // the ring road, NW corner
        default: camera.reset(); break;
        }
    }

    struct ApplicationState
    {
        Camera* camera = nullptr;
        TrafficSystem* traffic = nullptr;
        DayNight* dayNight = nullptr;
        int framebufferWidth = 1280;
        int framebufferHeight = 720;
        bool paused = false;
        bool showHelp = true;
        int shadingMode = 2;
        bool firstMouseEvent = true;
        double lastMouseX = 0.0;
        double lastMouseY = 0.0;
        bool showFrameGraph = false;
        bool fullscreenRequested = false;
        RenderScaler* scaler = nullptr;

        // Frame pacing: false = on a fast display (120 Hz or more) draw on every
        // second refresh for perfectly even frame times; true = every refresh.
        bool fullRatePacing = false;
        bool pacingChanged = true;
        int pacingHz = 60;
    };

    // Where the window was before going fullscreen, to return it there.
    struct WindowPlacement
    {
        int x = 100;
        int y = 100;
        int width = 1920;
        int height = 1080;
    };

    void toggleFullscreen(GLFWwindow* window, WindowPlacement& placement)
    {
        if (glfwGetWindowMonitor(window) == nullptr)
        {
            glfwGetWindowPos(window, &placement.x, &placement.y);
            glfwGetWindowSize(window, &placement.width, &placement.height);
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            // The monitor's own mode, so the switch is instant and the refresh
            // rate stays what the desktop uses.
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
        else
        {
            glfwSetWindowMonitor(window, nullptr, placement.x, placement.y,
                                 placement.width, placement.height, 0);
        }
    }

    // Sets the swap interval and returns the resulting frame rate. On a hybrid-
    // GPU laptop at 144 Hz, frames every 6.9 ms often miss a refresh and land
    // 13.9 ms apart instead, which reads as stutter. Drawing on every second
    // refresh gives an even 72 FPS: every frame exactly 13.9 ms.
    int applyPacing(GLFWwindow* window, bool fullRate)
    {
        GLFWmonitor* monitor = glfwGetWindowMonitor(window);
        const GLFWvidmode* mode = glfwGetVideoMode(monitor != nullptr ? monitor : glfwGetPrimaryMonitor());
        const int refresh = mode != nullptr && mode->refreshRate > 0 ? mode->refreshRate : 60;
        const int interval = (!fullRate && refresh >= 120) ? 2 : 1;
        glfwSwapInterval(interval);
        return refresh / interval;
    }

    void glfwErrorCallback(int code, const char* description)
    {
        std::cerr << "GLFW error " << code << ": " << description << '\n';
    }

    ApplicationState* stateFrom(GLFWwindow* window)
    {
        return static_cast<ApplicationState*>(glfwGetWindowUserPointer(window));
    }

    void framebufferCallback(GLFWwindow* window, int width, int height)
    {
        ApplicationState* state = stateFrom(window);
        if (state == nullptr)
            return;

        state->framebufferWidth = std::max(width, 1);
        state->framebufferHeight = std::max(height, 1);
        glViewport(0, 0, state->framebufferWidth, state->framebufferHeight);
    }

    void cursorCallback(GLFWwindow* window, double xPosition, double yPosition)
    {
        ApplicationState* state = stateFrom(window);
        if (state == nullptr || state->camera == nullptr)
            return;

        if (state->firstMouseEvent)
        {
            state->lastMouseX = xPosition;
            state->lastMouseY = yPosition;
            state->firstMouseEvent = false;
            return;
        }

        const float xOffset = static_cast<float>(xPosition - state->lastMouseX);
        const float yOffset = static_cast<float>(state->lastMouseY - yPosition);
        state->lastMouseX = xPosition;
        state->lastMouseY = yPosition;
        state->camera->processMouse(xOffset, yOffset);
    }

    void keyCallback(GLFWwindow* window, int key, int, int action, int)
    {
        if (action != GLFW_PRESS)
            return;

        ApplicationState* state = stateFrom(window);
        if (key == GLFW_KEY_ESCAPE)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        else if (state == nullptr)
            return;
        else if (key == GLFW_KEY_C && state->camera != nullptr)
            state->camera->cycleMode();
        else if (key == GLFW_KEY_V && state->camera != nullptr && state->traffic != nullptr)
            state->camera->toggleDriverView(state->traffic->vehicles().size());
        else if (key == GLFW_KEY_TAB && state->camera != nullptr && state->traffic != nullptr)
            state->camera->nextFollow(state->traffic->vehicles().size());
        else if (key == GLFW_KEY_G && state->traffic != nullptr)
            state->traffic->advancePhase();
        else if (key == GLFW_KEY_P)
            state->paused = !state->paused;
        else if (key == GLFW_KEY_H)
            state->showHelp = !state->showHelp;
        else if (key == GLFW_KEY_F5)
            state->showFrameGraph = !state->showFrameGraph;
        else if (key == GLFW_KEY_F6 && state->scaler != nullptr)
            state->scaler->cycleMode();
        else if (key == GLFW_KEY_F11)
            state->fullscreenRequested = true;
        else if (key == GLFW_KEY_F7)
        {
            state->fullRatePacing = !state->fullRatePacing;
            state->pacingChanged = true;
        }
        else if (key == GLFW_KEY_1)
            state->shadingMode = 0;
        else if (key == GLFW_KEY_2)
            state->shadingMode = 1;
        else if (key == GLFW_KEY_3)
            state->shadingMode = 2;
        else if (key == GLFW_KEY_T && state->dayNight != nullptr)
            state->dayNight->toggleAutomatic();
        else if (key == GLFW_KEY_Y && state->dayNight != nullptr)
            state->dayNight->setDay();
        else if (key == GLFW_KEY_N && state->dayNight != nullptr)
            state->dayNight->setNight();
        else if (key == GLFW_KEY_L && state->dayNight != nullptr)
            state->dayNight->toggleStreetLamps();
        else if (key == GLFW_KEY_R && state->camera != nullptr && state->traffic != nullptr && state->dayNight != nullptr)
        {
            state->camera->reset();
            state->traffic->reset();
            state->dayNight->reset();
            state->paused = false;
            state->shadingMode = 2;
            state->firstMouseEvent = true;
        }
    }

    bool hasCore33Context()
    {
        GLint major = 0;
        GLint minor = 0;
        GLint profile = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);

        std::cout << "OpenGL " << major << '.' << minor << " | "
                  << reinterpret_cast<const char*>(glGetString(GL_RENDERER)) << '\n';
        std::cout << "Context profile: "
                  << ((profile & GL_CONTEXT_CORE_PROFILE_BIT) != 0 ? "Core" : "Not core") << '\n';

        return (major > 3 || (major == 3 && minor >= 3)) &&
               (profile & GL_CONTEXT_CORE_PROFILE_BIT) != 0;
    }

    const char* shadingModeName(int shadingMode)
    {
        if (shadingMode == 0)
            return "FLAT";
        if (shadingMode == 1)
            return "GOURAUD";
        return "PHONG";
    }

    // Headless endurance test: runs the city's traffic for simulated minutes
    // at the real 60 Hz step and checks that no two vehicle bodies ever
    // overlapped, that nobody was stuck, and that the traffic spread over the
    // junctions instead of piling up at one.
    //     OpenGLMiniProject.exe --soak 30 7 [--cars 25] [--trace [T]]
    int runSoak(float minutes, unsigned int seed, std::size_t cars, float traceFrom, float longestAllowedStop)
    {
        constexpr float step = 1.0f / 60.0f;
        constexpr double largestAllowedShare = 0.35;
        const long long steps = static_cast<long long>(minutes * 60.0f / step);

        const auto started = std::chrono::steady_clock::now();
        TrafficSystem traffic(cars, seed);
        // --trace [T] prints every vehicle every 2 s, twenty times, starting at
        // simulated second T, or by default from the moment some vehicle has
        // been standing still for too long.
        int traceDumps = traceFrom >= -1.0f ? 20 : 0;
        long long nextTraceStep = traceFrom >= 0.0f ? static_cast<long long>(traceFrom / step) : 0;
        for (long long index = 0; index < steps; ++index)
        {
            traffic.update(step);
            const bool triggered = traceFrom >= 0.0f || traffic.stats().longestStop > longestAllowedStop;
            if (traceDumps > 0 && index >= nextTraceStep && triggered)
            {
                std::printf("t = %.1f s  %s", index * step, traffic.describe().c_str());
                nextTraceStep = index + 120;
                --traceDumps;
            }
        }
        const double wallSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();

        // Share of all vehicle-time spent on each junction's routes.
        const TrafficStats& stats = traffic.stats();
        double total = 0.0;
        for (double seconds : stats.junctionSeconds)
            total += seconds;
        std::size_t busiest = 0;
        for (std::size_t junction = 1; junction < stats.junctionSeconds.size(); ++junction)
        {
            if (stats.junctionSeconds[junction] > stats.junctionSeconds[busiest])
                busiest = junction;
        }
        const double share = total > 0.0 ? stats.junctionSeconds[busiest] / total : 0.0;

        const bool passed = stats.overlapSteps == 0 &&
                            stats.longestStop <= longestAllowedStop &&
                            share <= largestAllowedShare &&
                            stats.trips > 0;

        std::printf(
            "CITY seed %-5u cars %2zu  %5.1f min | overlaps %zu | closest gap %.2f m | longest stop %5.1f s | "
            "routes driven %5zu | busiest %s %.0f%% | %.1f s wall | %s\n",
            seed, cars, stats.simulatedSeconds / 60.0, stats.overlapSteps, stats.closestBodyGap,
            stats.longestStop, stats.trips,
            traffic.network().junctions()[busiest].name.c_str(), share * 100.0,
            wallSeconds, passed ? "PASS" : "FAIL");
        if (!passed)
            std::printf("%s", traffic.describe().c_str());
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    // Headless smoothness test. It replays the real frame loop (fixed 60 Hz
    // simulation, rendering at the display's rate with slightly uneven frame
    // times, as a real driver delivers them) and measures judder: how much a
    // car's on-screen speed (movement divided by frame time) changes from one
    // frame to the next, relative to its average speed. Smooth motion scores
    // near 0; a car that moves on some frames and stands still on others
    // scores 1 or more.
    //     OpenGLMiniProject.exe --motion-test
    int runMotionTest()
    {
        constexpr float simulationStep = 1.0f / 60.0f;
        constexpr float testSeconds = 90.0f;
        constexpr float allowedJudder = 0.01f;
        bool allPassed = true;

        for (const float refreshRate : {144.0f, 60.0f, 75.0f})
        {
            TrafficSystem traffic(24, 3);
            std::vector<VehiclePose> smooth;
            std::vector<VehiclePose> stepped;
            std::vector<VehiclePose> previousSmooth;
            std::vector<VehiclePose> previousStepped;
            std::vector<float> lastMoveSmooth(24, -1.0f);
            std::vector<float> lastMoveStepped(24, -1.0f);

            double judderSmooth = 0.0;
            double judderStepped = 0.0;
            double travelled = 0.0;
            float backlog = 0.0f;
            const int frames = static_cast<int>(testSeconds * refreshRate);

            for (int frame = 0; frame < frames; ++frame)
            {
                // Frame times wobble by about 3 %, as they do in practice.
                const float frameSeconds = (1.0f / refreshRate) *
                    (1.0f + 0.03f * std::sin(static_cast<float>(frame) * 1.7f));
                backlog += frameSeconds;
                while (backlog >= simulationStep)
                {
                    traffic.update(simulationStep);
                    backlog -= simulationStep;
                }
                traffic.interpolatePoses(backlog / simulationStep, smooth);
                traffic.interpolatePoses(1.0f, stepped);   // what the old renderer drew

                if (!previousSmooth.empty())
                {
                    for (std::size_t index = 0; index < smooth.size(); ++index)
                    {
                        if (!smooth[index].active || !previousSmooth[index].active)
                        {
                            lastMoveSmooth[index] = lastMoveStepped[index] = -1.0f;
                            continue;
                        }
                        const float distanceSmooth = glm::length(smooth[index].position - previousSmooth[index].position);
                        const float distanceStepped = glm::length(stepped[index].position - previousStepped[index].position);
                        const float moveSmooth = distanceSmooth / frameSeconds;     // on-screen speed
                        const float moveStepped = distanceStepped / frameSeconds;
                        if (distanceSmooth > 2.0f)   // a jump, not motion (never expected)
                        {
                            lastMoveSmooth[index] = lastMoveStepped[index] = -1.0f;
                            continue;
                        }
                        if (lastMoveSmooth[index] >= 0.0f)
                        {
                            judderSmooth += std::abs(moveSmooth - lastMoveSmooth[index]);
                            judderStepped += std::abs(moveStepped - lastMoveStepped[index]);
                            travelled += moveSmooth;
                        }
                        lastMoveSmooth[index] = moveSmooth;
                        lastMoveStepped[index] = moveStepped;
                    }
                }
                previousSmooth = smooth;
                previousStepped = stepped;
            }

            const float scoreSmooth = travelled > 0.0 ? static_cast<float>(judderSmooth / travelled) : 0.0f;
            const float scoreStepped = travelled > 0.0 ? static_cast<float>(judderStepped / travelled) : 0.0f;
            const bool passed = scoreSmooth <= allowedJudder;
            allPassed = allPassed && passed;
            std::printf("%3.0f Hz display | judder without interpolation %.3f | with interpolation %.4f | %s\n",
                        refreshRate, scoreStepped, scoreSmooth, passed ? "PASS" : "FAIL");
        }
        return allPassed ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    // Headless check that street lamps never pop in or out. At night a camera
    // drives every road of the city at street level and at follow-camera
    // height, and turns on the spot in the middle of every junction. Each
    // frame the light budget chooses its lights, exactly as the renderer
    // does, and the test records how much any light's strength changes from
    // one frame to the next - counting only lights whose reach is on screen
    // in both frames, since nobody sees the others change. A light that fades
    // in over a quarter of a second at 60 FPS changes by 0.07 per frame.
    //     OpenGLMiniProject.exe --light-test
    int runLightTest()
    {
        constexpr float dt = 1.0f / 60.0f;
        constexpr float allowedStep = 0.1f;

        const RoadNetwork network = RoadNetwork::makeCity();
        std::vector<PointLight> lights;
        for (const StreetLamp& lamp : network.streetLamps())
            lights.push_back(PointLight::fromStreetLamp(lamp));

        LightBudget budget;
        std::vector<LightBudget::Choice> chosen;
        std::vector<float> previous(lights.size(), 0.0f);
        std::vector<float> current(lights.size(), 0.0f);
        std::vector<bool> previousInView(lights.size(), false);
        const glm::mat4 projection = glm::perspective(glm::radians(55.0f), 16.0f / 9.0f, 0.12f, 1200.0f);

        std::size_t frames = 0;
        std::size_t pops = 0;
        float worst = 0.0f;
        glm::vec3 worstAt {0.0f};
        std::size_t budgetFull = 0;
        bool first = true;

        const auto frame = [&](const glm::vec3& eye, float yawDegrees, float pitchDegrees)
        {
            const glm::vec3 front {std::cos(glm::radians(yawDegrees)) * std::cos(glm::radians(pitchDegrees)),
                                   std::sin(glm::radians(pitchDegrees)),
                                   std::sin(glm::radians(yawDegrees)) * std::cos(glm::radians(pitchDegrees))};
            const glm::mat4 viewProjection = projection * glm::lookAt(eye, eye + front, glm::vec3{0.0f, 1.0f, 0.0f});
            budget.choose(lights, eye, viewProjection, true, chosen);

            std::fill(current.begin(), current.end(), 0.0f);
            for (const LightBudget::Choice& choice : chosen)
                current[choice.light] = choice.fade;
            budgetFull += chosen.size() >= static_cast<std::size_t>(LightBudget::maximumLights) ? 1 : 0;

            for (std::size_t index = 0; index < lights.size(); ++index)
            {
                const bool inView = LightBudget::reachInView(lights[index], viewProjection);
                if (!first && inView && previousInView[index])
                {
                    const float step = std::abs(current[index] - previous[index]);
                    pops += step > allowedStep ? 1 : 0;
                    if (step > worst)
                    {
                        worst = step;
                        worstAt = eye;
                    }
                }
                previousInView[index] = inView;
            }
            std::swap(previous, current);
            first = false;
            ++frames;
        };

        // Drive every road both ways, looking along it: 12 m/s, at street
        // level, follow-camera height and as a low fly-over.
        for (const float height : {1.7f, 5.0f, 30.0f})
        {
            for (const RoadNetwork::Road& road : network.roads())
            {
                for (int direction = 0; direction < 2; ++direction)
                {
                    const glm::vec2 from = direction == 0 ? road.start : road.end;
                    const glm::vec2 to = direction == 0 ? road.end : road.start;
                    const glm::vec2 along = glm::normalize(to - from);
                    const glm::vec2 lane = from + glm::vec2{-along.y, along.x} * RoadNetwork::laneOffsets[1];
                    const float yaw = glm::degrees(std::atan2(along.y, along.x));
                    const float length = glm::length(to - from);
                    first = true;   // a new path: the camera jumps here
                    for (float travelled = 0.0f; travelled <= length; travelled += 12.0f * dt)
                    {
                        const glm::vec2 p = lane + along * travelled;
                        frame({p.x, height, p.y}, yaw, height > 20.0f ? -30.0f : (height > 2.0f ? -12.0f : -2.0f));
                    }
                }
            }
        }

        // Turn on the spot in every junction, a full circle in six seconds.
        for (const Junction& junction : network.junctions())
        {
            first = true;
            for (float yaw = 0.0f; yaw < 360.0f; yaw += 1.0f)
                frame({junction.centre.x, 1.7f, junction.centre.y}, yaw, -2.0f);
        }

        const bool passed = pops == 0;
        std::printf("%zu lamps, %zu frames (budget full in %.0f%%) | largest change of one light in one frame %.3f "
                    "at (%.0f, %.1f, %.0f) | frames over %.2f: %zu | %s\n",
                    lights.size(), frames, 100.0 * static_cast<double>(budgetFull) / static_cast<double>(frames),
                    worst, worstAt.x, worstAt.y, worstAt.z, allowedStep, pops, passed ? "PASS" : "FAIL");
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    }
}

int main(int argc, char** argv)
{
    // Geometry checks that need no window, so the route mathematics can be
    // verified from the command line: OpenGLMiniProject.exe --self-test
    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--self-test") != 0)
            continue;

        TrafficSystem traffic;
        std::string report;
        const bool passed = traffic.selfTest(report);
        std::cout << report;
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    std::size_t vehicleCount = 36;   // the closed 400 m city holds up to 40 (tested by --soak)
    for (int index = 1; index + 1 < argc; ++index)
    {
        if (std::strcmp(argv[index], "--cars") == 0)
            vehicleCount = static_cast<std::size_t>(std::clamp(std::atoi(argv[index + 1]), 1, 40));
    }

    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--soak") != 0)
            continue;

        const float minutes = index + 1 < argc ? static_cast<float>(std::atof(argv[index + 1])) : 30.0f;
        const unsigned int seed = index + 2 < argc ? static_cast<unsigned int>(std::strtoul(argv[index + 2], nullptr, 10)) : 1u;
        // traceFrom: below -1 = no trace, -1 = on a long stop, else a start time.
        float traceFrom = -2.0f;
        float stopLimit = 60.0f;   // --stop-limit S: longest allowed wait
        for (int other = 1; other < argc; ++other)
        {
            if (std::strcmp(argv[other], "--stop-limit") == 0 && other + 1 < argc)
                stopLimit = static_cast<float>(std::atof(argv[other + 1]));
            if (std::strcmp(argv[other], "--trace") == 0)
            {
                traceFrom = -1.0f;
                if (other + 1 < argc && argv[other + 1][0] != '-')
                    traceFrom = static_cast<float>(std::atof(argv[other + 1]));
            }
        }
        return runSoak(std::max(minutes, 0.1f), seed, vehicleCount, traceFrom, stopLimit);
    }

    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--motion-test") == 0)
            return runMotionTest();
        if (std::strcmp(argv[index], "--light-test") == 0)
            return runLightTest();
    }

    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--plot") != 0)
            continue;

        TrafficSystem traffic;
        std::cout << traffic.networkReport();
        const bool written = traffic.writeNetworkImage("network.png");
        std::cout << (written ? "Wrote network.png" : "Could not write network.png") << '\n';
        return written ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    CaptureOptions capture;
    bool startFullscreen = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        const bool hasValue = index + 1 < argc;
        if (argument == "--capture" && hasValue)
        {
            capture.enabled = true;
            capture.path = argv[++index];
        }
        else if (argument == "--frames" && hasValue)
            capture.frames = std::max(1, std::atoi(argv[++index]));
        else if (argument == "--view" && hasValue)
            capture.view = std::atoi(argv[++index]);
        else if (argument == "--time" && hasValue)
            capture.hour = static_cast<float>(std::atof(argv[++index]));
        else if (argument == "--no-hud")
            capture.hideHud = true;
        else if (argument == "--shading" && hasValue)
            capture.shading = std::clamp(std::atoi(argv[++index]), 0, 2);
        else if (argument == "--size" && hasValue)
        {
            // WIDTHxHEIGHT, for example 1920x1080.
            char* separator = nullptr;
            const long width = std::strtol(argv[++index], &separator, 10);
            const long height = (separator != nullptr && *separator == 'x') ? std::strtol(separator + 1, nullptr, 10) : 0;
            if (width > 0 && height > 0)
            {
                capture.width = static_cast<int>(width);
                capture.height = static_cast<int>(height);
            }
        }
        else if (argument == "--scale" && hasValue)
            capture.scale = static_cast<float>(std::atof(argv[++index])) < 0.9f ? RenderScaler::reducedScale : 1.0f;
        else if (argument == "--full-rate")
            capture.fullRate = true;
        else if (argument == "--graph")
            capture.graph = true;
        else if (argument == "--fullscreen")
            startFullscreen = true;
    }

    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() != GLFW_TRUE)
        return EXIT_FAILURE;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // Anti-aliasing now happens in the off-screen HDR target, so the window
    // itself needs no multisampling.
    glfwWindowHint(GLFW_SAMPLES, 0);

    // The window stays hidden until the first frame has been drawn, so the
    // one-off costs of first use (shader and texture upload inside the driver)
    // are paid before anything is on screen instead of as a visible hitch.
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    // 1920x1080 when the desktop has room for it; on a 1080p laptop screen the
    // window is maximised instead (F11 then gives true fullscreen 1080p).
    int windowWidth = 1920;
    int windowHeight = 1080;
    bool maximise = false;
    int workX = 0;
    int workY = 0;
    int workWidth = 1920;
    int workHeight = 1080;
    glfwGetMonitorWorkarea(glfwGetPrimaryMonitor(), &workX, &workY, &workWidth, &workHeight);
    if (capture.enabled)
    {
        windowWidth = capture.width;
        windowHeight = capture.height;
    }
    else if (workWidth < windowWidth || workHeight < windowHeight + 40)
    {
        maximise = true;
        windowWidth = std::min(windowWidth, workWidth);
        windowHeight = std::min(windowHeight, workHeight - 40);
    }

    GLFWwindow* window = glfwCreateWindow(
        windowWidth,
        windowHeight,
        "Interactive 3D Traffic Intersection Simulator",
        nullptr,
        nullptr);

    if (window == nullptr)
    {
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glfwSetWindowPos(window,
                     workX + std::max(0, (workWidth - windowWidth) / 2),
                     workY + std::max(0, (workHeight - windowHeight) / 2));

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0)
    {
        std::cerr << "Failed to load OpenGL functions through GLAD.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    if (!hasCore33Context())
    {
        std::cerr << "OpenGL 3.3 Core is required.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glDepthFunc(GL_LESS);

    int exitCode = EXIT_SUCCESS;
    try
    {
        Camera camera;
        TrafficSystem traffic(vehicleCount);
        DayNight dayNight;
        RenderScaler scaler;
        ApplicationState state;
        state.camera = &camera;
        state.traffic = &traffic;
        state.dayNight = &dayNight;
        state.scaler = &scaler;
        glfwSetWindowUserPointer(window, &state);
        glfwSetFramebufferSizeCallback(window, framebufferCallback);
        glfwSetCursorPosCallback(window, cursorCallback);
        glfwSetKeyCallback(window, keyCallback);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        Scene scene(traffic);
        Overlay overlay;
        Sky sky;
        HdrTarget hdr;
        PostProcess postProcess;
        FrameStats frameStats;
        WindowPlacement placement;

        if (capture.enabled)
        {
            applyCaptureView(camera, capture.view);
            if (capture.hour >= 0.0f)
                dayNight.setTime(capture.hour);
            state.showHelp = !capture.hideHud;
            state.shadingMode = capture.shading;
            state.showFrameGraph = capture.graph;
            state.fullRatePacing = capture.fullRate;
            scaler.setMode(capture.scale < 1.0f ? ResolutionMode::Reduced : ResolutionMode::Native);
        }

        // The simulation advances in fixed 1/60 s steps, independent of the
        // frame rate, so traffic behaves identically on a slow or a fast GPU.
        // Rendering then blends between the last two steps (render
        // interpolation), so motion is smooth at 60, 75, 144 Hz or anything else.
        constexpr float simulationStep = 1.0f / 60.0f;
        constexpr int maximumStepsPerFrame = 8;
        float simulationBacklog = 0.0f;
        std::vector<VehiclePose> poses;
        poses.reserve(64);

        // Everything one frame draws, from the current camera and poses.
        const auto renderFrame = [&](float alpha, double timeSeconds)
        {
            glfwGetFramebufferSize(window, &state.framebufferWidth, &state.framebufferHeight);
            const int outputWidth = std::max(state.framebufferWidth, 1);
            const int outputHeight = std::max(state.framebufferHeight, 1);
            const int renderWidth = std::max(1, static_cast<int>(std::lround(outputWidth * scaler.scale())));
            const int renderHeight = std::max(1, static_cast<int>(std::lround(outputHeight * scaler.scale())));

            frameStats.beginGpu();

            // 1. Sky and scene into the multisampled HDR target.
            hdr.resize(renderWidth, renderHeight);
            hdr.bindForScene();
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            const float aspect = static_cast<float>(outputWidth) / static_cast<float>(outputHeight);
            const glm::mat4 view = camera.viewMatrix();
            const glm::mat4 projection = camera.projectionMatrix(aspect);
            sky.render(view, projection, camera.position(), dayNight, static_cast<float>(timeSeconds));
            scene.render(
                view, projection, camera.position(),
                traffic, poses,
                dayNight, state.shadingMode,
                camera.mode() == CameraMode::Driver,
                camera.followedVehicleIndex(),
                static_cast<float>(timeSeconds));

            // 2. Resolve the samples, then bloom and tone-map into the window,
            //    scaling up if the scene was rendered smaller.
            hdr.resolve();
            postProcess.render(
                hdr.colorTexture(), renderWidth, renderHeight,
                outputWidth, outputHeight, dayNight.exposure());

            // 3. The HUD is drawn last, straight onto the tone-mapped image.
            if (!capture.hideHud)
            {
                PerformanceInfo performance;
                performance.fps = frameStats.fps();
                performance.frameMs = frameStats.frameMs();
                performance.gpuMs = frameStats.gpuMs();
                performance.renderWidth = renderWidth;
                performance.renderHeight = renderHeight;
                performance.scalePercent = static_cast<int>(std::lround(scaler.scale() * 100.0f));
                performance.resolutionMode = scaler.modeName();
                performance.showGraph = state.showFrameGraph;
                performance.pacingHz = state.pacingHz;
                performance.fullRatePacing = state.fullRatePacing;
                performance.frameHistory = frameStats.frameHistory().data();
                performance.historySize = frameStats.frameHistory().size();
                performance.historyHead = frameStats.head();

                overlay.render(
                    outputWidth,
                    outputHeight,
                    state.paused,
                    camera.modeName(),
                    traffic.phaseName(),
                    shadingModeName(state.shadingMode),
                    dayNight.timeText(),
                    dayNight.automatic(),
                    dayNight.streetLampsOn(),
                    traffic.vehicles().size(),
                    traffic.stats().overlapPairsNow,
                    performance,
                    state.showHelp);
            }

            frameStats.endGpu();
        };

        // Warm-up: draw one complete frame while the window is still hidden.
        traffic.interpolatePoses(0.0f, poses);
        camera.update(0.0f, poses);
        renderFrame(0.0f, glfwGetTime());
        glFinish();

        glfwShowWindow(window);
        if (maximise)
            glfwMaximizeWindow(window);
        if (startFullscreen)
            toggleFullscreen(window, placement);

        // Frame times for the --capture report (the first second is skipped:
        // it includes window creation).
        std::vector<float> captureFrameMs;
        captureFrameMs.reserve(static_cast<std::size_t>(capture.frames) + 1);
        double captureGpuMsTotal = 0.0;
        int captureGpuSamples = 0;
        int capturedFrames = 0;

        // For each slow frame, where the time went: our own work (simulation
        // and draw calls), the buffer swap (waiting on the GPU / display), or
        // the window-event pump (the operating system).
        struct Hitch
        {
            double atSeconds;
            float frameMs;
            float eventsMs;
            float workMs;
            float swapMs;
        };
        std::vector<Hitch> hitches;
        hitches.reserve(16);
        float lastEventsMs = 0.0f;
        float lastWorkMs = 0.0f;
        float lastSwapMs = 0.0f;

        double previousTime = glfwGetTime();
        while (glfwWindowShouldClose(window) == GLFW_FALSE)
        {
            const double currentTime = glfwGetTime();
            const float frameSeconds = static_cast<float>(currentTime - previousTime);
            previousTime = currentTime;
            frameStats.recordFrame(frameSeconds);
            if (capture.enabled && frameSeconds > 0.025f && currentTime > 1.0 && hitches.size() < 16)
                hitches.push_back({currentTime, frameSeconds * 1000.0f, lastEventsMs, lastWorkMs, lastSwapMs});

            glfwPollEvents();
            const double eventsDone = glfwGetTime();
            lastEventsMs = static_cast<float>((eventsDone - currentTime) * 1000.0);
            if (state.fullscreenRequested)
            {
                state.fullscreenRequested = false;
                toggleFullscreen(window, placement);
                state.pacingChanged = true;
            }
            if (state.pacingChanged)
            {
                state.pacingChanged = false;
                state.pacingHz = applyPacing(window, state.fullRatePacing);
            }

            // The real frame time drives everything; only a long stall (a
            // dragged window, a breakpoint) is cut short so nothing jumps.
            const float dt = std::min(frameSeconds, 0.25f);
            camera.processKeyboard(window, dt);
            if (!state.paused)
            {
                simulationBacklog += dt;
                int steps = 0;
                while (simulationBacklog >= simulationStep && steps < maximumStepsPerFrame)
                {
                    traffic.update(simulationStep);
                    dayNight.update(simulationStep);
                    simulationBacklog -= simulationStep;
                    ++steps;
                }
                if (simulationBacklog >= simulationStep)
                    simulationBacklog = std::fmod(simulationBacklog, simulationStep);   // never spiral
            }

            // How far the clock has run into the next step: the blend factor.
            const float alpha = simulationBacklog / simulationStep;
            traffic.interpolatePoses(alpha, poses);
            camera.update(dt, poses);
            scaler.update(frameSeconds, frameStats.frameMs(), frameStats.gpuMs());

            renderFrame(alpha, currentTime);

            if (capture.enabled)
            {
                ++capturedFrames;
                if (currentTime > 1.0 && capturedFrames > 30)
                {
                    captureFrameMs.push_back(frameSeconds * 1000.0f);
                    captureGpuMsTotal += frameStats.gpuMs();
                    ++captureGpuSamples;
                }

                if (capturedFrames >= capture.frames)
                {
                    const bool saved = saveFramebufferPng(
                        capture.path, state.framebufferWidth, state.framebufferHeight);
                    std::cout << (saved ? "Saved " : "Could not save ") << capture.path << '\n';

                    if (!captureFrameMs.empty())
                    {
                        std::vector<float> sorted = captureFrameMs;
                        std::sort(sorted.begin(), sorted.end());
                        double total = 0.0;
                        for (float ms : sorted)
                            total += ms;
                        const float average = static_cast<float>(total / static_cast<double>(sorted.size()));
                        const float p99 = sorted[std::min(sorted.size() - 1, sorted.size() * 99 / 100)];
                        const std::size_t overBudget = static_cast<std::size_t>(std::count_if(
                            sorted.begin(), sorted.end(), [](float ms) { return ms > 25.0f; }));
                        std::printf(
                            "Frames %zu at %dx%d (scene %d%%) | average %.2f ms (%.0f FPS) | 99th %.2f ms | "
                            "worst %.2f ms | over 25 ms: %zu | GPU %.2f ms\n",
                            sorted.size(), state.framebufferWidth, state.framebufferHeight,
                            static_cast<int>(std::lround(scaler.scale() * 100.0f)),
                            average, 1000.0f / average, p99, sorted.back(), overBudget,
                            captureGpuSamples > 0 ? captureGpuMsTotal / captureGpuSamples : 0.0);
                    }
                    for (const Hitch& hitch : hitches)
                        std::printf("  slow frame at %.2f s: %.1f ms (events %.1f, our work %.1f, swap %.1f)\n",
                                    hitch.atSeconds, hitch.frameMs, hitch.eventsMs, hitch.workMs, hitch.swapMs);
                    std::cout.flush();
                    exitCode = saved ? EXIT_SUCCESS : EXIT_FAILURE;
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
            }

            const double workDone = glfwGetTime();
            lastWorkMs = static_cast<float>((workDone - eventsDone) * 1000.0);
            glfwSwapBuffers(window);
            lastSwapMs = static_cast<float>((glfwGetTime() - workDone) * 1000.0);
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "Application error: " << error.what() << '\n';
        exitCode = EXIT_FAILURE;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
