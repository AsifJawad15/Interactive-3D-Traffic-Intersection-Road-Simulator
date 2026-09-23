#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Camera.h"
#include "DayNight.h"
#include "Framebuffer.h"
#include "Overlay.h"
#include "PostProcess.h"
#include "Scene.h"
#include "Sky.h"
#include "Screenshot.h"
#include "Simulation.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

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
        bool roundabout = false;
        int shading = 2;
    };

    // Camera poses for --view. Yaw 90 looks along +z (north), yaw 0 along +x.
    void applyCaptureView(Camera& camera, int view)
    {
        switch (view)
        {
        case 1: camera.setFreePose({2.5f, 1.7f, -36.0f}, 90.0f, -3.0f); break;   // street level, south arm
        case 2: camera.setFreePose({-34.0f, 3.2f, 6.0f}, 0.0f, -7.0f); break;    // side road, west arm
        case 3: camera.setFreePose({0.0f, 58.0f, -72.0f}, 90.0f, -36.0f); break; // high overview
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
    };

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
        else if (key == GLFW_KEY_M && state->traffic != nullptr)
            state->traffic->toggleMode();
        else if (key == GLFW_KEY_P)
            state->paused = !state->paused;
        else if (key == GLFW_KEY_H)
            state->showHelp = !state->showHelp;
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

    // Headless endurance test: runs the traffic for simulated minutes at the
    // real 60 Hz step and checks that no two vehicle bodies ever overlapped,
    // that nobody was stuck, and that every approach kept moving.
    //     OpenGLMiniProject.exe --soak 30 7 [--cars 12] [--mode signals|roundabout|both]
    int runSoak(float minutes, unsigned int seed, std::size_t cars, const std::string& modes, float traceFrom)
    {
        constexpr float step = 1.0f / 60.0f;
        constexpr float longestAllowedStop = 60.0f;
        const long long steps = static_cast<long long>(minutes * 60.0f / step);
        bool allPassed = true;

        for (const IntersectionMode mode : {IntersectionMode::Signals, IntersectionMode::Roundabout})
        {
            const bool signals = mode == IntersectionMode::Signals;
            if ((signals && modes == "roundabout") || (!signals && modes == "signals"))
                continue;

            const auto started = std::chrono::steady_clock::now();
            TrafficSystem traffic(cars, seed);
            traffic.setMode(mode);
            traffic.resetStats();
            // --trace [T] prints the whole junction every 2 s, twenty times,
            // starting at simulated second T, or by default from the moment
            // some vehicle has been standing still for too long.
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

            const TrafficStats& stats = traffic.stats();
            const auto [fewest, most] = std::minmax_element(
                stats.tripsPerApproach.begin(), stats.tripsPerApproach.end());
            const bool passed = stats.overlapSteps == 0 &&
                                stats.longestStop <= longestAllowedStop &&
                                *fewest > 0;
            allPassed = allPassed && passed;

            std::printf(
                "%-10s seed %-5u cars %2zu  %5.1f min | overlaps %zu | closest gap %.2f m | "
                "longest stop %5.1f s | trips %4zu (N %zu E %zu S %zu W %zu) | %.1f s wall | %s\n",
                signals ? "SIGNALS" : "ROUNDABOUT", seed, cars, stats.simulatedSeconds / 60.0,
                stats.overlapSteps, stats.closestBodyGap, stats.longestStop, stats.trips,
                stats.tripsPerApproach[0], stats.tripsPerApproach[2],
                stats.tripsPerApproach[1], stats.tripsPerApproach[3],
                wallSeconds, passed ? "PASS" : "FAIL");
            if (!passed)
                std::printf("%s", traffic.describe().c_str());
            (void)most;
        }
        return allPassed ? EXIT_SUCCESS : EXIT_FAILURE;
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

    std::size_t vehicleCount = 8;
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
        std::string modes = "both";
        float traceFrom = -2.0f;
        for (int other = 1; other < argc; ++other)
        {
            if (std::strcmp(argv[other], "--mode") == 0 && other + 1 < argc)
                modes = argv[other + 1];
            if (std::strcmp(argv[other], "--trace") == 0)
            {
                traceFrom = -1.0f;
                if (other + 1 < argc && argv[other + 1][0] != '-')
                    traceFrom = static_cast<float>(std::atof(argv[other + 1]));
            }
        }
        return runSoak(std::max(minutes, 0.1f), seed, vehicleCount, modes, traceFrom);
    }

    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--plot") != 0)
            continue;

        TrafficSystem traffic;
        std::cout << traffic.topDownPlot(false) << '\n' << traffic.topDownPlot(true);
        return EXIT_SUCCESS;
    }

    CaptureOptions capture;
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
        else if (argument == "--roundabout")
            capture.roundabout = true;
        else if (argument == "--shading" && hasValue)
            capture.shading = std::clamp(std::atoi(argv[++index]), 0, 2);
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

    GLFWwindow* window = glfwCreateWindow(
        1280,
        720,
        "Interactive 3D Traffic Intersection Simulator",
        nullptr,
        nullptr);

    if (window == nullptr)
    {
        glfwTerminate();
        return EXIT_FAILURE;
    }

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
        ApplicationState state;
        state.camera = &camera;
        state.traffic = &traffic;
        state.dayNight = &dayNight;
        glfwGetFramebufferSize(window, &state.framebufferWidth, &state.framebufferHeight);
        glfwSetWindowUserPointer(window, &state);
        glfwSetFramebufferSizeCallback(window, framebufferCallback);
        glfwSetCursorPosCallback(window, cursorCallback);
        glfwSetKeyCallback(window, keyCallback);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glViewport(0, 0, state.framebufferWidth, state.framebufferHeight);

        Scene scene;
        Overlay overlay;
        Sky sky;
        HdrTarget hdr;
        PostProcess postProcess;

        if (capture.enabled)
        {
            applyCaptureView(camera, capture.view);
            if (capture.hour >= 0.0f)
                dayNight.setTime(capture.hour);
            if (capture.roundabout)
                traffic.setMode(IntersectionMode::Roundabout);
            state.showHelp = !capture.hideHud;
            state.shadingMode = capture.shading;
        }
        int capturedFrames = 0;

        // The simulation advances in fixed 1/60 s steps, independent of the
        // frame rate, so traffic behaves identically on a slow or a fast GPU.
        constexpr float simulationStep = 1.0f / 60.0f;
        float simulationBacklog = 0.0f;

        double previousTime = glfwGetTime();
        double fpsWindowStart = previousTime;
        int renderedFrames = 0;
        float displayedFps = 0.0f;

        while (glfwWindowShouldClose(window) == GLFW_FALSE)
        {
            const double currentTime = glfwGetTime();
            const float dt = std::min(static_cast<float>(currentTime - previousTime), 0.05f);
            previousTime = currentTime;

            glfwPollEvents();
            camera.processKeyboard(window, dt);
            if (!state.paused)
            {
                simulationBacklog += dt;
                int steps = 0;
                while (simulationBacklog >= simulationStep && steps < 5)
                {
                    traffic.update(simulationStep);
                    dayNight.update(simulationStep);
                    simulationBacklog -= simulationStep;
                    ++steps;
                }
                if (steps == 5)
                    simulationBacklog = 0.0f;   // never spiral when a frame stalls
            }
            camera.update(dt, traffic.vehicles());

            ++renderedFrames;
            const double fpsElapsed = currentTime - fpsWindowStart;
            if (fpsElapsed >= 0.5)
            {
                displayedFps = static_cast<float>(renderedFrames / fpsElapsed);
                renderedFrames = 0;
                fpsWindowStart = currentTime;
            }

            // 1. Sky and scene into the multisampled HDR target.
            hdr.resize(state.framebufferWidth, state.framebufferHeight);
            hdr.bindForScene();
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            const float aspect = static_cast<float>(state.framebufferWidth) /
                                 static_cast<float>(state.framebufferHeight);
            const glm::mat4 view = camera.viewMatrix();
            const glm::mat4 projection = camera.projectionMatrix(aspect);
            sky.render(view, projection, camera.position(), dayNight, static_cast<float>(currentTime));
            scene.render(
                view, projection, camera.position(),
                traffic, dayNight, state.shadingMode,
                camera.mode() == CameraMode::Driver,
                camera.followedVehicleIndex(),
                static_cast<float>(currentTime));

            // 2. Resolve the samples, then bloom and tone-map into the window.
            hdr.resolve();
            postProcess.render(
                hdr.colorTexture(), state.framebufferWidth, state.framebufferHeight,
                dayNight.exposure());

            // 3. The HUD is drawn last, straight onto the tone-mapped image.
            if (!capture.hideHud)
                overlay.render(
                state.framebufferWidth,
                state.framebufferHeight,
                displayedFps,
                state.paused,
                camera.modeName(),
                traffic.phaseName() + " | " + traffic.modeName(),
                shadingModeName(state.shadingMode),
                dayNight.timeText(),
                dayNight.automatic(),
                dayNight.streetLampsOn(),
                traffic.vehicles().size(),
                traffic.stats().overlapPairsNow,
                state.showHelp);

            if (capture.enabled && ++capturedFrames >= capture.frames)
            {
                const bool saved = saveFramebufferPng(
                    capture.path, state.framebufferWidth, state.framebufferHeight);
                std::cout << (saved ? "Saved " : "Could not save ") << capture.path << std::endl;
                exitCode = saved ? EXIT_SUCCESS : EXIT_FAILURE;
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }

            glfwSwapBuffers(window);
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
