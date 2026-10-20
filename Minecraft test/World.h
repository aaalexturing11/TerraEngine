#pragma once

#include "Config.h"

enum class Block : unsigned char {
    Air = 0,
    Grass = 1,
    Water = 2,
    Torch = 3,
    Lava = 4,
    Stone = 5,
    Obsidian = 6,
    Dirt = 7,
    Log = 8,
    Leaves = 9,
    TallGrass = 10,
    Poppy = 11,
    Triteleia = 12,
    TriteleiaRed = 13
};

inline bool isPlant(Block block) {
    return block == Block::TallGrass || block == Block::Poppy
        || block == Block::Triteleia || block == Block::TriteleiaRed;
}

struct BlockInfo {
    bool solid = false;
    bool placeableOnTop = true;
    bool fluid = false;
    bool blocksSky = false;
};

const BlockInfo& blockInfo(Block block);
bool allowsPlacementOnTop(Block support);

bool inBounds(int x, int y, int z);
bool occupied(int x, int y, int z);
bool solid(int x, int y, int z);
Block blockAt(int x, int y, int z);

void worldSetSeed(unsigned seed);
unsigned worldGetSeed();
void worldReset();
void initializeWorld();
int worldStreamAround(float x, float z);
int worldStreamStep(float x, float z, int inner, int budget, int radius = LOAD_RADIUS);
int worldMissingAround(float x, float z, int radius);
bool worldFindSpawn(int& x, int& y, int& z);
void worldFixSpawn(int& x, int& y, int& z);
int loadedChunkCount();
void loadedChunkCoord(int i, int& cx, int& cz);
void worldColumnSpan(int x, int z, int& y0, int& y1);

void setBlock(int x, int y, int z, Block value, int meta = 0);
int blockMeta(int x, int y, int z);

float propagatedLightAt(int x, int y, int z);
void cellLight(int x, int y, int z, float& sky, float& lamp);

void markMeshArea(int x0, int z0, int x1, int z1);
bool consumeMeshDirty(int& x0, int& z0, int& x1, int& z1);
float sampleSkyForFace(int bx, int by, int bz, float nx, float ny, float nz);
float sampleLampForFace(int bx, int by, int bz, float nx, float ny, float nz);
float causticAmount(int x, int y, int z);

void tickWater(float dt);
void waterImpact(int x, int y, int z, float vy, float mass);

int waterLevelAt(int x, int y, int z);
float waterFill01(int x, int y, int z);
float waterSurfaceY(int x, int z);
bool waterHasSurface(int x, int z);
bool waterHasFall(int x, int z);
float waterFallTopY(int x, int z);
float waterFallBotY(int x, int z);
bool waterSwimmable(int x, int y, int z);
float waterPoolDepth(int x, int z);
void refreshWaterHeights();
void markWaterMeshDirty();
bool consumeWaterMeshDirty();
int waterCellCount();
void getWaterCell(int i, int& x, int& y, int& z);
void waterFlow(int x, int y, int z, float& fx, float& fz, float& down);
