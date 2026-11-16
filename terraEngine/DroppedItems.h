#pragma once

#include "World.h"
#include "Lighting.h"
#include <glm/glm.hpp>

bool droppedItemsQueue(Block block, int count);
void droppedItemsUpdate(float dt);
void droppedItemsDraw(const DayNightLighting& light, float time, const glm::vec3& eye, const glm::vec3& look);
int droppedItemsTorchLights(glm::vec3* tips, int maxCount, float maxDist);
