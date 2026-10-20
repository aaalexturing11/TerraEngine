#pragma once

#include "Lighting.h"
#include <glm/glm.hpp>

bool initBlockShaders();
void beginShadowPass(const DayNightLighting& light, const glm::vec3& focus, int mapSize);
void endShadowPass(int viewW, int viewH);
void bindOpaqueShader(const DayNightLighting& light, float time);
void bindPlantShadowShader(float time);
void bindWaterShader(const glm::mat4& viewProj, const glm::vec3& camPos, const glm::vec3& sky, const glm::vec3& sunDir, float sunAmt, float time);
void bindFixedPipeline();
