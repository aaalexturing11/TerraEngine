#pragma once

#include <glm/glm.hpp>

bool isOccludedVoxelDDA(const glm::vec3& start, const glm::vec3& dirToLight, float maxDist);
