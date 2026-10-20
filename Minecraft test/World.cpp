#include "World.h"

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
static unsigned char gMeta[GRID][HEIGHT][GRID] = {};

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

static bool placeFlow(int x, int y, int z, int lv) {
    if (!inBounds(x, y, z) || waterSource[x][y][z]) return false;
    if (blocks[x][y][z] != Block::Air) return false;
    blocks[x][y][z] = Block::Water;
    waterLevel[x][y][z] = (unsigned char)lv;
    rememberWater(x, y, z);
    resolveFluidContact(x, y, z);
    return true;
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

static void stepWater(bool& changed, int& minX, int& minY, int& minZ, int& maxX, int& maxY, int& maxZ) {
    auto touch = [&](int x, int y, int z) {
        changed = true;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
        if (z < minZ) minZ = z;
        if (z > maxZ) maxZ = z;
    };

    std::vector<WaterCell> snapshot = waterCells;
    for (const WaterCell& c : snapshot) {
        if (waterSupported(c.x, c.y, c.z)) continue;
        if (blocks[c.x][c.y][c.z] != Block::Water || waterSource[c.x][c.y][c.z]) continue;
        blocks[c.x][c.y][c.z] = Block::Air;
        waterLevel[c.x][c.y][c.z] = 255;
        touch(c.x, c.y, c.z);
    }

    for (const WaterCell& c : snapshot) {
        if (!inBounds(c.x, c.y, c.z) || blocks[c.x][c.y][c.z] != Block::Water) continue;
        if (!waterSource[c.x][c.y][c.z] && !waterSupported(c.x, c.y, c.z)) continue;
        int lv = (int)waterLevel[c.x][c.y][c.z];
        bool blockedBelow = (c.y == 0) || solid(c.x, c.y - 1, c.z);
        if (!blockedBelow && placeFlow(c.x, c.y - 1, c.z, 0))
            touch(c.x, c.y - 1, c.z);
        if ((blockedBelow || waterSource[c.x][c.y][c.z]) && lv < 7) {
            const int dir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (int i = 0; i < 4; ++i) {
                int nx = c.x + dir[i][0];
                int nz = c.z + dir[i][1];
                if (placeFlow(nx, c.y, nz, lv + 1))
                    touch(nx, c.y, nz);
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

void tickWater(float dt) {
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;
    waterClock += dt;
    if (waterClock < 0.16f || waterCells.empty()) return;
    waterClock = 0.0f;

    bool changed = false;
    int minX = GRID, minY = HEIGHT, minZ = GRID;
    int maxX = 0, maxY = 0, maxZ = 0;
    stepWater(changed, minX, minY, minZ, maxX, maxY, maxZ);
    if (!changed || minX > maxX || minY > maxY || minZ > maxZ) return;

    for (int x = minX; x <= maxX; ++x)
        for (int z = minZ; z <= maxZ; ++z)
            verticalColumn(x, z);
    floodBox(minX - 6, std::max(0, minY - 4), minZ - 6, maxX + 6, std::min(HEIGHT - 1, maxY + 6), maxZ + 6, 12);
    markMeshArea(minX - 16, minZ - 16, maxX + 16, maxZ + 16);
}

int blockMeta(int x, int y, int z) {
    if (!inBounds(x, y, z)) return 0;
    return gMeta[x][y][z];
}

void setBlock(int x, int y, int z, Block value, int meta) {
    if (!inBounds(x, y, z)) return;
    if (value != Block::Water)
        waterSource[x][y][z] = false;
    blocks[x][y][z] = value;
    gMeta[x][y][z] = (value == Block::Torch) ? (unsigned char)meta : 0;
    if (value == Block::Water) {
        waterSource[x][y][z] = true;
        waterLevel[x][y][z] = 0;
        rememberWater(x, y, z);
    }
    else {
        waterLevel[x][y][z] = 255;
    }
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
