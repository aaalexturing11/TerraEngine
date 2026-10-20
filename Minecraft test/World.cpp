#include "World.h"
#include "FluidSolver.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <vector>

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

static int divFloor(int a, int b) {
    int q = a / b;
    int r = a % b;
    if (r != 0 && ((a < 0) != (b < 0))) --q;
    return q;
}

static int modFloor(int a, int b) {
    int r = a % b;
    if (r < 0) r += b;
    return r;
}

static unsigned long long packChunk(int cx, int cz) {
    return ((unsigned long long)(unsigned)cx << 32) | (unsigned)cz;
}

static int cidx(int lx, int y, int lz) {
    return (lx * CHUNK + lz) * HEIGHT + y;
}

struct Chunk {
    int cx = 0, cz = 0;
    bool edited = false;
    Block vol[CHUNK * CHUNK * HEIGHT];
    unsigned char meta[CHUNK * CHUNK * HEIGHT];
    unsigned char sky[CHUNK * CHUNK * HEIGHT];
    unsigned char lamp[CHUNK * CHUNK * HEIGHT];
    unsigned char src[CHUNK * CHUNK * HEIGHT];
    unsigned char lvl[CHUNK * CHUNK * HEIGHT];
    unsigned char fill[CHUNK * CHUNK * HEIGHT];
    float surf[CHUNK * CHUNK];
    float surfTmp[CHUNK * CHUNK];
    unsigned char mark[CHUNK * CHUNK];
    unsigned char vis0[CHUNK * CHUNK];
    unsigned char vis1[CHUNK * CHUNK];
    bool surfDirty = true;
};

static std::unordered_map<unsigned long long, Chunk*> gChunks;
static std::vector<Chunk*> gLoaded;
static bool gReacting = false;

static Chunk* findChunk(int cx, int cz) {
    auto it = gChunks.find(packChunk(cx, cz));
    return it == gChunks.end() ? nullptr : it->second;
}

static Chunk* gChunkHint = nullptr;

static Chunk* chunkOf(int x, int z) {
    int cx = divFloor(x, CHUNK);
    int cz = divFloor(z, CHUNK);
    if (gChunkHint && gChunkHint->cx == cx && gChunkHint->cz == cz) return gChunkHint;
    Chunk* c = findChunk(cx, cz);
    if (c) gChunkHint = c;
    return c;
}

static bool yOk(int y) { return y >= 0 && y < HEIGHT; }

static Block getB(int x, int y, int z) {
    if (!yOk(y)) return Block::Air;
    Chunk* c = chunkOf(x, z);
    if (!c) return Block::Air;
    return c->vol[cidx(modFloor(x, CHUNK), y, modFloor(z, CHUNK))];
}

static void setVol(Chunk* c, int lx, int y, int lz, Block b) {
    int i = cidx(lx, y, lz);
    c->vol[i] = b;
    if (b == Block::Water) {
        c->fill[i] = 255;
        c->lvl[i] = 0;
        c->src[i] = 0;
    }
    else {
        c->fill[i] = 0;
        c->lvl[i] = 255;
        c->src[i] = 0;
    }
}

static unsigned char& refMeta(int x, int y, int z) { return chunkOf(x, z)->meta[cidx(modFloor(x, CHUNK), y, modFloor(z, CHUNK))]; }
static unsigned char& refSrc(int x, int y, int z) { return chunkOf(x, z)->src[cidx(modFloor(x, CHUNK), y, modFloor(z, CHUNK))]; }
static unsigned char& refLvl(int x, int y, int z) { return chunkOf(x, z)->lvl[cidx(modFloor(x, CHUNK), y, modFloor(z, CHUNK))]; }
static unsigned char& refFill(int x, int y, int z) { return chunkOf(x, z)->fill[cidx(modFloor(x, CHUNK), y, modFloor(z, CHUNK))]; }
static unsigned char& refSky(int x, int y, int z) { return chunkOf(x, z)->sky[cidx(modFloor(x, CHUNK), y, modFloor(z, CHUNK))]; }
static unsigned char& refLamp(int x, int y, int z) { return chunkOf(x, z)->lamp[cidx(modFloor(x, CHUNK), y, modFloor(z, CHUNK))]; }

bool inBounds(int x, int y, int z) {
    return yOk(y) && chunkOf(x, z) != nullptr;
}

Block blockAt(int x, int y, int z) { return getB(x, y, z); }
bool occupied(int x, int y, int z) { return getB(x, y, z) != Block::Air; }
bool solid(int x, int y, int z) { return blockInfo(getB(x, y, z)).solid; }

int loadedChunkCount() { return (int)gLoaded.size(); }

void loadedChunkCoord(int i, int& cx, int& cz) {
    if (i < 0 || i >= (int)gLoaded.size()) { cx = cz = 0; return; }
    cx = gLoaded[i]->cx;
    cz = gLoaded[i]->cz;
}

void worldColumnSpan(int x, int z, int& y0, int& y1) {
    Chunk* c = chunkOf(x, z);
    if (!c) { y0 = 0; y1 = -1; return; }
    int i = modFloor(z, CHUNK) * CHUNK + modFloor(x, CHUNK);
    y0 = c->vis0[i];
    y1 = c->vis1[i];
}

static unsigned gWorldSeed = 1u;
static float gOffX = 0.0f, gOffZ = 0.0f, gFreq = 1.0f, gRotC = 1.0f, gRotS = 0.0f;

void worldSetSeed(unsigned seed) {
    if (seed == 0) seed = 1;
    gWorldSeed = seed;
    gOffX = (float)(seed & 65535u) * 4.17f + (float)((seed >> 16) & 255u) * 80.0f;
    gOffZ = (float)((seed >> 7) & 65535u) * 3.91f + (float)(seed & 255u) * 60.0f;
    gFreq = 0.82f + (float)((seed >> 3) % 7u) * 0.06f;
    float ang = (float)(seed % 6283u) * 0.001f;
    gRotC = std::cos(ang);
    gRotS = std::sin(ang);
}

unsigned worldGetSeed() { return gWorldSeed; }

static unsigned ss(unsigned s) { return s ^ (gWorldSeed * 2246822519u + 3266489917u); }

static unsigned mixHash(int x, int z, unsigned s = 0) {
    unsigned n = (unsigned)x * 374761393u + (unsigned)z * 668265263u + ss(s) * 1442695041u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return n ^ (n >> 16);
}

static float hash01(int x, int z, unsigned s) {
    return (float)(mixHash(x, z, s) & 0xFFFFFFu) * (1.0f / 16777215.0f);
}

static float fade(float t) { return t * t * (3.0f - 2.0f * t); }

static float valueNoise(float x, float z, unsigned s) {
    int x0 = (int)std::floor(x), z0 = (int)std::floor(z);
    float tx = fade(x - (float)x0), tz = fade(z - (float)z0);
    float a = hash01(x0, z0, s), b = hash01(x0 + 1, z0, s);
    float c = hash01(x0, z0 + 1, s), d = hash01(x0 + 1, z0 + 1, s);
    return (a * (1.0f - tx) + b * tx) * (1.0f - tz) + (c * (1.0f - tx) + d * tx) * tz;
}

static float fbm(float x, float z, unsigned s, int oct) {
    float acc = 0.0f, amp = 1.0f, nrm = 0.0f;
    for (int i = 0; i < oct; ++i) {
        acc += (valueNoise(x, z, s + (unsigned)i * 101u) * 2.0f - 1.0f) * amp;
        nrm += amp;
        amp *= 0.5f;
        x *= 2.03f;
        z *= 2.03f;
    }
    return acc / nrm;
}

static float hash013(int x, int y, int z, unsigned s) {
    unsigned n = (unsigned)x * 374761393u ^ (unsigned)y * 668265263u ^ (unsigned)z * 1274126177u ^ ss(s) * 1442695041u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return (float)((n ^ (n >> 16)) & 0xFFFFFFu) * (1.0f / 16777215.0f);
}

static float valueNoise3(float x, float y, float z, unsigned s) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y), z0 = (int)std::floor(z);
    float tx = fade(x - (float)x0), ty = fade(y - (float)y0), tz = fade(z - (float)z0);
    float c000 = hash013(x0, y0, z0, s), c100 = hash013(x0 + 1, y0, z0, s);
    float c010 = hash013(x0, y0 + 1, z0, s), c110 = hash013(x0 + 1, y0 + 1, z0, s);
    float c001 = hash013(x0, y0, z0 + 1, s), c101 = hash013(x0 + 1, y0, z0 + 1, s);
    float c011 = hash013(x0, y0 + 1, z0 + 1, s), c111 = hash013(x0 + 1, y0 + 1, z0 + 1, s);
    float x00 = c000 * (1.0f - tx) + c100 * tx;
    float x10 = c010 * (1.0f - tx) + c110 * tx;
    float x01 = c001 * (1.0f - tx) + c101 * tx;
    float x11 = c011 * (1.0f - tx) + c111 * tx;
    float y0v = x00 * (1.0f - ty) + x10 * ty;
    float y1v = x01 * (1.0f - ty) + x11 * ty;
    return y0v * (1.0f - tz) + y1v * tz;
}

static void noisePos(int x, int z, float& ox, float& oz) {
    float sx = (float)x + gOffX;
    float sz = (float)z + gOffZ;
    ox = (sx * gRotC - sz * gRotS) * gFreq;
    oz = (sx * gRotS + sz * gRotC) * gFreq;
}

static float smooth01(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

struct ColGen {
    int ground;
    int waterTo;
    bool ocean, lake, beach, mountain, forest;
};

struct Climate {
    float cont, range, ridge, hill, sea, moist, ox, oz;
};

static Climate climatePoint(int x, int z) {
    Climate c;
    noisePos(x, z, c.ox, c.oz);
    c.cont = fbm(c.ox * 0.00125f, c.oz * 0.00125f, 11, 4);
    c.range = fbm(c.ox * 0.00105f, c.oz * 0.00105f, 29, 3);
    c.ridge = 1.0f - std::abs(fbm(c.ox * 0.0054f, c.oz * 0.0041f, 47, 3));
    c.hill = fbm(c.ox * 0.0088f, c.oz * 0.0088f, 73, 3);
    c.sea = fbm(c.ox * 0.00082f, c.oz * 0.00082f, 211, 3);
    c.moist = fbm(c.ox * 0.0044f + 18.0f, c.oz * 0.0044f, 131, 2);
    return c;
}

static Climate climateAt(int x, int z) {
    int x0 = divFloor(x, 4) * 4;
    int z0 = divFloor(z, 4) * 4;
    struct Slot { int x, z; Climate c; bool ok; };
    static Slot slots[128];
    auto cached = [&](int sx, int sz) {
        unsigned h = ((unsigned)sx * 73856093u) ^ ((unsigned)sz * 19349663u);
        Slot& s = slots[h & 127u];
        if (!s.ok || s.x != sx || s.z != sz) {
            s.ok = true;
            s.x = sx;
            s.z = sz;
            s.c = climatePoint(sx, sz);
        }
        return s.c;
    };
    Climate c00 = cached(x0, z0);
    Climate c10 = cached(x0 + 4, z0);
    Climate c01 = cached(x0, z0 + 4);
    Climate c11 = cached(x0 + 4, z0 + 4);
    float tx = (float)(x - x0) * 0.25f;
    float tz = (float)(z - z0) * 0.25f;
    auto mix4 = [&](float a, float b, float c, float d) {
        float ab = a * (1.0f - tx) + b * tx;
        float cd = c * (1.0f - tx) + d * tx;
        return ab * (1.0f - tz) + cd * tz;
    };
    Climate o;
    o.cont = mix4(c00.cont, c10.cont, c01.cont, c11.cont);
    o.range = mix4(c00.range, c10.range, c01.range, c11.range);
    o.ridge = mix4(c00.ridge, c10.ridge, c01.ridge, c11.ridge);
    o.hill = mix4(c00.hill, c10.hill, c01.hill, c11.hill);
    o.sea = mix4(c00.sea, c10.sea, c01.sea, c11.sea);
    o.moist = mix4(c00.moist, c10.moist, c01.moist, c11.moist);
    o.ox = mix4(c00.ox, c10.ox, c01.ox, c11.ox);
    o.oz = mix4(c00.oz, c10.oz, c01.oz, c11.oz);
    return o;
}

static ColGen columnGen(int x, int z) {
    Climate cl = climateAt(x, z);
    float cont = cl.cont;
    float range = cl.range;
    float ridge = cl.ridge;
    float hill = cl.hill;
    float detail = fbm(cl.ox * 0.031f, cl.oz * 0.031f, 91, 2);
    float sea = cl.sea;
    float moist = cl.moist;
    float ox = cl.ox, oz = cl.oz;
    float mt = smooth01((range - 0.16f) / 0.42f) * smooth01((ridge - 0.58f) / 0.28f);

    ColGen g;
    g.ground = SEA_LEVEL;
    g.waterTo = -1;
    g.ocean = g.lake = g.beach = g.mountain = g.forest = false;

    if (sea < -0.66f && mt < 0.18f) {
        g.ocean = true;
        float depth = (-0.66f - sea) * 18.0f + detail * 1.5f;
        if (depth < 2.0f) depth = 2.0f;
        if (depth > 16.0f) depth = 16.0f;
        g.ground = SEA_LEVEL - (int)depth;
        g.waterTo = SEA_LEVEL;
    }
    else if (sea < -0.50f && mt < 0.22f) {
        g.beach = true;
        g.ground = SEA_LEVEL - 1 + (int)((sea + 0.66f) * 16.0f + detail);
        if (g.ground < SEA_LEVEL) g.waterTo = SEA_LEVEL;
    }
    else {
        float base = 6.0f + hill * 8.0f + detail * 2.2f + cont * 3.0f;
        if (base < 3.0f) base = 3.0f;
        float foot = smooth01((range - 0.02f) / 0.28f) * ridge * 10.0f;
        float peak = mt * mt * (46.0f + ridge * 42.0f + detail * 5.0f);
        g.ground = SEA_LEVEL + (int)(base + foot + peak);
        g.mountain = mt > 0.34f;
        if (!g.mountain && moist > -0.08f) g.forest = true;
    }

    if (!g.ocean && !g.beach && mt < 0.20f) {
        unsigned cell = mixHash(x >> 3, z >> 3, 640);
        if ((int)(cell % 1000) < 36) {
            float bowl = fbm(ox * 0.072f, oz * 0.072f, 640, 2);
            if (bowl < -0.02f) {
                int lip = g.ground;
                int depth = 2 + (int)((cell >> 6) % 2);
                g.ground = lip - depth;
                g.waterTo = lip - 1;
                if (g.waterTo > g.ground) {
                    g.lake = true;
                    g.forest = false;
                    g.mountain = false;
                }
                else g.waterTo = -1;
            }
        }
    }

    if (g.ground < 3) g.ground = 3;
    if (g.ground > HEIGHT - 24) g.ground = HEIGHT - 24;
    if (g.waterTo >= HEIGHT) g.waterTo = HEIGHT - 1;
    return g;
}

static void verticalColumn(int x, int z) {
    Chunk* c = chunkOf(x, z);
    if (!c) return;
    int lx = modFloor(x, CHUNK), lz = modFloor(z, CHUNK);
    float sky = 1.0f;
    for (int y = HEIGHT - 1; y >= 0; --y) {
        int i = cidx(lx, y, lz);
        Block b = c->vol[i];
        if (blockInfo(b).blocksSky) {
            c->sky[i] = 0;
            sky = 0.0f;
        }
        else {
            if (b == Block::Water) sky *= 0.62f;
            else if (b == Block::Leaves) {
                sky *= 0.82f;
                if (sky < 0.46f) sky = 0.46f;
            }
            c->sky[i] = (unsigned char)(sky * 255.0f + 0.5f);
        }
        if (b == Block::Torch || b == Block::Lava) c->lamp[i] = 255;
        else c->lamp[i] = 0;
    }
}

static void seedCell(int x, int y, int z) {
    if (!inBounds(x, y, z)) return;
    Block b = getB(x, y, z);
    if (blockInfo(b).blocksSky && b != Block::Lava) {
        refSky(x, y, z) = 0;
        refLamp(x, y, z) = 0;
    }
    else if (b == Block::Lava) {
        refSky(x, y, z) = 0;
        refLamp(x, y, z) = 255;
    }
    else if (b == Block::Torch) {
        refLamp(x, y, z) = 255;
    }
}

static const int kDir[6][3] = {
    { 1, 0, 0 }, { -1, 0, 0 },
    { 0, 1, 0 }, { 0, -1, 0 },
    { 0, 0, 1 }, { 0, 0, -1 }
};

static void floodBox(int x0, int y0, int z0, int x1, int y1, int z1, int passes) {
    if (y0 < 0) y0 = 0;
    if (y1 >= HEIGHT) y1 = HEIGHT - 1;
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
                    if (!inBounds(x, y, z)) continue;
                    Block here = getB(x, y, z);
                    if (blockInfo(here).blocksSky && here != Block::Lava) continue;
                    float bestSky = (float)refSky(x, y, z) * (1.0f / 255.0f);
                    float bestLamp = (float)refLamp(x, y, z) * (1.0f / 255.0f);
                    for (int d = 0; d < 6; ++d) {
                        int nx = x + kDir[d][0], ny = y + kDir[d][1], nz = z + kDir[d][2];
                        if (!inBounds(nx, ny, nz)) continue;
                        Block nb = getB(nx, ny, nz);
                        if (blockInfo(nb).blocksSky && nb != Block::Lava) continue;
                        float inSky = (float)refSky(nx, ny, nz) * (1.0f / 255.0f) * skyDecay;
                        float inLamp = (float)refLamp(nx, ny, nz) * (1.0f / 255.0f) * lampDecay;
                        if (nb == Block::Water) {
                            inSky *= 0.85f;
                            inLamp *= 0.9f;
                        }
                        if (inSky > bestSky) bestSky = inSky;
                        if (inLamp > bestLamp) bestLamp = inLamp;
                    }
                    if (here == Block::Torch || here == Block::Lava)
                        bestLamp = bestLamp > 1.0f ? bestLamp : 1.0f;
                    refSky(x, y, z) = (unsigned char)(bestSky * 255.0f + 0.5f);
                    refLamp(x, y, z) = (unsigned char)(bestLamp * 255.0f + 0.5f);
                }
            }
        }
    }
}

static void relightRegion(int x, int y, int z) {
    const int r = 4;
    for (int ix = x - r; ix <= x + r; ++ix)
        for (int iz = z - r; iz <= z + r; ++iz)
            verticalColumn(ix, iz);
    int y0 = y - 4, y1 = y + 4;
    if (y0 < 0) y0 = 0;
    if (y1 >= HEIGHT) y1 = HEIGHT - 1;
    floodBox(x - r, y0, z - r, x + r, y1, z + r, 4);
}

static void placeInChunk(Chunk* c, int wx, int y, int wz, Block b) {
    if (!yOk(y)) return;
    if (divFloor(wx, CHUNK) != c->cx || divFloor(wz, CHUNK) != c->cz) return;
    int lx = modFloor(wx, CHUNK), lz = modFloor(wz, CHUNK);
    int i = cidx(lx, y, lz);
    if (c->vol[i] != Block::Air && b != Block::Leaves && b != Block::Log) return;
    if (b == Block::Leaves && c->vol[i] != Block::Air) return;
    setVol(c, lx, y, lz, b);
}

static void growTreeInto(Chunk* c, int x, int z, int ground) {
    unsigned h = mixHash(x, z, 201);
    int kind = (int)(h % 5);
    int trunk = 4;
    int rad = 2;
    if (kind == 0) { trunk = 3 + (int)(h % 2); rad = 2; }
    else if (kind == 1 || kind == 2) { trunk = 5 + (int)(h % 3); rad = 2; }
    else if (kind == 3) { trunk = 8 + (int)(h % 3); rad = 3; }
    else { trunk = 10 + (int)(h % 4); rad = 3; }

    for (int i = 0; i < trunk; ++i)
        placeInChunk(c, x, ground + 1 + i, z, Block::Log);
    int top = ground + trunk;
    for (int dy = -rad; dy <= 2; ++dy) {
        int r = (dy >= 1) ? 1 : rad;
        if (dy == -rad) r = rad - 1;
        if (r < 1) r = 1;
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz) {
                if (dx == 0 && dz == 0 && dy <= 0) continue;
                if (std::abs(dx) == r && std::abs(dz) == r && (mixHash(x + dx, z + dz, 7) % 3) == 0) continue;
                placeInChunk(c, x + dx, top + dy, z + dz, Block::Leaves);
            }
    }
}

static void generateChunk(Chunk* c) {
    for (int i = 0; i < CHUNK * CHUNK * HEIGHT; ++i) {
        c->vol[i] = Block::Air;
        c->meta[i] = 0;
        c->sky[i] = 0;
        c->lamp[i] = 0;
        c->src[i] = 0;
        c->lvl[i] = 255;
        c->fill[i] = 0;
    }
    for (int i = 0; i < CHUNK * CHUNK; ++i) {
        c->surf[i] = 0.0f;
        c->mark[i] = 0;
    }
    int colGround[CHUNK * CHUNK];
    unsigned char colWet[CHUNK * CHUNK];
    unsigned char carveLo[CHUNK * CHUNK];
    for (int i = 0; i < CHUNK * CHUNK; ++i) carveLo[i] = 255;

    constexpr int kPad = 3;
    constexpr int kSpan = CHUNK + kPad * 2;
    ColGen span[kSpan][kSpan];
    int x0 = c->cx * CHUNK - kPad;
    int z0 = c->cz * CHUNK - kPad;
    for (int iz = 0; iz < kSpan; ++iz)
        for (int ix = 0; ix < kSpan; ++ix)
            span[ix][iz] = columnGen(x0 + ix, z0 + iz);
    auto col = [&](int wx, int wz) -> ColGen {
        int ix = wx - x0, iz = wz - z0;
        if ((unsigned)ix >= (unsigned)kSpan || (unsigned)iz >= (unsigned)kSpan)
            return columnGen(wx, wz);
        return span[ix][iz];
    };

    for (int lx = 0; lx < CHUNK; ++lx)
        for (int lz = 0; lz < CHUNK; ++lz) {
            int wx = c->cx * CHUNK + lx;
            int wz = c->cz * CHUNK + lz;
            int ci = lz * CHUNK + lx;
            ColGen g = col(wx, wz);
            colGround[ci] = g.ground;
            colWet[ci] = (g.ocean || g.lake) ? 1 : 0;
            int slope = 0;
            ColGen gx = col(wx + 1, wz);
            ColGen gz = col(wx, wz + 1);
            slope = std::abs(gx.ground - g.ground);
            int sz = std::abs(gz.ground - g.ground);
            if (sz > slope) slope = sz;

            for (int y = 0; y <= g.ground; ++y) {
                Block b = Block::Stone;
                if (y == g.ground) {
                    if (g.ocean || g.lake) b = Block::Dirt;
                    else if (g.beach) b = Block::Dirt;
                    else if (g.mountain && (slope >= 3 || g.ground > SEA_LEVEL + 26)) b = Block::Stone;
                    else b = Block::Grass;
                }
                else if (y >= g.ground - 3) b = (g.mountain && slope >= 2) ? Block::Stone : Block::Dirt;
                setVol(c, lx, y, lz, b);
            }
            if (g.waterTo >= g.ground + 1) {
                int top = g.waterTo;
                if (top >= HEIGHT) top = HEIGHT - 1;
                for (int y = g.ground + 1; y <= top; ++y)
                    setVol(c, lx, y, lz, Block::Water);
            }
        }

    for (int lx = 0; lx < CHUNK; ++lx)
        for (int lz = 0; lz < CHUNK; ++lz) {
            int ci = lz * CHUNK + lx;
            int ground = colGround[ci];
            bool wet = colWet[ci] != 0;
            int wx = c->cx * CHUNK + lx;
            int wz = c->cz * CHUNK + lz;
            bool entrance = !wet && (mixHash(wx, wz, 901) % 240) == 0;
            int yTop = wet ? ground - 5 : (entrance ? ground : ground - 4);
            if (yTop > HEIGHT - 2) yTop = HEIGHT - 2;
            if (yTop < 6) continue;
            for (int y = 4; y <= yTop; y += 2) {
                float cheese = valueNoise3((float)wx * 0.04f, (float)y * 0.06f, (float)wz * 0.04f, 301);
                bool cave = cheese > 0.67f;
                if (!cave) continue;
                for (int yy = y; yy <= y + 1 && yy <= yTop; ++yy) {
                    Block cur = c->vol[cidx(lx, yy, lz)];
                    if (cur == Block::Air || cur == Block::Water) continue;
                    setVol(c, lx, yy, lz, Block::Air);
                    if (yy > 0 && (int)carveLo[ci] > yy - 1) carveLo[ci] = (unsigned char)(yy - 1);
                }
            }
        }

    auto wellFloor = [&](int lx, int y, int lz) -> bool {
        if (lx < 1 || lz < 1 || lx >= CHUNK - 1 || lz >= CHUNK - 1) return false;
        if (y < 6 || y >= HEIGHT - 2) return false;
        Block here = c->vol[cidx(lx, y, lz)];
        Block below = c->vol[cidx(lx, y - 1, lz)];
        Block above = c->vol[cidx(lx, y + 1, lz)];
        if (here != Block::Air || above != Block::Air) return false;
        if (below != Block::Stone && below != Block::Dirt && below != Block::Grass) return false;
        const int dir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int i = 0; i < 4; ++i) {
            int nx = lx + dir[i][0], nz = lz + dir[i][1];
            Block side = c->vol[cidx(nx, y, nz)];
            Block under = c->vol[cidx(nx, y - 1, nz)];
            bool openDrop = (side == Block::Air || side == Block::Water)
                && (under == Block::Air || under == Block::Water);
            if (openDrop) return false;
        }
        return true;
    };
    for (int lx = 1; lx < CHUNK - 1; ++lx)
        for (int lz = 1; lz < CHUNK - 1; ++lz) {
            int ci = lz * CHUNK + lx;
            if (colWet[ci]) continue;
            int wx = c->cx * CHUNK + lx;
            int wz = c->cz * CHUNK + lz;
            if ((mixHash(wx >> 1, wz >> 1, 770) % 70) != 0) continue;
            int ground = colGround[ci];
            int floorY = -1;
            int limit = ground - 6;
            if (limit > HEIGHT - 3) limit = HEIGHT - 3;
            for (int y = 6; y < limit; ++y) {
                if (!wellFloor(lx, y, lz)) continue;
                floorY = y;
                break;
            }
            if (floorY < 0) continue;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    if (wellFloor(lx + dx, floorY, lz + dz))
                        setVol(c, lx + dx, floorY, lz + dz, Block::Water);
        }

    auto treeCandidate = [&](int x, int z, int& ground) -> bool {
        unsigned h = mixHash(x, z, 44);
        if ((int)(h % 1000) > 1000 / 22) return false;
        ColGen g = col(x, z);
        if (g.ocean || g.lake || g.beach || g.ground < SEA_LEVEL) return false;
        int chance = g.forest ? 22 : (g.mountain ? 90 : 70);
        if ((int)(h % 1000) > 1000 / chance) return false;
        if ((h >> 8) % 7 == 0) return false;
        ground = g.ground;
        return true;
    };
    for (int lx = -3; lx < CHUNK + 3; ++lx)
        for (int lz = -3; lz < CHUNK + 3; ++lz) {
            int wx = c->cx * CHUNK + lx;
            int wz = c->cz * CHUNK + lz;
            int ground = 0;
            if (!treeCandidate(wx, wz, ground)) continue;
            unsigned h = mixHash(wx, wz, 44);
            bool crowded = false;
            for (int oz = -1; oz <= 1 && !crowded; ++oz)
                for (int ox = -1; ox <= 1; ++ox) {
                    if (!ox && !oz) continue;
                    int ng = 0;
                    if (!treeCandidate(wx + ox, wz + oz, ng)) continue;
                    unsigned hn = mixHash(wx + ox, wz + oz, 44);
                    if (hn < h || (hn == h && (ox < 0 || (ox == 0 && oz < 0)))) {
                        crowded = true;
                        break;
                    }
                }
            if (crowded) continue;
            if (lx >= 0 && lx < CHUNK && lz >= 0 && lz < CHUNK && ground >= 0 && ground < HEIGHT) {
                Block floor = c->vol[cidx(lx, ground, lz)];
                if (floor != Block::Grass && floor != Block::Dirt) continue;
            }
            growTreeInto(c, wx, wz, ground);
        }

    for (int lx = 0; lx < CHUNK; ++lx)
        for (int lz = 0; lz < CHUNK; ++lz) {
            int wx = c->cx * CHUNK + lx;
            int wz = c->cz * CHUNK + lz;
            int iTop = -1;
            for (int y = HEIGHT - 2; y >= 0; --y) {
                if (c->vol[cidx(lx, y, lz)] != Block::Air) { iTop = y; break; }
            }
            if (iTop < 0) continue;
            if (c->vol[cidx(lx, iTop, lz)] != Block::Grass) continue;
            if (c->vol[cidx(lx, iTop + 1, lz)] != Block::Air) continue;
            unsigned h = mixHash(wx + 19, wz + 3, 5);
            Block plant = Block::Air;
            if (h % 3 == 0) plant = Block::TallGrass;
            else if (h % 15 == 0) plant = Block::Poppy;
            else if (h % 17 == 0) plant = Block::Triteleia;
            else if (h % 21 == 0) plant = Block::TriteleiaRed;
            if (plant != Block::Air)
                setVol(c, lx, iTop + 1, lz, plant);
        }

    for (int lx = 0; lx < CHUNK; ++lx)
        for (int lz = 0; lz < CHUNK; ++lz) {
            int i = lz * CHUNK + lx;
            int top = -1;
            for (int y = HEIGHT - 1; y >= 0; --y) {
                if (c->vol[cidx(lx, y, lz)] != Block::Air) { top = y; break; }
            }
            if (top < 0) {
                c->vis0[i] = 1;
                c->vis1[i] = 0;
            }
            else {
                int base = SEA_LEVEL - 20;
                if (top - base > 96) base = top - 96;
                if (carveLo[i] < 255 && carveLo[i] < base) base = carveLo[i];
                if (base < 0) base = 0;
                if (base > top) base = top;
                c->vis0[i] = (unsigned char)base;
                c->vis1[i] = (unsigned char)top;
            }
            verticalColumn(c->cx * CHUNK + lx, c->cz * CHUNK + lz);
        }
}

static bool gMeshDirty = true;
static bool gWaterMeshDirty = true;
static int gMeshX0 = -99999, gMeshZ0 = -99999, gMeshX1 = 99999, gMeshZ1 = 99999;

void markWaterMeshDirty() { gWaterMeshDirty = true; }

bool consumeWaterMeshDirty() {
    bool d = gWaterMeshDirty;
    gWaterMeshDirty = false;
    return d;
}

void markMeshArea(int x0, int z0, int x1, int z1) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (z0 > z1) { int t = z0; z0 = z1; z1 = t; }
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
    gMeshX0 = 1; gMeshZ0 = 1; gMeshX1 = 0; gMeshZ1 = 0;
    return true;
}

static void dirtyWaterAround(int x, int z) {
    for (int oz = -1; oz <= 1; ++oz)
        for (int ox = -1; ox <= 1; ++ox) {
            Chunk* c = chunkOf(x + ox * CHUNK, z + oz * CHUNK);
            if (c) c->surfDirty = true;
        }
    markWaterMeshDirty();
}

static void refreshChunkSurface(Chunk* c) {
    int yStart = HEIGHT - 1;
    for (int lx = 0; lx < CHUNK; ++lx)
        for (int lz = 0; lz < CHUNK; ++lz) {
            int i = lz * CHUNK + lx;
            c->surf[i] = 0.0f;
            c->mark[i] = 0;
            int top = -1;
            for (int y = yStart; y >= 0; --y) {
                if (c->vol[cidx(lx, y, lz)] == Block::Water) { top = y; break; }
            }
            if (top < 0) continue;
            float fill = c->src[cidx(lx, top, lz)] ? 1.0f : (float)c->fill[cidx(lx, top, lz)] * (1.0f / 255.0f);
            float h = (float)top + fill;
            if (top + 1 < HEIGHT && blockInfo(c->vol[cidx(lx, top + 1, lz)]).solid && h > (float)top + 0.97f)
                h = (float)top + 0.97f;
            c->surf[i] = h;
            c->mark[i] = 1;
        }
    c->surfDirty = false;
}

void refreshWaterHeights() {
    bool any = false;
    for (size_t i = 0; i < gLoaded.size(); ++i) {
        if (!gLoaded[i]->surfDirty) continue;
        refreshChunkSurface(gLoaded[i]);
        any = true;
    }
    if (!any) return;
    markWaterMeshDirty();
}

static Chunk* createChunk(int cx, int cz) {
    Chunk* c = new Chunk();
    c->cx = cx;
    c->cz = cz;
    gChunks[packChunk(cx, cz)] = c;
    gLoaded.push_back(c);
    generateChunk(c);
    refreshChunkSurface(c);
    markMeshArea(cx * CHUNK, cz * CHUNK, cx * CHUNK + CHUNK - 1, cz * CHUNK + CHUNK - 1);
    markMeshArea((cx - 1) * CHUNK, cz * CHUNK, cx * CHUNK, (cz + 1) * CHUNK - 1);
    markMeshArea((cx + 1) * CHUNK, cz * CHUNK, (cx + 1) * CHUNK + CHUNK - 1, (cz + 1) * CHUNK - 1);
    markMeshArea(cx * CHUNK, (cz - 1) * CHUNK, (cx + 1) * CHUNK - 1, cz * CHUNK);
    markMeshArea(cx * CHUNK, (cz + 1) * CHUNK, (cx + 1) * CHUNK - 1, (cz + 1) * CHUNK + CHUNK - 1);
    return c;
}

int worldStreamStep(float x, float z, int inner, int budget, int radius) {
    int ccx = divFloor((int)std::floor(x), CHUNK);
    int ccz = divFloor((int)std::floor(z), CHUNK);
    const int R = radius < 1 ? 1 : radius;
    if (inner < 0) inner = 0;
    if (budget < 1) budget = 1;
    struct Miss { int dx, dz, d; };
    std::vector<Miss> miss;
    miss.reserve(64);
    for (int dz = -R; dz <= R; ++dz)
        for (int dx = -R; dx <= R; ++dx) {
            if (findChunk(ccx + dx, ccz + dz)) continue;
            int adx = dx < 0 ? -dx : dx;
            int adz = dz < 0 ? -dz : dz;
            int d = adx > adz ? adx : adz;
            Miss m;
            m.dx = dx; m.dz = dz; m.d = d;
            miss.push_back(m);
        }
    std::sort(miss.begin(), miss.end(), [](const Miss& a, const Miss& b) { return a.d < b.d; });
    int made = 0;
    for (size_t i = 0; i < miss.size(); ++i) {
        if (made >= budget) break;
        if (miss[i].d > inner && made >= 1) break;
        createChunk(ccx + miss[i].dx, ccz + miss[i].dz);
        ++made;
    }
    return (int)miss.size() - made;
}

int worldStreamAround(float x, float z) {
    int before = (int)gLoaded.size();
    worldStreamStep(x, z, 2, 1, STREAM_RADIUS);
    int ccx = divFloor((int)std::floor(x), CHUNK);
    int ccz = divFloor((int)std::floor(z), CHUNK);
    const int R = STREAM_RADIUS;
    size_t w = 0;
    for (size_t i = 0; i < gLoaded.size(); ++i) {
        Chunk* c = gLoaded[i];
        int adx = c->cx - ccx;
        int adz = c->cz - ccz;
        if (adx < 0) adx = -adx;
        if (adz < 0) adz = -adz;
        if (adx > R + 1 || adz > R + 1) {
            if (!c->edited) {
                if (gChunkHint == c) gChunkHint = nullptr;
                gChunks.erase(packChunk(c->cx, c->cz));
                delete c;
                continue;
            }
        }
        gLoaded[w++] = c;
    }
    gLoaded.resize(w);
    int made = (int)gLoaded.size() - before;
    return made > 0 ? made : 0;
}

int worldMissingAround(float x, float z, int radius) {
    int ccx = divFloor((int)std::floor(x), CHUNK);
    int ccz = divFloor((int)std::floor(z), CHUNK);
    int miss = 0;
    for (int dz = -radius; dz <= radius; ++dz)
        for (int dx = -radius; dx <= radius; ++dx)
            if (!findChunk(ccx + dx, ccz + dz)) ++miss;
    return miss;
}

static bool columnDry(int x, int z, int& ground) {
    ColGen g = columnGen(x, z);
    if (g.ocean || g.lake || g.beach || g.waterTo >= 0) return false;
    if (g.ground < SEA_LEVEL + 3) return false;
    ground = g.ground;
    return true;
}

static bool padDry(int x, int z, int& ground) {
    if (!columnDry(x, z, ground)) return false;
    for (int dz = -2; dz <= 2; ++dz)
        for (int dx = -2; dx <= 2; ++dx) {
            if (!dx && !dz) continue;
            int near = 0;
            if (!columnDry(x + dx, z + dz, near)) return false;
            int dh = near - ground;
            if (dh < 0) dh = -dh;
            if (dh > 2) return false;
        }
    return true;
}

static bool blocksDry(int x, int y, int z) {
    for (int dz = -2; dz <= 2; ++dz)
        for (int dx = -2; dx <= 2; ++dx) {
            int wx = x + dx, wz = z + dz;
            if (!inBounds(wx, y, wz)) return false;
            if (!solid(wx, y, wz)) return false;
            if (getB(wx, y, wz) == Block::Water) return false;
            if (getB(wx, y + 1, wz) == Block::Water || getB(wx, y + 2, wz) == Block::Water) return false;
            if (solid(wx, y + 1, wz) || solid(wx, y + 2, wz)) return false;
            if (waterHasSurface(wx, wz) && waterSurfaceY(wx, wz) >= (float)y + 0.75f) return false;
        }
    return true;
}

bool worldFindSpawn(int& x, int& y, int& z) {
    unsigned h = mixHash((int)gWorldSeed, 19, 77);
    int ox = (int)(h % 241) - 120;
    int oz = (int)((h >> 9) % 241) - 120;
    for (int r = 0; r < 140; r += 2) {
        for (int dz = -r; dz <= r; dz += 2)
            for (int dx = -r; dx <= r; dx += 2) {
                if (r > 0 && std::abs(dx) != r && std::abs(dz) != r) continue;
                int ground = 0;
                if (!padDry(ox + dx, oz + dz, ground)) continue;
                x = ox + dx; y = ground; z = oz + dz;
                return true;
            }
    }
    for (int r = 0; r < 140; r += 3) {
        for (int dz = -r; dz <= r; dz += 3)
            for (int dx = -r; dx <= r; dx += 3) {
                if (r > 0 && std::abs(dx) != r && std::abs(dz) != r) continue;
                int ground = 0;
                if (!columnDry(ox + dx, oz + dz, ground)) continue;
                x = ox + dx; y = ground; z = oz + dz;
                return true;
            }
    }
    x = ox; y = SEA_LEVEL + 6; z = oz;
    return false;
}

void initializeWorld() {
    refreshWaterHeights();
}

struct WaterCell { int x, y, z; };
static std::vector<WaterCell> waterCells;
static float gImpactLock = 0.0f;

void worldReset() {
    gChunkHint = nullptr;
    for (size_t i = 0; i < gLoaded.size(); ++i)
        delete gLoaded[i];
    gLoaded.clear();
    gChunks.clear();
    waterCells.clear();
    gImpactLock = 0.0f;
    gMeshDirty = true;
    gWaterMeshDirty = true;
    gMeshX0 = -99999; gMeshZ0 = -99999; gMeshX1 = 99999; gMeshZ1 = 99999;
}

void worldFixSpawn(int& x, int& y, int& z) {
    auto ok = [&](int wx, int wz) -> bool {
        int gy = 0;
        if (!padDry(wx, wz, gy)) return false;
        if (!inBounds(wx, gy, wz)) return false;
        if (!blocksDry(wx, gy, wz)) return false;
        x = wx;
        y = gy;
        z = wz;
        return true;
    };
    if (ok(x, z)) return;
    for (int r = 2; r < 96; r += 2) {
        for (int dz = -r; dz <= r; dz += 2)
            for (int dx = -r; dx <= r; dx += 2) {
                if (std::abs(dx) != r && std::abs(dz) != r) continue;
                if (ok(x + dx, z + dz)) return;
            }
    }
}

static void rememberWater(int x, int y, int z) {
    waterCells.push_back({ x, y, z });
}

static bool isSrc(int x, int y, int z) {
    return inBounds(x, y, z) && refSrc(x, y, z) != 0;
}

static float fillOf(int x, int y, int z) {
    if (!inBounds(x, y, z) || getB(x, y, z) != Block::Water) return 0.0f;
    if (isSrc(x, y, z)) return 255.0f;
    return (float)refFill(x, y, z);
}

static bool canEnterWater(int x, int y, int z) {
    if (!inBounds(x, y, z) || isSrc(x, y, z)) return false;
    Block b = getB(x, y, z);
    return b == Block::Air || b == Block::Torch || isPlant(b);
}

static void eraseWater(int x, int y, int z) {
    if (!inBounds(x, y, z)) return;
    int i = cidx(modFloor(x, CHUNK), y, modFloor(z, CHUNK));
    Chunk* c = chunkOf(x, z);
    c->vol[i] = Block::Air;
    c->src[i] = 0;
    c->lvl[i] = 255;
    c->fill[i] = 0;
    c->meta[i] = 0;
}

static bool erasePlant(int x, int y, int z) {
    if (!inBounds(x, y, z) || !isPlant(getB(x, y, z))) return false;
    int i = cidx(modFloor(x, CHUNK), y, modFloor(z, CHUNK));
    Chunk* c = chunkOf(x, z);
    c->vol[i] = Block::Air;
    c->meta[i] = 0;
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

static void resolveLavaAt(int x, int y, int z);
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

static bool placeFlow(int x, int y, int z, int lv);
static float addFillTo(int x, int y, int z, float amount, int lv, bool& changed);

static int surfaceY(int x, int y, int z) {
    while (y + 1 < HEIGHT && getB(x, y + 1, z) == Block::Water) ++y;
    return y;
}

static bool placeFlow(int x, int y, int z, int lv) {
    if (!canEnterWater(x, y, z)) return false;
    Chunk* c = chunkOf(x, z);
    int i = cidx(modFloor(x, CHUNK), y, modFloor(z, CHUNK));
    c->vol[i] = Block::Water;
    c->src[i] = 0;
    c->lvl[i] = (unsigned char)lv;
    c->fill[i] = 6;
    c->meta[i] = 0;
    c->edited = true;
    rememberWater(x, y, z);
    resolveFluidContact(x, y, z);
    return true;
}

static float addFillTo(int x, int y, int z, float amount, int lv, bool& changed) {
    if (amount <= 0.0f || !inBounds(x, y, z) || solid(x, y, z)) return 0.0f;
    if (getB(x, y, z) != Block::Water) {
        if (!canEnterWater(x, y, z)) return 0.0f;
        if (!placeFlow(x, y, z, lv)) return 0.0f;
        changed = true;
        refFill(x, y, z) = 0;
    }
    if (isSrc(x, y, z)) return 0.0f;
    float have = (float)refFill(x, y, z);
    float room = 255.0f - have;
    if (room <= 0.0f) return 0.0f;
    if (amount > room) amount = room;
    refFill(x, y, z) = (unsigned char)(have + amount + 0.5f);
    chunkOf(x, z)->edited = true;
    return amount;
}

float waterFill01(int x, int y, int z) {
    if (!inBounds(x, y, z) || getB(x, y, z) != Block::Water) return 0.0f;
    if (isSrc(x, y, z)) return 1.0f;
    return (float)refFill(x, y, z) * (1.0f / 255.0f);
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

    const float rateSide = 55.0f * dt;
    const float rateSrc = 64.0f * dt;
    const int dir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    std::vector<WaterCell> snapshot = waterCells;

    auto fallingAt = [&](int x, int y, int z) {
        if (y <= 0 || isSrc(x, y, z)) return false;
        if (solid(x, y - 1, z)) return false;
        if (getB(x, y - 1, z) == Block::Water && fillOf(x, y - 1, z) > 209.0f) return false;
        return !solid(x, y - 1, z);
    };

    for (const WaterCell& cell : snapshot) {
        if (!inBounds(cell.x, cell.y, cell.z) || getB(cell.x, cell.y, cell.z) != Block::Water) continue;
        if (erasePlantsNear(cell.x, cell.y, cell.z))
            touch(cell.x, cell.y, cell.z);
        bool source = isSrc(cell.x, cell.y, cell.z);
        int lv = source ? 0 : (int)refLvl(cell.x, cell.y, cell.z);
        float have = fillOf(cell.x, cell.y, cell.z);

        int by = cell.y - 1;
        bool openBelow = cell.y > 0 && !solid(cell.x, by, cell.z) && canEnterWater(cell.x, by, cell.z);
        bool waterBelow = cell.y > 0 && getB(cell.x, by, cell.z) == Block::Water && fillOf(cell.x, by, cell.z) < 254.5f;

        if ((openBelow || waterBelow) && !solid(cell.x, by, cell.z)) {
            int dest = by;
            while (dest > 0 && canEnterWater(cell.x, dest, cell.z)) --dest;
            int land = dest;
            if (solid(cell.x, dest, cell.z)) land = dest + 1;
            else if (getB(cell.x, dest, cell.z) == Block::Water) {
                land = dest;
                if (fillOf(cell.x, dest, cell.z) >= 254.5f && dest + 1 < cell.y && !solid(cell.x, dest + 1, cell.z))
                    land = dest + 1;
            }
            if (land >= cell.y || solid(cell.x, land, cell.z)) {
                if (!source) continue;
            }
            else {
                float give = source ? (rateSrc * 3.5f) : have;
                if (!source) {
                    refFill(cell.x, cell.y, cell.z) = 0;
                    eraseWater(cell.x, cell.y, cell.z);
                    touch(cell.x, cell.y, cell.z);
                }
                if (give > 0.0f && !solid(cell.x, land, cell.z)) {
                    addFillTo(cell.x, land, cell.z, give, lv < 7 ? lv + 1 : 7, changed);
                    erasePlantsNear(cell.x, land, cell.z);
                    touch(cell.x, land, cell.z);
                }
            }
            if (!source) continue;
            have = fillOf(cell.x, cell.y, cell.z);
        }

        if (have < 4.0f && !source) continue;

        int bestX = -1, bestZ = -1;
        float bestFill = 256.0f;
        for (int i = 0; i < 4; ++i) {
            int nx = cell.x + dir[i][0], nz = cell.z + dir[i][1];
            if (!inBounds(nx, cell.y, nz)) continue;
            if (solid(nx, cell.y, nz)) continue;
            Block nb = getB(nx, cell.y, nz);
            if (nb != Block::Water && !canEnterWater(nx, cell.y, nz)) continue;
            float nf = fillOf(nx, cell.y, nz);
            if (nb == Block::Air || nb == Block::Torch || isPlant(nb)) nf = 0.0f;
            if (have - nf < 4.0f) continue;
            if (nf < bestFill) {
                bestFill = nf;
                bestX = nx;
                bestZ = nz;
            }
        }
        if (bestX >= 0) {
            float give = source ? rateSrc : (have - bestFill) * 0.38f;
            if (give > rateSide) give = rateSide;
            if (!source) {
                if (give > have - 3.0f) give = have - 3.0f;
                if (give > 0.0f) {
                    refFill(cell.x, cell.y, cell.z) = (unsigned char)(have - give);
                    have -= give;
                }
            }
            if (give > 0.0f) {
                addFillTo(bestX, cell.y, bestZ, give, lv < 7 ? lv + 1 : 7, changed);
                erasePlantsNear(bestX, cell.y, bestZ);
                touch(bestX, cell.y, bestZ);
            }
        }
        else if (have >= 252.0f && cell.y + 1 < HEIGHT && !solid(cell.x, cell.y + 1, cell.z)
            && (canEnterWater(cell.x, cell.y + 1, cell.z) || getB(cell.x, cell.y + 1, cell.z) == Block::Water)) {
            float give = source ? rateSrc : rateSide;
            if (!source && give > have - 220.0f) give = have - 220.0f;
            if (give > 0.0f) {
                if (!source) refFill(cell.x, cell.y, cell.z) = (unsigned char)(have - give);
                addFillTo(cell.x, cell.y + 1, cell.z, give, lv, changed);
                erasePlantsNear(cell.x, cell.y + 1, cell.z);
                touch(cell.x, cell.y + 1, cell.z);
            }
        }
    }

    for (const WaterCell& cell : snapshot) {
        if (!inBounds(cell.x, cell.y, cell.z) || getB(cell.x, cell.y, cell.z) != Block::Water) continue;
        if (isSrc(cell.x, cell.y, cell.z) || fallingAt(cell.x, cell.y, cell.z)) continue;
        float have = fillOf(cell.x, cell.y, cell.z);
        if (have < 8.0f) continue;
        for (int i = 0; i < 4; ++i) {
            int nx = cell.x + dir[i][0], nz = cell.z + dir[i][1];
            if (!inBounds(nx, cell.y, nz) || solid(nx, cell.y, nz)) continue;
            if (getB(nx, cell.y, nz) != Block::Water) continue;
            if (isSrc(nx, cell.y, nz) || fallingAt(nx, cell.y, nz)) continue;
            float nf = fillOf(nx, cell.y, nz);
            float diff = have - nf;
            if (diff < 3.0f) continue;
            float xfer = diff * 0.28f;
            if (xfer > rateSide) xfer = rateSide;
            if (xfer > have - 3.0f) xfer = have - 3.0f;
            if (xfer <= 0.0f) continue;
            refFill(cell.x, cell.y, cell.z) = (unsigned char)(have - xfer);
            addFillTo(nx, cell.y, nz, xfer, (int)refLvl(nx, cell.y, nz), changed);
            have -= xfer;
            touch(nx, cell.y, nz);
        }
    }

    size_t w = 0;
    for (size_t i = 0; i < waterCells.size(); ++i) {
        WaterCell cell = waterCells[i];
        if (inBounds(cell.x, cell.y, cell.z) && getB(cell.x, cell.y, cell.z) == Block::Water)
            waterCells[w++] = cell;
    }
    waterCells.resize(w);
}

void waterImpact(int x, int y, int z, float vy, float mass) {
    if (gImpactLock > 0.0f) return;
    if (!inBounds(x, y, z)) return;
    int sy = y;
    if (getB(x, y, z) != Block::Water) {
        for (int i = 1; i <= 3; ++i) {
            if (inBounds(x, y - i, z) && getB(x, y - i, z) == Block::Water) {
                sy = y - i;
                break;
            }
        }
    }
    if (getB(x, sy, z) != Block::Water) return;
    sy = surfaceY(x, sy, z);
    float speed = vy < 0.0f ? -vy : vy;
    fluidImpact((float)x + 0.5f, (float)sy + 0.95f, (float)z + 0.5f, speed, mass);
    gImpactLock = 0.28f;
}

void tickWater(float dt) {
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;
    gImpactLock -= dt;
    if (gImpactLock < 0.0f) gImpactLock = 0.0f;
    bool changed = false;
    int minX = 100000, minY = HEIGHT, minZ = 100000;
    int maxX = -100000, maxY = 0, maxZ = -100000;
    if (!waterCells.empty())
        stepWater(changed, minX, minY, minZ, maxX, maxY, maxZ, dt);
    refreshWaterHeights();
    if (changed) dirtyWaterAround(minX, minZ);
    if (!changed || minX > maxX) return;
    for (int x = minX; x <= maxX; ++x)
        for (int z = minZ; z <= maxZ; ++z)
            verticalColumn(x, z);
    int y0 = minY - 4, y1 = maxY + 6;
    if (y0 < 0) y0 = 0;
    if (y1 >= HEIGHT) y1 = HEIGHT - 1;
    floodBox(minX - 6, y0, minZ - 6, maxX + 6, y1, maxZ + 6, 8);
    markMeshArea(minX - 16, minZ - 16, maxX + 16, maxZ + 16);
}

float waterSurfaceY(int x, int z) {
    Chunk* c = chunkOf(x, z);
    if (!c) return 0.0f;
    return c->surf[modFloor(z, CHUNK) * CHUNK + modFloor(x, CHUNK)];
}

bool waterHasSurface(int x, int z) {
    Chunk* c = chunkOf(x, z);
    if (!c) return false;
    return c->mark[modFloor(z, CHUNK) * CHUNK + modFloor(x, CHUNK)] != 0;
}

bool waterHasFall(int, int) { return false; }
float waterFallTopY(int, int) { return 0.0f; }
float waterFallBotY(int, int) { return 0.0f; }

float waterPoolDepth(int x, int z) {
    if (!waterHasSurface(x, z)) return 0.0f;
    float h = waterSurfaceY(x, z);
    if (h <= 0.02f) return 0.0f;
    float bot = h;
    while (bot > 0.0f) {
        int iy = (int)std::floor(bot - 0.01f);
        if (iy < 0 || solid(x, iy, z)) break;
        if (getB(x, iy, z) != Block::Water) break;
        bot -= 1.0f;
    }
    float d = h - bot;
    return d < 0.0f ? 0.0f : d;
}

bool waterSwimmable(int x, int y, int z) {
    if (!inBounds(x, y, z) || getB(x, y, z) != Block::Water) return false;
    if (y > 0 && !solid(x, y - 1, z)) {
        bool waterUnder = getB(x, y - 1, z) == Block::Water && waterFill01(x, y - 1, z) > 0.82f;
        if (!waterUnder && !isSrc(x, y, z)) return false;
        if (isSrc(x, y, z) && !waterUnder) return false;
    }
    return waterFill01(x, y, z) >= 0.48f || isSrc(x, y, z);
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
    return refMeta(x, y, z);
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

void setBlock(int x, int y, int z, Block value, int meta) {
    if (!inBounds(x, y, z)) return;
    Chunk* c = chunkOf(x, z);
    int i = cidx(modFloor(x, CHUNK), y, modFloor(z, CHUNK));
    Block old = c->vol[i];
    c->edited = true;
    if (value != Block::Water)
        c->src[i] = 0;
    c->vol[i] = value;
    c->meta[i] = (value == Block::Torch || value == Block::Log) ? (unsigned char)meta : 0;
    if (value == Block::Water || old == Block::Water)
        dirtyWaterAround(x, z);
    if (value == Block::Water) {
        c->src[i] = 1;
        c->lvl[i] = 0;
        c->fill[i] = 255;
        rememberWater(x, y, z);
        erasePlantsNear(x, y, z);
    }
    else {
        c->lvl[i] = 255;
        c->fill[i] = 0;
    }
    if (blockInfo(old).solid && !blockInfo(value).solid)
        releaseDecorations(x, y, z);
    auto expandSpan = [](int sx, int sz, int sy) {
        if (sy < 0) sy = 0;
        if (sy >= HEIGHT) sy = HEIGHT - 1;
        Chunk* sc = chunkOf(sx, sz);
        if (!sc) return;
        int si = modFloor(sz, CHUNK) * CHUNK + modFloor(sx, CHUNK);
        if ((unsigned)sy < sc->vis0[si]) sc->vis0[si] = (unsigned char)sy;
        if ((unsigned)sy > sc->vis1[si]) sc->vis1[si] = (unsigned char)sy;
    };
    for (int oz = -1; oz <= 1; ++oz)
        for (int ox = -1; ox <= 1; ++ox)
            for (int oy = -1; oy <= 1; ++oy)
                expandSpan(x + ox, z + oz, y + oy);
    relightRegion(x, y, z);
    markMeshArea(x - 5, z - 5, x + 5, z + 5);
    if (!gReacting)
        resolveFluidContact(x, y, z);
}

int waterLevelAt(int x, int y, int z) {
    if (!inBounds(x, y, z) || getB(x, y, z) != Block::Water) return -1;
    return refLvl(x, y, z);
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

static float fieldSky(int x, int y, int z) {
    if (!inBounds(x, y, z)) return 0.0f;
    return (float)refSky(x, y, z) * (1.0f / 255.0f);
}

static float fieldLamp(int x, int y, int z) {
    if (!inBounds(x, y, z)) return 0.0f;
    return (float)refLamp(x, y, z) * (1.0f / 255.0f);
}

static float sampleField(bool sky, int bx, int by, int bz, float nx, float ny, float nz, bool skyOpen) {
    int sx = bx + (nx > 0.0f ? 1 : (nx < 0.0f ? -1 : 0));
    int sy = by + (ny > 0.0f ? 1 : (ny < 0.0f ? -1 : 0));
    int sz = bz + (nz > 0.0f ? 1 : (nz < 0.0f ? -1 : 0));
    if (!inBounds(sx, sy, sz)) {
        if (sky && (ny >= 0.0f || skyOpen)) return ny < 0.0f ? 0.62f : 1.0f;
        return 0.0f;
    }
    if (solid(sx, sy, sz)) return sky ? 0.15f : 0.0f;
    return sky ? fieldSky(sx, sy, sz) : fieldLamp(sx, sy, sz);
}

float propagatedLightAt(int x, int y, int z) { return fieldSky(x, y, z); }

void cellLight(int x, int y, int z, float& sky, float& lamp) {
    sky = 0.0f;
    lamp = 0.0f;
    if (!inBounds(x, y, z)) return;
    sky = fieldSky(x, y, z);
    lamp = fieldLamp(x, y, z);
    for (int i = 1; i <= 5; ++i) {
        Block above = blockAt(x, y + i, z);
        if (above == Block::Leaves) sky *= 0.72f;
        else if (blockInfo(above).blocksSky) break;
    }
}

float sampleSkyForFace(int bx, int by, int bz, float nx, float ny, float nz) {
    return sampleField(true, bx, by, bz, nx, ny, nz, true);
}

float sampleLampForFace(int bx, int by, int bz, float nx, float ny, float nz) {
    return sampleField(false, bx, by, bz, nx, ny, nz, false);
}

float causticAmount(int x, int y, int z) {
    for (int i = 1; i <= 5; ++i) {
        Block b = blockAt(x, y + i, z);
        if (b == Block::Water) return 1.0f / (float)i;
        if (blockInfo(b).blocksSky) return 0.0f;
    }
    return 0.0f;
}
