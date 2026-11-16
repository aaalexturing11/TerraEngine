#pragma once

#include "World.h"
#include <glm/glm.hpp>

struct DayNightLighting {
    glm::vec3 sunDir{ 0.0f, 1.0f, 0.0f };
    glm::vec3 moonDir{ 0.0f, -1.0f, 0.0f };
    glm::vec3 sunColor{ 1.0f, 0.97f, 0.88f };
    glm::vec3 moonColor{ 0.45f, 0.55f, 0.9f };
    glm::vec3 ambientRGB{ 0.2f };
    glm::vec3 skyColor{ 0.53f, 0.81f, 0.92f };
    float dayFactor = 1.0f;
    float dawnFactor = 0.0f;
    float duskFactor = 0.0f;
    float sunIntensity = 1.0f;
    float moonIntensity = 0.0f;
    float starFactor = 0.0f;
    int moonPhase = 0;
};

struct FaceLight {
    glm::vec3 albedo{ 0.3f, 0.7f, 0.3f };
    float gi = 1.0f;
    float emission = 0.0f;
    float caustic = 0.0f;
    float ao = 1.0f;
    float blockLight = 0.0f;
};

DayNightLighting computeDayNightLighting(double timeSeconds);
FaceLight evaluateFace(int bx, int by, int bz, const glm::vec3& normal);
