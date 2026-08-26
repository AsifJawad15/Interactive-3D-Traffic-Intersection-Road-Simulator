#pragma once

#include <glm/vec3.hpp>

#include <string>

class DayNight
{
public:
    void update(float dt);
    void reset();

    void toggleAutomatic();
    void setDay();
    void setNight();
    void toggleStreetLamps();

    float timeOfDay() const { return timeOfDay_; }
    bool automatic() const { return automatic_; }
    bool streetLampsOn() const;
    glm::vec3 ambientLight() const;
    glm::vec3 sunDirection() const;
    glm::vec3 sunColor() const;
    glm::vec3 skyColor() const;
    float daylightAmount() const;
    std::string timeText() const;

private:
    float timeOfDay_ = 10.0f;
    bool automatic_ = true;
    bool lampOverride_ = false;
    bool manualLampsOn_ = false;
};
