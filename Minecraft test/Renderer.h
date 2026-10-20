#pragma once

#include "Lighting.h"
#include <glm/glm.hpp>

void setup3D(int w, int h);
void drawCubeOutline(float cx, float cy, float cz, float size);
void drawCrosshair(int w, int h);
void drawBillboardTextured(const glm::vec3& pos, const glm::vec3& viewDir, float radius, unsigned int tex, float brightness);

void renderShadowCasters(int renderRadiusXZ, int renderMaxY);
struct BlockTextures {
    unsigned grassTop = 0;
    unsigned grassSide = 0;
    unsigned dirt = 0;
    unsigned stone = 0;
    unsigned torch = 0;
    unsigned torchAlt = 0;
    unsigned logSide = 0;
    unsigned logTop = 0;
    unsigned leaves = 0;
    unsigned tallGrass = 0;
    unsigned poppy = 0;
    unsigned triteleia = 0;
    unsigned triteleiaRed = 0;
};

void setBlockTextures(const BlockTextures& textures);
void renderOpaqueBlocks(int renderRadiusXZ, int renderMaxY);
void renderWaterBlocks(int renderRadiusXZ, int renderMaxY);
