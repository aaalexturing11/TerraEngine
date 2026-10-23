#pragma once

#include "World.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

extern glm::ivec3 selectedBlock;
extern bool hasSelected;
extern glm::ivec3 hitNormal;

void updateSelectedBlock(GLFWwindow* window);
void handleMouseClick(GLFWwindow* window, int button, int action, int mods);
void pollPlaceHotkeys(GLFWwindow* window);
void selectPlaceSlot(int slot);
void setHotbarBlock(int slot, Block block);
int selectedPlaceSlot();
Block placeSlotBlock(int slot);
bool inventoryOpen();
void setInventoryOpen(bool open);
const char* placeSlotName(int slot);
const char* selectedPlaceName();
