#include "BlockSelection.h"
#include "Camera.h"
#include "Character.h"
#include "Inventory.h"
#include "PauseMenu.h"
#include "World.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

glm::ivec3 selectedBlock(-1);
bool hasSelected = false;
glm::ivec3 hitNormal(0);

static bool rayOrientedBox(const glm::vec3& origin, const glm::vec3& dir,
    const glm::vec3& center, const glm::vec3 axes[3], const glm::vec3& he,
    float maxT, float& tHit, glm::vec3& outN) {
    float tMin = 0.0f;
    float tMax = maxT;
    glm::vec3 p = center - origin;
    glm::vec3 nMin(0.0f);
    for (int i = 0; i < 3; ++i) {
        float e = glm::dot(axes[i], p);
        float f = glm::dot(axes[i], dir);
        if (std::fabs(f) > 1.0e-6f) {
            float t1 = (e + he[i]) / f;
            float t2 = (e - he[i]) / f;
            glm::vec3 n1 = axes[i];
            glm::vec3 n2 = -axes[i];
            if (t1 > t2) {
                std::swap(t1, t2);
                std::swap(n1, n2);
            }
            if (t1 > tMin) {
                tMin = t1;
                nMin = n1;
            }
            if (t2 < tMax) tMax = t2;
            if (tMin > tMax || tMax < 0.0f) return false;
        }
        else if (-e - he[i] > 0.0f || -e + he[i] < 0.0f) {
            return false;
        }
    }
    tHit = tMin;
    outN = nMin;
    return tHit <= maxT;
}

static glm::ivec3 dominantNormal(const glm::vec3& n) {
    float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
    if (ax < 1.0e-4f && ay < 1.0e-4f && az < 1.0e-4f) return glm::ivec3(0);
    if (ay >= ax && ay >= az) return glm::ivec3(0, n.y >= 0.0f ? 1 : -1, 0);
    if (ax >= az) return glm::ivec3(n.x >= 0.0f ? 1 : -1, 0, 0);
    return glm::ivec3(0, 0, n.z >= 0.0f ? 1 : -1);
}

static bool torchBox(int x, int y, int z, glm::vec3& center, glm::vec3 axes[3], glm::vec3& he) {
    int facing = blockMeta(x, y, z);
    glm::vec3 axis(0.0f, 1.0f, 0.0f);
    glm::vec3 base(x + 0.5f, (float)y, z + 0.5f);
    if (facing == 1) {
        axis = glm::normalize(glm::vec3(1, 1, 0));
        base = glm::vec3((float)x, y + 0.22f, z + 0.5f);
    }
    else if (facing == 2) {
        axis = glm::normalize(glm::vec3(-1, 1, 0));
        base = glm::vec3(x + 1.0f, y + 0.22f, z + 0.5f);
    }
    else if (facing == 3) {
        axis = glm::normalize(glm::vec3(0, 1, 1));
        base = glm::vec3(x + 0.5f, y + 0.22f, (float)z);
    }
    else if (facing == 4) {
        axis = glm::normalize(glm::vec3(0, 1, -1));
        base = glm::vec3(x + 0.5f, y + 0.22f, z + 1.0f);
    }
    glm::vec3 hint = std::abs(axis.y) > 0.95f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    axes[0] = glm::normalize(glm::cross(axis, hint));
    axes[1] = glm::normalize(glm::cross(axes[0], axis));
    axes[2] = axis;
    float half = 1.0f / 16.0f + 0.02f;
    float len = 10.0f / 16.0f + 0.045f;
    base -= axis * 0.015f;
    center = base + axis * (len * 0.5f);
    he = glm::vec3(half, half, len * 0.5f);
    return true;
}

static bool shapeHit(Block block, int x, int y, int z,
    const glm::vec3& origin, const glm::vec3& dir, float maxT, float& tHit, glm::vec3& outN) {
    if (block == Block::Torch) {
        glm::vec3 center, he, axes[3];
        torchBox(x, y, z, center, axes, he);
        return rayOrientedBox(origin, dir, center, axes, he, maxT, tHit, outN);
    }
    if (!isPlant(block)) return false;
    glm::vec3 center(x + 0.5f, y + 0.5f, z + 0.5f);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 diagonals[2] = {
        glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f)),
        glm::normalize(glm::vec3(-1.0f, 0.0f, 1.0f))
    };
    bool hit = false;
    float best = maxT;
    for (const glm::vec3& along : diagonals) {
        glm::vec3 axes[3] = { along, up, glm::normalize(glm::cross(along, up)) };
        glm::vec3 he(0.7072f, 0.5f, 0.07f);
        float t = 0.0f;
        glm::vec3 n(0.0f);
        if (!rayOrientedBox(origin, dir, center, axes, he, maxT, t, n)) continue;
        if (t <= best) {
            best = t;
            outN = n;
            hit = true;
        }
    }
    if (hit) tHit = best;
    return hit;
}

static bool fullCube(Block block) {
    return block != Block::Air && block != Block::Water && block != Block::Torch && !isPlant(block);
}

void updateSelectedBlock(GLFWwindow* window) {
    const float maxDist = 8.0f;
    glm::vec3 camPos = cameraPosition();
    if (cameraFirstPerson()) {
        glm::vec3 eye, focus;
        cameraRenderFrame(eye, focus);
        camPos = eye;
    }

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    if (h == 0) return;

    glm::mat4 proj = glm::perspective(glm::radians(60.0f), (float)w / (float)h, 0.1f, 250.0f);
    glm::mat4 view = glm::lookAt(camPos, camPos + cameraViewDirection(), glm::vec3(0, 1, 0));

    float cx = w / 2.0f, cy = h / 2.0f;
    glm::vec3 nearPoint = glm::unProject(glm::vec3(cx, cy, 0.0f), view, proj, glm::vec4(0, 0, w, h));
    glm::vec3 farPoint = glm::unProject(glm::vec3(cx, cy, 1.0f), view, proj, glm::vec4(0, 0, w, h));
    glm::vec3 rayDir = glm::normalize(farPoint - nearPoint);

    glm::vec3 pos = camPos;
    glm::ivec3 voxel = glm::floor(pos);
    glm::ivec3 step(
        rayDir.x > 0 ? 1 : (rayDir.x < 0 ? -1 : 0),
        rayDir.y > 0 ? 1 : (rayDir.y < 0 ? -1 : 0),
        rayDir.z > 0 ? 1 : (rayDir.z < 0 ? -1 : 0)
    );
    glm::vec3 tMax, tDelta;
    for (int i = 0; i < 3; ++i) {
        if (rayDir[i] > 0.0f) {
            tMax[i] = (glm::floor(pos[i]) + 1.0f - pos[i]) / rayDir[i];
            tDelta[i] = 1.0f / rayDir[i];
        }
        else if (rayDir[i] < 0.0f) {
            tMax[i] = (pos[i] - glm::floor(pos[i])) / -rayDir[i];
            tDelta[i] = 1.0f / -rayDir[i];
        }
        else {
            tMax[i] = std::numeric_limits<float>::infinity();
            tDelta[i] = std::numeric_limits<float>::infinity();
        }
    }

    hasSelected = false;
    hitNormal = glm::ivec3(0);
    float dist = 0.0f;
    glm::vec3 rayOrigin = camPos;

    auto acceptShape = [&](int x, int y, int z, float tExit) {
        Block block = blockAt(x, y, z);
        float t = 0.0f;
        glm::vec3 n(0.0f);
        if (!shapeHit(block, x, y, z, rayOrigin, rayDir, maxDist, t, n)) return false;
        if (t > tExit + 0.001f) return false;
        selectedBlock = glm::ivec3(x, y, z);
        hasSelected = true;
        hitNormal = dominantNormal(n);
        return true;
    };

    Block here = blockAt(voxel.x, voxel.y, voxel.z);
    float tExit = std::min(tMax.x, std::min(tMax.y, tMax.z));
    if (fullCube(here)) {
        selectedBlock = voxel;
        hasSelected = true;
        return;
    }
    if ((here == Block::Torch || isPlant(here)) && acceptShape(voxel.x, voxel.y, voxel.z, tExit))
        return;

    while (dist < maxDist) {
        int axis;
        if (tMax.x < tMax.y && tMax.x < tMax.z) axis = 0;
        else if (tMax.y < tMax.z) axis = 1;
        else axis = 2;

        voxel[axis] += step[axis];
        dist = tMax[axis];
        tMax[axis] += tDelta[axis];

        if (dist > maxDist) break;

        Block block = blockAt(voxel.x, voxel.y, voxel.z);
        if (fullCube(block)) {
            selectedBlock = voxel;
            hasSelected = true;
            hitNormal = glm::ivec3(0);
            hitNormal[axis] = -step[axis];
            return;
        }
        float leave = std::min(tMax.x, std::min(tMax.y, tMax.z));
        if ((block == Block::Torch || isPlant(block)) && acceptShape(voxel.x, voxel.y, voxel.z, leave))
            return;
    }
}

static bool gInventory = false;

bool inventoryOpen() { return gInventory; }
void setInventoryOpen(bool open) {
    if (gInventory && !open) inventoryCancelGesture();
    gInventory = open;
}

Block placeSlotBlock(int slot) {
    if (slot < 0 || slot >= kHotbarSlots) return Block::Air;
    return inventorySlot(slot);
}

const char* placeSlotName(int slot) {
    return inventoryBlockName(placeSlotBlock(slot));
}

void selectPlaceSlot(int slot) {
    inventorySelect(slot);
}

void setHotbarBlock(int slot, Block block) {
    if (slot < 0 || slot >= kHotbarSlots) return;
    inventorySetSlot(slot, block);
    inventorySelect(slot);
}

int selectedPlaceSlot() { return inventorySelected(); }

void pollPlaceHotkeys(GLFWwindow* window) {
    static bool wasDown[kHotbarSlots] = {};
    const int keys[kHotbarSlots] = {
        GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_5,
        GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9
    };
    for (int i = 0; i < kHotbarSlots; ++i) {
        bool down = glfwGetKey(window, keys[i]) == GLFW_PRESS;
        if (down && !wasDown[i]) inventorySelect(i);
        wasDown[i] = down;
    }
}

const char* selectedPlaceName() {
    return inventoryBlockName(inventorySlot(inventorySelected()));
}

void handleMouseClick(GLFWwindow*, int button, int action, int) {
    if (gInventory || paused || !gameMenuInWorld() || action != GLFW_PRESS) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT && cameraOfferLedgeClimb()) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT)
        characterCueAction(button == GLFW_MOUSE_BUTTON_LEFT);
    if (!hasSelected) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (blockAt(selectedBlock.x, selectedBlock.y, selectedBlock.z) == Block::Water) return;
        setBlock(selectedBlock.x, selectedBlock.y, selectedBlock.z, Block::Air);
    }
    else {
        Block hit = blockAt(selectedBlock.x, selectedBlock.y, selectedBlock.z);
        Block gPlace = placeSlotBlock(selectedPlaceSlot());
        if (gPlace == Block::Air) return;
        if (gPlace == Block::Torch && hit != Block::Water) {
            glm::ivec3 p = selectedBlock + hitNormal;
            int meta = 0;
            if (hitNormal.y > 0) {
                if (!allowsPlacementOnTop(hit)) return;
            }
            else if (hitNormal.y < 0 || !solid(selectedBlock.x, selectedBlock.y, selectedBlock.z)) {
                return;
            }
            else if (hitNormal.x > 0) meta = 1;
            else if (hitNormal.x < 0) meta = 2;
            else if (hitNormal.z > 0) meta = 3;
            else meta = 4;
            if (!inBounds(p.x, p.y, p.z) || occupied(p.x, p.y, p.z)) return;
            setBlock(p.x, p.y, p.z, Block::Torch, meta);
            return;
        }
        if (isPlant(gPlace)) {
            if (hitNormal.y <= 0 || !allowsPlacementOnTop(hit)) return;
            glm::ivec3 p = selectedBlock + hitNormal;
            if (!inBounds(p.x, p.y, p.z) || occupied(p.x, p.y, p.z)) return;
            setBlock(p.x, p.y, p.z, gPlace);
            return;
        }
        glm::ivec3 p = selectedBlock;
        if (hit != Block::Water) {
            if (hitNormal.y > 0 && !allowsPlacementOnTop(hit))
                return;
            p = selectedBlock + hitNormal;
        }
        if (!inBounds(p.x, p.y, p.z))
            return;
        Block there = blockAt(p.x, p.y, p.z);
        if (there != Block::Air && there != Block::Water)
            return;
        int meta = 0;
        if (gPlace == Block::Log) {
            if (hitNormal.x != 0) meta = 1;
            else if (hitNormal.z != 0) meta = 2;
        }
        setBlock(p.x, p.y, p.z, gPlace, meta);
    }
}
