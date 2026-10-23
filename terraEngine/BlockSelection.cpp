#include "BlockSelection.h"
#include "Camera.h"
#include "Character.h"
#include "PauseMenu.h"
#include "World.h"

#include <glm/gtc/matrix_transform.hpp>
#include <limits>

glm::ivec3 selectedBlock(-1);
bool hasSelected = false;
glm::ivec3 hitNormal(0);

void updateSelectedBlock(GLFWwindow* window) {
    const float maxDist = 8.0f;
    glm::vec3 camPos = cameraPosition();

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
    auto selectable = [](int x, int y, int z) {
        Block b = blockAt(x, y, z);
        return b != Block::Air && b != Block::Water;
    };

    if (selectable(voxel.x, voxel.y, voxel.z)) {
        selectedBlock = voxel;
        hasSelected = true;
        return;
    }

    while (dist < maxDist) {
        int axis;
        if (tMax.x < tMax.y && tMax.x < tMax.z) axis = 0;
        else if (tMax.y < tMax.z) axis = 1;
        else axis = 2;

        voxel[axis] += step[axis];
        dist = tMax[axis];
        tMax[axis] += tDelta[axis];

        if (dist > maxDist) break;

        if (selectable(voxel.x, voxel.y, voxel.z)) {
            selectedBlock = voxel;
            hasSelected = true;
            hitNormal = glm::ivec3(0);
            hitNormal[axis] = -step[axis];
            return;
        }
    }
}

static bool gInventory = false;

bool inventoryOpen() { return gInventory; }
void setInventoryOpen(bool open) { gInventory = open; }

static Block kSlots[10] = {
    Block::Grass, Block::Water, Block::Torch, Block::Lava, Block::Dirt,
    Block::Log, Block::Leaves, Block::TallGrass, Block::Poppy, Block::Triteleia
};
static int gSlot = 0;

Block placeSlotBlock(int slot) {
    if (slot < 0 || slot > 9) return Block::Grass;
    return kSlots[slot];
}

const char* placeSlotName(int slot) {
    switch (placeSlotBlock(slot)) {
    case Block::Water: return "Agua";
    case Block::Torch: return "Antorcha";
    case Block::Lava: return "Lava";
    case Block::Dirt: return "Tierra";
    case Block::Log: return "Tronco";
    case Block::Leaves: return "Hojas";
    case Block::TallGrass: return "Pasto";
    case Block::Poppy: return "Amapola";
    case Block::Triteleia: return "Flor";
    case Block::TriteleiaRed: return "Flor rosa";
    case Block::Stone: return "Piedra";
    case Block::Obsidian: return "Obsidiana";
    default: return "Pasto";
    }
}

void selectPlaceSlot(int slot) {
    if (slot < 0) slot = 0;
    if (slot > 9) slot = 9;
    gSlot = slot;
}

void setHotbarBlock(int slot, Block block) {
    if (slot < 0 || slot > 9) return;
    kSlots[slot] = block;
    gSlot = slot;
}

int selectedPlaceSlot() { return gSlot; }

void pollPlaceHotkeys(GLFWwindow* window) {
    const int keys[10] = {
        GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_5,
        GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9, GLFW_KEY_0
    };
    for (int i = 0; i < 10; ++i)
        if (glfwGetKey(window, keys[i]) == GLFW_PRESS) gSlot = i;
}

const char* selectedPlaceName() {
    return placeSlotName(gSlot);
}

void handleMouseClick(GLFWwindow*, int button, int action, int) {
    if (gInventory || paused || !gameMenuInWorld() || action != GLFW_PRESS) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT)
        characterCueAction(button == GLFW_MOUSE_BUTTON_LEFT);
    if (!hasSelected) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (blockAt(selectedBlock.x, selectedBlock.y, selectedBlock.z) == Block::Water) return;
        setBlock(selectedBlock.x, selectedBlock.y, selectedBlock.z, Block::Air);
    }
    else {
        Block hit = blockAt(selectedBlock.x, selectedBlock.y, selectedBlock.z);
        Block gPlace = placeSlotBlock(gSlot);
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
