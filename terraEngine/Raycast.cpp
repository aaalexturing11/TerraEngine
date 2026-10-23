#include "Raycast.h"
#include "World.h"

#include <cmath>
#include <limits>

bool isOccludedVoxelDDA(const glm::vec3& start, const glm::vec3& dirToLight, float maxDist) {
    glm::vec3 dir = dirToLight;
    float len = glm::length(dir);
    if (len < 1e-6f) return false;
    dir /= len;

    glm::vec3 pos = start;
    glm::ivec3 voxel = glm::ivec3(glm::floor(pos));
    glm::ivec3 step(
        (dir.x > 0.0f) ? 1 : (dir.x < 0.0f ? -1 : 0),
        (dir.y > 0.0f) ? 1 : (dir.y < 0.0f ? -1 : 0),
        (dir.z > 0.0f) ? 1 : (dir.z < 0.0f ? -1 : 0)
    );

    glm::vec3 tMax(0.0f), tDelta(0.0f);
    for (int i = 0; i < 3; ++i) {
        if (dir[i] > 0.0f) {
            tMax[i] = (glm::floor(pos[i]) + 1.0f - pos[i]) / dir[i];
            tDelta[i] = 1.0f / dir[i];
        }
        else if (dir[i] < 0.0f) {
            tMax[i] = (pos[i] - glm::floor(pos[i])) / (-dir[i]);
            tDelta[i] = 1.0f / (-dir[i]);
        }
        else {
            tMax[i] = std::numeric_limits<float>::infinity();
            tDelta[i] = std::numeric_limits<float>::infinity();
        }
    }

    float traveled = 0.0f;
    while (traveled <= maxDist) {
        if (solid(voxel.x, voxel.y, voxel.z)) {
            return true;
        }

        int axis = 0;
        if (tMax.y < tMax.x) axis = 1;
        if (tMax.z < tMax[axis]) axis = 2;

        voxel[axis] += step[axis];
        traveled = tMax[axis];
        tMax[axis] += tDelta[axis];
    }

    return false;
}
