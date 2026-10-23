#pragma once

#include "World.h"

// 9 de hotbar (0-8), 27 de almacén (9-35) y la mano izquierda (36).
constexpr int kHotbarSlots = 9;
constexpr int kMainSlots = 27;
constexpr int kOffhandSlot = 36;
constexpr int kInventorySlots = 37;

enum class HudMode { Playing = 0, Hidden = 1, Clean = 2 };

void inventoryTick(float dt);
void inventoryCancelGesture();
void inventorySelect(int slot);
int inventorySelected();
Block inventorySlot(int index);
void inventorySetSlot(int index, Block block);
Block inventoryOffhand();
const char* inventoryBlockName(Block block);

// Intercambia el slot seleccionado con la mano izquierda. No duplica.
bool inventorySwapOffhand();
float inventorySwapAmount();
HudMode inventoryHud();
void inventoryCycleHud();

void inventoryDrawHotbar(int width, int height, float time);
void inventoryDrawScreen(int width, int height, float time);
