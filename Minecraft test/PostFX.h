#pragma once

#include "Lighting.h"
#include <glm/glm.hpp>

bool initPostFX();
void beginScenePass(int width, int height);
void scenePassSize(int& width, int& height);
void blitSceneForWater();
void bindWaterInputs();
void presentScene(int width, int height, const DayNightLighting& light, const glm::vec2& sunUv, float godStrength, bool underwater, float time);
