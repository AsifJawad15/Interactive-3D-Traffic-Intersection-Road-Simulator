#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Camera.h"
#include "DayNight.h"
#include "Overlay.h"
#include "Scene.h"
#include "Simulation.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

namespace
{
    struct ApplicationState
    {
        Camera* camera = nullptr;
        TrafficSystem* traffic = nullptr;
        DayNight* dayNight = nullptr;
        int framebufferWidth = 1280;
        int framebufferHeight = 720;
        bool paused = false;
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

    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--plot") != 0)
            continue;

        TrafficSystem traffic;
        std::cout << traffic.topDownPlot(false) << '\n' << traffic.topDownPlot(true);
        return EXIT_SUCCESS;
    }

    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() != GLFW_TRUE)
        return EXIT_FAILURE;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

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
        TrafficSystem traffic;
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
                traffic.update(dt);
                dayNight.update(dt);
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

            const glm::vec3 skyColor = dayNight.skyColor();
            glClearColor(skyColor.r, skyColor.g, skyColor.b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            const float aspect = static_cast<float>(state.framebufferWidth) /
                                 static_cast<float>(state.framebufferHeight);
            scene.render(
                camera.viewMatrix(), camera.projectionMatrix(aspect), camera.position(),
                traffic, dayNight, state.shadingMode,
                camera.mode() == CameraMode::Driver,
                camera.followedVehicleIndex(),
                static_cast<float>(currentTime));
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
                traffic.vehicles().size());

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
