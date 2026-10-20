#include "World.h"
#include "FluidSolver.h"

#include <algorithm>
#include <cmath>
#include <vector>

Block blocks[GRID][HEIGHT][GRID] = {};

const BlockInfo& blockInfo(Block block) {
    static const BlockInfo kAir{};
    static const BlockInfo kGrass{ true, true, false, true };
    static const BlockInfo kWater{ false, false, true, false };
    static const BlockInfo kTorch{ false, true, false, false };
    static const BlockInfo kLava{ true, false, true, true };
    static const BlockInfo kStone{ true, true, false, true };
    static const BlockInfo kObsidian{ true, true, false, true };
    static const BlockInfo kDirt{ true, true, false, true };
    static const BlockInfo kLog{ true, true, false, true };
    static const BlockInfo kLeaves{ true, true, false, false };
    static const BlockInfo kPlant{ false, false, false, false };
    switch (block) {
    case Block::Grass: return kGrass;
    case Block::Water: return kWater;
    case Block::Torch: return kTorch;
    case Block::Lava: return kLava;
    case Block::Stone: return kStone;
    case Block::Obsidian: return kObsidian;
    case Block::Dirt: return kDirt;
    case Block::Log: return kLog;
    case Block::Leaves: return kLeaves;
    case Block::TallGrass:
    case Block::Poppy:
    case Block::Triteleia:
    case Block::TriteleiaRed: return kPlant;
    default: return kAir;
    }
}

bool allowsPlacementOnTop(Block support) {
    return blockInfo(support).placeableOnTop;
}

static float skylight[GRID][HEIGHT][GRID] = {};
static float skyField[GRID][HEIGHT][GRID] = {};
static float lampField[GRID][HEIGHT][GRID] = {};
static bool waterSource[GRID][HEIGHT][GRID] = {};
static unsigned char waterLevel[GRID][HEIGHT][GRID] = {};
static float waterFill[GRID][HEIGHT][GRID] = {};
static unsigned char gMeta[GRID][HEIGHT][GRID] = {};
float waterHeightXZ[GRID * GRID] = {};

struct WaterCell { int x, y, z; };
static std::vector<WaterCell> waterCells;
static float waterClock = 0.0f;

bool inBounds(int x, int y, int z) {
    return (x >= 0 && x < GRID && y >= 0 && y < HEIGHT && z >= 0 && z < GRID);
}

Block blockAt(int x, int y, int z) {
    if (!inBounds(x, y, z)) return Block::Air;
    return blocks[x][y][z];
}

bool occupied(int x, int y, int z) {
    return blockAt(x, y, z) != Block::Air;
}

bool solid(int x, int y, int z) {
    return blockInfo(blockAt(x, y, z)).solid;
}

static void verticalColumn(int x, int z) {
    float sky = 1.0f;
    for (int y = HEIGHT - 1; y >= 0; --y) {
        Block b = blocks[x][y][z];
        if (blockInfo(b).blocksSky) {
            skylight[x][y][z] = 0.0f;
            sky = 0.0f;
        }
        else {
            if (b == Block::Water) sky *= 0.72f;
            skylight[x][y][z] = sky;
        }
    }
}

using Field = float[GRID][HEIGHT][GRID];

static void seedCell(int x, int y, int z) {
    Block b = blocks[x][y][z];
    if (blockInfo(b).blocksSky && b != Block::Lava) {
        skyField[x][y][z] = 0.0f;
        lampField[x][y][z] = 0.0f;
    }
    else if (b == Block::Lava) {
        skyField[x][y][z] = 0.0f;
        lampField[x][y][z] = 1.0f;
    }
    else if (b == Block::Torch) {
        skyField[x][y][z] = skylight[x][y][z];
        lampField[x][y][z] = 1.0f;
    }
    else {
        skyField[x][y][z] = skylight[x][y][z];
        lampField[x][y][z] = 0.0f;
    }
}

static const int kDir[6][3] = {
    { 1, 0, 0 }, { -1, 0, 0 },
    { 0, 1, 0 }, { 0, -1, 0 },
    { 0, 0, 1 }, { 0, 0, -1 }
};

static void floodBox(int x0, int y0, int z0, int x1, int y1, int z1, int passes) {
    x0 = std::max(0, x0); y0 = std::max(0, y0); z0 = std::max(0, z0);
    x1 = std::min(GRID - 1, x1); y1 = std::min(HEIGHT - 1, y1); z1 = std::min(GRID - 1, z1);

    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                seedCell(x, y, z);

    const float skyDecay = 0.58f;
    const float lampDecay = 0.64f;

    for (int pass = 0; pass < passes; ++pass) {
        for (int x = x0; x <= x1; ++x) {
            for (int y = y0; y <= y1; ++y) {
                for (int z = z0; z <= z1; ++z) {
                    Block here = blocks[x][y][z];
                    if (blockInfo(here).blocksSky && here != Block::Lava) continue;
                    float bestSky = skyField[x][y][z];
                    float bestLamp = lampField[x][y][z];
                    for (int d = 0; d < 6; ++d) {
                        int nx = x + kDir[d][0], ny = y + kDir[d][1], nz = z + kDir[d][2];
                        if (!inBounds(nx, ny, nz)) continue;
                        Block nb = blocks[nx][ny][nz];
                        if (blockInfo(nb).blocksSky && nb != Block::Lava) continue;
                        float inSky = skyField[nx][ny][nz] * skyDecay;
                        float inLamp = lampField[nx][ny][nz] * lampDecay;
                        if (blocks[nx][ny][nz] == Block::Water) {
                            inSky *= 0.85f;
                            inLamp *= 0.9f;
                        }
                        if (inSky > bestSky) bestSky = inSky;
                        if (inLamp > bestLamp) bestLamp = inLamp;
                    }
                    if (here == Block::Torch || here == Block::Lava)
                        bestLamp = std::max(bestLamp, 1.0f);
                    skyField[x][y][z] = bestSky;
                    lampField[x][y][z] = bestLamp;
                }
            }
        }
    }
}

static void relightRegion(int x, int y, int z) {
    const int r = 14;
    int x0 = std::max(0, x - r), x1 = std::min(GRID - 1, x + r);
    int z0 = std::max(0, z - r), z1 = std::min(GRID - 1, z + r);
    for (int ix = x0; ix <= x1; ++ix)
        for (int iz = z0; iz <= z1; ++iz)
            verticalColumn(ix, iz);
    floodBox(x0, std::max(0, y - 10), z0, x1, std::min(HEIGHT - 1, y + 10), z1, 18);
}

void recomputeAllLighting() {
    for (int x = 0; x < GRID; ++x)
        for (int z = 0; z < GRID; ++z)
            verticalColumn(x, z);
    floodBox(0, 0, 0, GRID - 1, 24, GRID - 1, 6);
}

static unsigned mixHash(int x, int z) {
    unsigned n = (unsigned)x * 374761393u + (unsigned)z * 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return n ^ (n >> 16);
}

static void growTree(int x, int z) {
    const int ground = 8;
    if (x < 2 || z < 2 || x >= GRID - 2 || z >= GRID - 2) return;
    if (blocks[x][ground][z] != Block::Grass) return;
    int trunk = 4 + (int)(mixHash(x, z) % 2);
    for (int i = 0; i < trunk; ++i)
        blocks[x][ground + 1 + i][z] = Block::Log;
    int top = ground + trunk;
    for (int dy = -2; dy <= 1; ++dy) {
        int rad = dy == 1 ? 1 : 2;
        for (int dx = -rad; dx <= rad; ++dx) {
            for (int dz = -rad; dz <= rad; ++dz) {
                if (dx == 0 && dz == 0 && dy <= 0) continue;
                if (std::abs(dx) == rad && std::abs(dz) == rad && (mixHash(x + dx, z + dz) % 3) == 0) continue;
                int lx = x + dx, ly = top + dy, lz = z + dz;
                if (!inBounds(lx, ly, lz) || blocks[lx][ly][lz] != Block::Air) continue;
                blocks[lx][ly][lz] = Block::Leaves;
            }
        }
    }
}

void initializeWorld() {
    const int stoneTop = 4;
    const int dirtTop = 8;
    const int surface = 8;
    for (int x = 0; x < GRID; ++x) {
        for (int z = 0; z < GRID; ++z) {
            for (int y = 0; y < stoneTop; ++y)
                blocks[x][y][z] = Block::Stone;
            for (int y = stoneTop; y < dirtTop; ++y)
                blocks[x][y][z] = Block::Dirt;
            blocks[x][surface][z] = Block::Grass;
        }
    }
    for (int x = 6; x < GRID - 6; x += 13) {
        for (int z = 6; z < GRID - 6; z += 13) {
            unsigned h = mixHash(x, z);
            growTree(x + (int)(h % 5) - 2, z + (int)((h >> 4) % 5) - 2);
        }
    }
    for (int x = 0; x < GRID; ++x) {
        for (int z = 0; z < GRID; ++z) {
            if (blocks[x][surface][z] != Block::Grass || blocks[x][surface + 1][z] != Block::Air) continue;
            unsigned h = mixHash(x + 19, z + 3);
            if (h % 4 == 0) blocks[x][surface + 1][z] = Block::TallGrass;
            else if (h % 17 == 0) blocks[x][surface + 1][z] = Block::Poppy;
            else if (h % 19 == 0) blocks[x][surface + 1][z] = Block::Triteleia;
            else if (h % 23 == 0) blocks[x][surface + 1][z] = Block::TriteleiaRed;
        }
    }
    recomputeAllLighting();
}

static bool gReacting = false;

static void resolveLavaAt(int x, int y, int z) {
    if (blockAt(x, y, z) != Block::Lava) return;
    const bool above = blockAt(x, y + 1, z) == Block::Water;
    const bool side =
        blockAt(x + 1, y, z) == Block::Water || blockAt(x - 1, y, z) == Block::Water ||
        blockAt(x, y, z + 1) == Block::Water || blockAt(x, y, z - 1) == Block::Water ||
        blockAt(x, y - 1, z) == Block::Water;
    if (!above && !side) return;
    gReacting = true;
    setBlock(x, y, z, above ? Block::Stone : Block::Obsidian);
    gReacting = false;
}

static void resolveFluidContact(int x, int y, int z) {
    if (gReacting) return;
    resolveLavaAt(x, y, z);
    resolveLavaAt(x + 1, y, z);
    resolveLavaAt(x - 1, y, z);
    resolveLavaAt(x, y + 1, z);
    resolveLavaAt(x, y - 1, z);
    resolveLavaAt(x, y, z + 1);
    resolveLavaAt(x, y, z - 1);
}

static void rememberWater(int x, int y, int z) {
    waterCells.push_back({ x, y, z });
}

static int gFallPhase = 0;

static bool canEnterWater(int x, int y, int z) {
    if (!inBounds(x, y, z) || waterSource[x][y][z]) return false;
    Block b = blocks[x][y][z];
    return b == Block::Air || b == Block::Torch || isPlant(b);
}

static void eraseWater(int x, int y, int z) {
    blocks[x][y][z] = Block::Air;
    waterSource[x][y][z] = false;
    waterLevel[x][y][z] = 255;
    waterFill[x][y][z] = 0;
    gMeta[x][y][z] = 0;
}

static bool erasePlant(int x, int y, int z) {
    if (!inBounds(x, y, z) || !isPlant(blocks[x][y][z])) return false;
    blocks[x][y][z] = Block::Air;
    gMeta[x][y][z] = 0;
    return true;
}

static bool erasePlantsNear(int x, int y, int z) {
    bool any = false;
    const int dir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int i = 0; i < 4; ++i) {
        if (erasePlant(x + dir[i][0], y, z + dir[i][1])) any = true;
        if (erasePlant(x + dir[i][0], y + 1, z + dir[i][1])) any = true;
    }
    if (erasePlant(x, y + 1, z)) any = true;
    return any;
}

static void putSource(int x, int y, int z) {
    blocks[x][y][z] = Block::Water;
    waterSource[x][y][z] = true;
    waterLevel[x][y][z] = 0;
    waterFill[x][y][z] = 255.0f;
    gMeta[x][y][z] = 0;
    rememberWater(x, y, z);
    resolveFluidContact(x, y, z);
}

static bool placeFlow(int x, int y, int z, int lv);
static float addFillTo(int x, int y, int z, float amount, int lv, bool& changed);

static int surfaceY(int x, int y, int z) {
    while (y + 1 < HEIGHT && blocks[x][y + 1][z] == Block::Water) ++y;
    return y;
}

static int receiveY(int x, int z, int fromY) {
    if (!inBounds(x, fromY, z)) return -1;
    Block b = blocks[x][fromY][z];
    if (b == Block::Air || b == Block::Torch || isPlant(b)) return fromY;
    if (b == Block::Water) {
        int top = surfaceY(x, fromY, z);
        if (top + 1 <= fromY && canEnterWater(x, top + 1, z)) return top + 1;
    }
    return -1;
}

static int addVolume(int x, int y, int z) {
    if (!inBounds(x, y, z) || blocks[x][y][z] != Block::Water) return y;
    int top = surfaceY(x, y, z);
    int bestX = x, bestZ = z, bestY = HEIGHT;
    const int dir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int i = 0; i < 4; ++i) {
        int ry = receiveY(x + dir[i][0], z + dir[i][1], top);
        if (ry >= 0 && ry < bestY) {
            bestY = ry;
            bestX = x + dir[i][0];
            bestZ = z + dir[i][1];
        }
    }
    bool changed = false;
    if (bestY <= top) {
        addFillTo(bestX, bestY, bestZ, 10.0f, 1, changed);
        erasePlantsNear(bestX, bestY, bestZ);
        return bestY;
    }
    float extra = addFillTo(x, top, z, 10.0f, 1, changed);
    if (extra > 0) return top;
    if (top + 1 < HEIGHT && canEnterWater(x, top + 1, z)) {
        addFillTo(x, top + 1, z, 10.0f, 1, changed);
        erasePlantsNear(x, top + 1, z);
        return top + 1;
    }
    return top;
}

static bool placeFlow(int x, int y, int z, int lv) {
    if (!canEnterWater(x, y, z)) return false;
    blocks[x][y][z] = Block::Water;
    waterSource[x][y][z] = false;
    waterLevel[x][y][z] = (unsigned char)lv;
    waterFill[x][y][z] = 6.0f;
    gMeta[x][y][z] = 0;
    rememberWater(x, y, z);
    resolveFluidContact(x, y, z);
    return true;
}

static float fillOf(int x, int y, int z) {
    if (!inBounds(x, y, z) || blocks[x][y][z] != Block::Water) return 0.0f;
    if (waterSource[x][y][z]) return 255.0f;
    return waterFill[x][y][z];
}

static float addFillTo(int x, int y, int z, float amount, int lv, bool& changed) {
    if (amount <= 0.0f || !inBounds(x, y, z) || solid(x, y, z)) return 0.0f;
    if (blocks[x][y][z] != Block::Water) {
        if (!canEnterWater(x, y, z)) return 0.0f;
        if (!placeFlow(x, y, z, lv)) return 0.0f;
        changed = true;
        waterFill[x][y][z] = 0.0f;
    }
    if (waterSource[x][y][z]) return 0.0f;
    float room = 255.0f - waterFill[x][y][z];
    if (room <= 0.0f) return 0.0f;
    if (amount > room) amount = room;
    waterFill[x][y][z] += amount;
    return amount;
}

float waterFill01(int x, int y, int z) {
    if (!inBounds(x, y, z) || blocks[x][y][z] != Block::Water) return 0.0f;
    if (waterSource[x][y][z]) return 1.0f;
    return waterFill[x][y][z] * (1.0f / 255.0f);
}

static bool waterSupported(int x, int y, int z) {
    if (!inBounds(x, y, z) || blocks[x][y][z] != Block::Water) return false;
    if (waterSource[x][y][z]) return true;
    int lv = waterLevel[x][y][z];
    if (waterLevelAt(x, y + 1, z) >= 0) return true;
    if (lv <= 0) return false;
    const int dir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int i = 0; i < 4; ++i) {
        int nx = x + dir[i][0], nz = z + dir[i][1];
        int nl = waterLevelAt(nx, y, nz);
        if (nl >= 0 && nl < lv) return true;
    }
    return false;
}

static void stepWater(bool& changed, int& minX, int& minY, int& minZ, int& maxX, int& maxY, int& maxZ, float dt) {
    auto touch = [&](int x, int y, int z) {
        changed = true;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
        if (z < minZ) minZ = z;
        if (z > maxZ) maxZ = z;
    };

    const float rateDown = 110.0f * dt;
    const float rateSide = 38.0f * dt;
    const float rateSrc = 52.0f * dt;

    const int dir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    std::vector<WaterCell> snapshot = waterCells;
    static float splashWait = 0.0f;
    splashWait += dt;

    auto pourDown = [&](int x, int y, int z, float amount, int lv) {
        if (y <= 0 || amount <= 0.0f) return;
        int dest = y - 1;
        while (dest > 0 && canEnterWater(x, dest, z)) --dest;
        int land = dest;
        if (canEnterWater(x, dest, z)) land = dest;
        else if (blockAt(x, dest, z) == Block::Water) {
            land = surfaceY(x, dest, z);
            if (fillOf(x, land, z) >= 254.5f) {
                if (land + 1 < HEIGHT && !solid(x, land + 1, z)
                    && (canEnterWater(x, land + 1, z) || blockAt(x, land + 1, z) == Block::Water))
                    land = land + 1;
            }
        }
        else {
            land = dest + 1;
            if (land >= y || solid(x, land, z) || !canEnterWater(x, land, z)) return;
        }
        if (solid(x, land, z)) return;
        float got = addFillTo(x, land, z, amount, lv < 7 ? lv + 1 : 7, changed);
        if (got > 0.0f) {
            erasePlantsNear(x, land, z);
            touch(x, land, z);
            float yBot = (float)land + waterFill01(x, land, z);
            fluidAddFall((float)x + 0.5f, (float)y + 0.15f, yBot, (float)z + 0.5f, got / 8.0f);
            if (splashWait > 0.45f && blockAt(x, land, z) == Block::Water && fillOf(x, land, z) > 20.0f) {
                float fall = (float)(y - land);
                if (fall < 1.0f) fall = 1.0f;
                fluidImpact((float)x + 0.5f, yBot, (float)z + 0.5f, std::sqrt(2.0f * 22.0f * fall) * 0.45f, 8.0f + fall);
                splashWait = 0.0f;
            }
        }
    };

    for (const WaterCell& c : snapshot) {
        if (!inBounds(c.x, c.y, c.z) || blocks[c.x][c.y][c.z] != Block::Water) continue;
        if (erasePlantsNear(c.x, c.y, c.z))
            touch(c.x, c.y, c.z);
        bool source = waterSource[c.x][c.y][c.z];
        int lv = source ? 0 : (int)waterLevel[c.x][c.y][c.z];
        float have = fillOf(c.x, c.y, c.z);

        bool openBelow = c.y > 0 && canEnterWater(c.x, c.y - 1, c.z);
        bool waterBelow = c.y > 0 && blocks[c.x][c.y - 1][c.z] == Block::Water && fillOf(c.x, c.y - 1, c.z) < 254.5f;

        if (openBelow || waterBelow) {
            float give = source ? rateSrc : (have < rateDown ? have : rateDown);
            if (!source && give > 0.0f) {
                waterFill[c.x][c.y][c.z] = have - give;
                if (waterFill[c.x][c.y][c.z] <= 0.02f) {
                    eraseWater(c.x, c.y, c.z);
                    touch(c.x, c.y, c.z);
                }
            }
            pourDown(c.x, c.y, c.z, give, lv);
            continue;
        }

        if (have < 4.0f && !source) continue;

        int bestX = -1, bestZ = -1;
        float bestFill = 256.0f;
        for (int i = 0; i < 4; ++i) {
            int nx = c.x + dir[i][0], nz = c.z + dir[i][1];
            if (!inBounds(nx, c.y, nz)) continue;
            if (solid(nx, c.y, nz)) continue;
            Block nb = blocks[nx][c.y][nz];
            if (nb != Block::Water && !canEnterWater(nx, c.y, nz)) continue;
            float nf = fillOf(nx, c.y, nz);
            if (nb == Block::Air || nb == Block::Torch || isPlant(nb)) nf = 0.0f;
            if (have - nf < 10.0f) continue;
            if (nf < bestFill) {
                bestFill = nf;
                bestX = nx;
                bestZ = nz;
            }
        }
        if (bestX >= 0) {
            float give = source ? rateSrc : (have - bestFill) * 0.22f;
            if (give > rateSide) give = rateSide;
            if (!source) {
                if (give > have - 3.0f) give = have - 3.0f;
                if (give > 0.0f) {
                    waterFill[c.x][c.y][c.z] = have - give;
                    have -= give;
                }
            }
            if (give > 0.0f) {
                addFillTo(bestX, c.y, bestZ, give, lv < 7 ? lv + 1 : 7, changed);
                erasePlantsNear(bestX, c.y, bestZ);
                touch(bestX, c.y, bestZ);
            }
        }
        else if (have >= 252.0f && c.y + 1 < HEIGHT && !solid(c.x, c.y + 1, c.z)
            && (canEnterWater(c.x, c.y + 1, c.z) || blockAt(c.x, c.y + 1, c.z) == Block::Water)) {
            float give = source ? rateSrc : rateSide;
            if (!source && give > have - 220.0f) give = have - 220.0f;
            if (give > 0.0f) {
                if (!source) waterFill[c.x][c.y][c.z] = have - give;
                addFillTo(c.x, c.y + 1, c.z, give, lv, changed);
                erasePlantsNear(c.x, c.y + 1, c.z);
                touch(c.x, c.y + 1, c.z);
            }
        }
    }

    size_t w = 0;
    for (size_t i = 0; i < waterCells.size(); ++i) {
        WaterCell c = waterCells[i];
        if (inBounds(c.x, c.y, c.z) && blocks[c.x][c.y][c.z] == Block::Water)
            waterCells[w++] = c;
    }
    waterCells.resize(w);
}

void waterImpact(int x, int y, int z, float vy, float mass) {
    if (!inBounds(x, y, z)) return;
    int sy = y;
    if (blocks[x][y][z] != Block::Water) {
        for (int i = 1; i <= 3; ++i) {
            if (inBounds(x, y - i, z) && blocks[x][y - i][z] == Block::Water) {
                sy = y - i;
                break;
            }
        }
    }
    if (blocks[x][sy][z] != Block::Water) return;
    sy = surfaceY(x, sy, z);
    float speed = vy < 0.0f ? -vy : vy;
    if (mass > 40.0f && speed > 12.0f)
        addVolume(x, sy, z);
    fluidImpact((float)x + 0.5f, (float)sy + 0.95f, (float)z + 0.5f, speed, mass);
    relightRegion(x, sy, z);
    markMeshArea(x - 6, z - 6, x + 6, z + 6);
}

void tickWater(float dt) {
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;
    fluidClearFalls();
    if (waterCells.empty()) {
        refreshWaterHeights();
        return;
    }

    bool changed = false;
    int minX = GRID, minY = HEIGHT, minZ = GRID;
    int maxX = 0, maxY = 0, maxZ = 0;
    stepWater(changed, minX, minY, minZ, maxX, maxY, maxZ, dt);
    refreshWaterHeights();
    if (!changed || minX > maxX || minY > maxY || minZ > maxZ) return;

    for (int x = minX; x <= maxX; ++x)
        for (int z = minZ; z <= maxZ; ++z)
            verticalColumn(x, z);
    floodBox(minX - 6, std::max(0, minY - 4), minZ - 6, maxX + 6, std::min(HEIGHT - 1, maxY + 6), maxZ + 6, 12);
    markMeshArea(minX - 16, minZ - 16, maxX + 16, maxZ + 16);
}

void refreshWaterHeights() {
    for (int i = 0; i < GRID * GRID; ++i) waterHeightXZ[i] = 0.0f;
    for (const WaterCell& c : waterCells) {
        if (!inBounds(c.x, c.y, c.z) || blocks[c.x][c.y][c.z] != Block::Water) continue;
        float h = (float)c.y + waterFill01(c.x, c.y, c.z);
        int i = c.z * GRID + c.x;
        if (h > waterHeightXZ[i]) waterHeightXZ[i] = h;
    }
}

float waterSurfaceY(int x, int z) {
    if (x < 0 || z < 0 || x >= GRID || z >= GRID) return 0.0f;
    return waterHeightXZ[z * GRID + x];
}

int waterCellCount() { return (int)waterCells.size(); }

void getWaterCell(int i, int& x, int& y, int& z) {
    if (i < 0 || i >= (int)waterCells.size()) { x = y = z = 0; return; }
    x = waterCells[i].x;
    y = waterCells[i].y;
    z = waterCells[i].z;
}

int blockMeta(int x, int y, int z) {
    if (!inBounds(x, y, z)) return 0;
    return gMeta[x][y][z];
}

static void releaseDecorations(int x, int y, int z) {
    Block above = blockAt(x, y + 1, z);
    if (isPlant(above) || above == Block::Torch)
        setBlock(x, y + 1, z, Block::Air);
    auto wall = [](int nx, int ny, int nz, int meta) {
        if (blockAt(nx, ny, nz) == Block::Torch && blockMeta(nx, ny, nz) == meta)
            setBlock(nx, ny, nz, Block::Air);
    };
    wall(x + 1, y, z, 1);
    wall(x - 1, y, z, 2);
    wall(x, y, z + 1, 3);
    wall(x, y, z - 1, 4);
}

void setBlock(int x, int y, int z, Block value, int meta) {
    if (!inBounds(x, y, z)) return;
    Block old = blocks[x][y][z];
    if (value != Block::Water)
        waterSource[x][y][z] = false;
    blocks[x][y][z] = value;
    gMeta[x][y][z] = (value == Block::Torch || value == Block::Log) ? (unsigned char)meta : 0;
    if (value == Block::Water) {
        waterSource[x][y][z] = true;
        waterLevel[x][y][z] = 0;
        waterFill[x][y][z] = 255.0f;
        rememberWater(x, y, z);
        erasePlantsNear(x, y, z);
    }
    else {
        waterLevel[x][y][z] = 255;
    }
    if (blockInfo(old).solid && !blockInfo(value).solid)
        releaseDecorations(x, y, z);
    relightRegion(x, y, z);
    markMeshArea(x - 16, z - 16, x + 16, z + 16);
    if (!gReacting)
        resolveFluidContact(x, y, z);
}

int waterLevelAt(int x, int y, int z) {
    if (!inBounds(x, y, z) || blocks[x][y][z] != Block::Water) return -1;
    return waterLevel[x][y][z];
}

void waterFlow(int x, int y, int z, float& fx, float& fz, float& down) {
    fx = fz = down = 0.0f;
    int lv = waterLevelAt(x, y, z);
    if (lv < 0) return;

    if (y > 0 && !solid(x, y - 1, z))
        down = 1.0f;

    const int dir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    bool drain = false;
    for (int i = 0; i < 4; ++i) {
        int nx = x + dir[i][0], nz = z + dir[i][1];
        if (waterLevelAt(nx, y, nz) < 0) continue;
        if (y > 0 && !solid(nx, y - 1, nz)) {
            fx += (float)dir[i][0];
            fz += (float)dir[i][1];
            drain = true;
        }
    }
    if (!drain) {
        for (int i = 0; i < 4; ++i) {
            int nl = waterLevelAt(x + dir[i][0], y, z + dir[i][1]);
            if (nl > lv) {
                fx += (float)dir[i][0];
                fz += (float)dir[i][1];
            }
        }
    }
    float len = std::sqrt(fx * fx + fz * fz);
    if (len > 0.001f) {
        fx /= len;
        fz /= len;
    }
}

static float fieldAt(Field& field, int x, int y, int z) {
    if (!inBounds(x, y, z)) return 0.0f;
    return field[x][y][z];
}

static float sampleField(Field& field, int bx, int by, int bz, float nx, float ny, float nz, bool skyOpen) {
    int sx = bx + (nx > 0.0f ? 1 : (nx < 0.0f ? -1 : 0));
    int sy = by + (ny > 0.0f ? 1 : (ny < 0.0f ? -1 : 0));
    int sz = bz + (nz > 0.0f ? 1 : (nz < 0.0f ? -1 : 0));

    if (!inBounds(sx, sy, sz)) {
        if (skyOpen && ny > 0.0f) return 1.0f;
        return 0.0f;
    }
    if (solid(sx, sy, sz))
        return 0.0f;
    return fieldAt(field, sx, sy, sz);
}

float propagatedLightAt(int x, int y, int z) {
    return fieldAt(skyField, x, y, z);
}

void cellLight(int x, int y, int z, float& sky, float& lamp) {
    sky = 0.0f;
    lamp = 0.0f;
    if (!inBounds(x, y, z)) return;
    sky = skyField[x][y][z];
    lamp = lampField[x][y][z];
    for (int i = 1; i <= 5; ++i) {
        Block above = blockAt(x, y + i, z);
        if (above == Block::Leaves) sky *= 0.72f;
        else if (blockInfo(above).blocksSky) break;
    }
}

static bool gMeshDirty = true;
static int gMeshX0 = 0, gMeshZ0 = 0, gMeshX1 = GRID - 1, gMeshZ1 = GRID - 1;

void markMeshArea(int x0, int z0, int x1, int z1) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (z0 > z1) { int t = z0; z0 = z1; z1 = t; }
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 >= GRID) x1 = GRID - 1;
    if (z1 >= GRID) z1 = GRID - 1;
    if (!gMeshDirty) {
        gMeshX0 = x0; gMeshZ0 = z0; gMeshX1 = x1; gMeshZ1 = z1;
        gMeshDirty = true;
        return;
    }
    if (x0 < gMeshX0) gMeshX0 = x0;
    if (z0 < gMeshZ0) gMeshZ0 = z0;
    if (x1 > gMeshX1) gMeshX1 = x1;
    if (z1 > gMeshZ1) gMeshZ1 = z1;
}

bool consumeMeshDirty(int& x0, int& z0, int& x1, int& z1) {
    if (!gMeshDirty) return false;
    x0 = gMeshX0; z0 = gMeshZ0; x1 = gMeshX1; z1 = gMeshZ1;
    gMeshDirty = false;
    gMeshX0 = GRID; gMeshZ0 = GRID; gMeshX1 = 0; gMeshZ1 = 0;
    return true;
}

float sampleSkyForFace(int bx, int by, int bz, float nx, float ny, float nz) {
    return sampleField(skyField, bx, by, bz, nx, ny, nz, true);
}

float sampleLampForFace(int bx, int by, int bz, float nx, float ny, float nz) {
    return sampleField(lampField, bx, by, bz, nx, ny, nz, false);
}

float causticAmount(int x, int y, int z) {
    for (int i = 1; i <= 5; ++i) {
        Block b = blockAt(x, y + i, z);
        if (b == Block::Water) return 1.0f / (float)i;
        if (blockInfo(b).blocksSky) return 0.0f;
    }
    return 0.0f;
}
