#include "FluidSolver.h"

#include "GlIncludes.h"
#include "GlExt.h"
#include "Camera.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <vector>

struct Drop {
    float x, y, z;
    float vx, vy, vz;
    float life, age, size;
};

struct FallSrc {
    float x, yTop, yBot, z, rate, acc;
};

static std::vector<FallSrc> gFalls;

static std::vector<Drop> gDrops;
static unsigned gRng = 1u;

static const int RS = 2;
static const int RN = GRID * RS;
static std::vector<float> gRipCur(RN * RN, 0.0f);
static std::vector<float> gRipPrev(RN * RN, 0.0f);
static std::vector<unsigned char> gRipMask(RN * RN, 0);
static unsigned gRipTex = 0;
static float gRipClock = 0.0f;
static float gMaskClock = 1.0f;
static float gRipEnergy = 0.0f;
static bool gRipDirty = true;

static void rippleDisturb(float wx, float wz, float radius, float amount) {
    float cx = wx * RS, cz = wz * RS;
    float r = radius * RS;
    int x0 = (int)std::floor(cx - r), x1 = (int)std::ceil(cx + r);
    int z0 = (int)std::floor(cz - r), z1 = (int)std::ceil(cz + r);
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            if (x < 1 || z < 1 || x >= RN - 1 || z >= RN - 1) continue;
            float dx = (x + 0.5f - cx) / r, dz = (z + 0.5f - cz) / r;
            float d2 = dx * dx + dz * dz;
            if (d2 > 1.0f) continue;
            float w = 0.5f + 0.5f * std::cos(std::sqrt(d2) * 3.14159265f);
            gRipCur[z * RN + x] -= amount * w;
        }
    gRipEnergy = 1.0f;
}

static void rebuildMask() {
    std::fill(gRipMask.begin(), gRipMask.end(), 0);
    int n = waterCellCount();
    for (int i = 0; i < n; ++i) {
        int bx, by, bz;
        getWaterCell(i, bx, by, bz);
        if (bx < 0 || bz < 0 || bx >= GRID || bz >= GRID) continue;
        for (int sz = 0; sz < RS; ++sz)
            for (int sx = 0; sx < RS; ++sx)
                gRipMask[(bz * RS + sz) * RN + bx * RS + sx] = 1;
    }
}

static void rippleStep() {
    const float damp = 0.985f;
    for (int z = 1; z < RN - 1; ++z)
        for (int x = 1; x < RN - 1; ++x) {
            int i = z * RN + x;
            if (!gRipMask[i]) {
                gRipPrev[i] = 0.0f;
                continue;
            }
            float sum = gRipCur[i - 1] + gRipCur[i + 1] + gRipCur[i - RN] + gRipCur[i + RN];
            float next = (sum * 0.5f - gRipPrev[i]) * damp;
            gRipPrev[i] = next;
        }
    gRipCur.swap(gRipPrev);
}

void fluidBindRipple(int unit) {
    glx.ActiveTexture(GL_TEXTURE0 + unit);
    if (!gRipTex) {
        glGenTextures(1, &gRipTex);
        glBindTexture(GL_TEXTURE_2D, gRipTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, RN, RN, 0, GL_RED, GL_FLOAT, gRipCur.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gRipDirty = false;
    }
    else {
        glBindTexture(GL_TEXTURE_2D, gRipTex);
        if (gRipDirty) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, RN, RN, GL_RED, GL_FLOAT, gRipCur.data());
            gRipDirty = false;
        }
    }
    glx.ActiveTexture(GL_TEXTURE0);
}

float fluidRippleWorldSize() { return (float)GRID; }

static unsigned gHeightTex = 0;

void fluidBindHeight(int unit) {
    glx.ActiveTexture(GL_TEXTURE0 + unit);
    if (!gHeightTex) {
        glGenTextures(1, &gHeightTex);
        glBindTexture(GL_TEXTURE_2D, gHeightTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, GRID, GRID, 0, GL_RED, GL_FLOAT, waterHeightXZ);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else {
        glBindTexture(GL_TEXTURE_2D, gHeightTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GRID, GRID, GL_RED, GL_FLOAT, waterHeightXZ);
    }
    glx.ActiveTexture(GL_TEXTURE0);
}


static float rnd() {
    gRng = gRng * 1664525u + 1013904223u;
    return (gRng >> 8) * (1.0f / 16777216.0f);
}

static const int NX = 7;
static const int NY = 5;
static const int NZ = 7;

static void projectSplash(float wx, float wy, float wz, float speed, float& outX, float& outY, float& outZ) {
    float vx[NX + 1][NY][NZ] = {};
    float vy[NX][NY + 1][NZ] = {};
    float vz[NX][NY][NZ + 1] = {};
    float p[NX][NY][NZ] = {};
    int kind[NX][NY][NZ];

    int ox = (int)std::floor(wx) - NX / 2;
    int oy = (int)std::floor(wy) - NY / 2;
    int oz = (int)std::floor(wz) - NZ / 2;
    for (int k = 0; k < NZ; ++k)
        for (int j = 0; j < NY; ++j)
            for (int i = 0; i < NX; ++i) {
                Block b = blockAt(ox + i, oy + j, oz + k);
                if (b == Block::Water) kind[i][j][k] = 1;
                else if (solid(ox + i, oy + j, oz + k)) kind[i][j][k] = 2;
                else kind[i][j][k] = 0;
            }

    int ci = NX / 2, cj = NY / 2, ck = NZ / 2;
    kind[ci][cj][ck] = 1;
    vy[ci][cj + 1][ck] = -speed;

    for (int iter = 0; iter < 8; ++iter) {
        for (int k = 0; k < NZ; ++k)
            for (int j = 0; j < NY; ++j)
                for (int i = 0; i < NX; ++i) {
                    if (kind[i][j][k] != 1) continue;
                    float div = (vx[i + 1][j][k] - vx[i][j][k]) + (vy[i][j + 1][k] - vy[i][j][k]) + (vz[i][j][k + 1] - vz[i][j][k]);
                    float diag = 0.0f;
                    float sum = 0.0f;
                    auto neigh = [&](int ni, int nj, int nk) {
                        if (ni < 0 || nj < 0 || nk < 0 || ni >= NX || nj >= NY || nk >= NZ) return;
                        if (kind[ni][nj][nk] == 2) return;
                        diag += 1.0f;
                        if (kind[ni][nj][nk] == 1) sum += p[ni][nj][nk];
                    };
                    neigh(i - 1, j, k); neigh(i + 1, j, k);
                    neigh(i, j - 1, k); neigh(i, j + 1, k);
                    neigh(i, j, k - 1); neigh(i, j, k + 1);
                    if (diag < 1.0f) continue;
                    p[i][j][k] = (sum - div) / diag;
                }
    }

    auto face = [&](int i, int j, int k, int ni, int nj, int nk, float& vel) {
        bool a = i >= 0 && j >= 0 && k >= 0 && i < NX && j < NY && k < NZ && kind[i][j][k] != 2;
        bool b = ni >= 0 && nj >= 0 && nk >= 0 && ni < NX && nj < NY && nk < NZ && kind[ni][nj][nk] != 2;
        if (!a || !b) return;
        float pa = kind[i][j][k] == 1 ? p[i][j][k] : 0.0f;
        float pb = kind[ni][nj][nk] == 1 ? p[ni][nj][nk] : 0.0f;
        vel -= pb - pa;
    };
    for (int k = 0; k < NZ; ++k)
        for (int j = 0; j < NY; ++j)
            for (int i = 0; i < NX; ++i) {
                face(i - 1, j, k, i, j, k, vx[i][j][k]);
                face(i, j - 1, k, i, j, k, vy[i][j][k]);
                face(i, j, k - 1, i, j, k, vz[i][j][k]);
            }
    face(NX - 1, cj, ck, NX, cj, ck, vx[NX][cj][ck]);

    outX = vx[ci + 1][cj][ck] - vx[ci][cj][ck];
    outY = vy[ci][cj + 1][ck];
    if (outY < 0.0f) outY = -outY * 0.35f;
    outZ = vz[ci][cj][ck + 1] - vz[ci][cj][ck];
}

static void pushDrop(float x, float y, float z, float vx, float vy, float vz, float size, float life) {
    if (solid((int)std::floor(x), (int)std::floor(y), (int)std::floor(z))) return;
    if (gDrops.size() > 80) return;
    gDrops.push_back({ x, y, z, vx, vy, vz, life, 0.0f, size });
}

void fluidClearFalls() {
    for (FallSrc& f : gFalls) f.rate = 0.0f;
}

void fluidAddFall(float x, float yTop, float yBot, float z, float rate) {
    if (yTop < yBot + 0.2f) return;
    if (rate < 0.02f) rate = 0.02f;
    for (FallSrc& f : gFalls) {
        if (std::fabs(f.x - x) < 0.05f && std::fabs(f.z - z) < 0.05f && std::fabs(f.yTop - yTop) < 0.2f) {
            f.rate += rate;
            if (yBot < f.yBot) f.yBot = yBot;
            return;
        }
    }
    gFalls.push_back({ x, yTop, yBot, z, rate, 0.0f });
}

void fluidStream(float x, float yTop, float yBot, float z, float strength) {
    fluidAddFall(x, yTop, yBot, z, strength);
}

void fluidImpact(float x, float y, float z, float speed, float mass) {
    if (speed < 0.0f) speed = -speed;
    if (speed < 1.2f || mass <= 0.0f) return;
    float energy = 0.5f * mass * speed * speed;
    int count = (int)(energy / 280.0f);
    if (count < 2) count = 2;
    if (count > 16) count = 16;
    float kick = speed * 0.32f;
    if (kick > 6.0f) kick = 6.0f;

    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
    if (mass > 40.0f)
        projectSplash(x, y, z, speed, gx, gy, gz);

    float sink = std::sqrt(energy) * 0.008f;
    if (sink > 1.0f) sink = 1.0f;
    float radius = 0.28f + std::sqrt(mass) * 0.04f;
    if (radius > 0.85f) radius = 0.85f;
    rippleDisturb(x, z, radius, sink);

    for (int i = 0; i < count; ++i) {
        float ang = rnd() * 6.2831853f;
        float rad = 0.12f + rnd() * 0.4f;
        float px = x + std::cos(ang) * rad * 0.22f;
        float pz = z + std::sin(ang) * rad * 0.22f;
        float py = y + rnd() * 0.06f;
        float svx = std::cos(ang) * kick * (0.35f + rnd() * 0.65f) + gx * 0.35f;
        float svz = std::sin(ang) * kick * (0.35f + rnd() * 0.65f) + gz * 0.35f;
        float svy = (0.3f + rnd() * 0.7f) * kick * 0.8f + gy * 0.4f;
        float size = 0.03f + rnd() * 0.04f;
        pushDrop(px, py, pz, svx, svy, svz, size, 0.35f + rnd() * 0.4f);
    }
}

void fluidTick(float dt) {
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;

    gMaskClock += dt;
    if (gMaskClock > 0.5f) {
        gMaskClock = 0.0f;
        rebuildMask();
    }
    if (gRipEnergy > 0.0f) {
        gRipClock += dt;
        int steps = 0;
        while (gRipClock >= 1.0f / 60.0f && steps < 3) {
            gRipClock -= 1.0f / 60.0f;
            rippleStep();
            ++steps;
        }
        if (steps > 0) gRipDirty = true;
        gRipEnergy -= dt * 0.12f;
        if (gRipEnergy <= 0.0f) {
            gRipEnergy = 0.0f;
            std::fill(gRipCur.begin(), gRipCur.end(), 0.0f);
            std::fill(gRipPrev.begin(), gRipPrev.end(), 0.0f);
            gRipDirty = true;
        }
    }

    const float G = 22.0f;
    size_t fw = 0;
    for (size_t i = 0; i < gFalls.size(); ++i) {
        FallSrc f = gFalls[i];
        if (f.rate <= 0.001f) continue;
        float spawn = 2.2f + f.rate * 2.5f;
        if (spawn > 4.5f) spawn = 4.5f;
        f.acc += dt * spawn;
        while (f.acc >= 1.0f) {
            f.acc -= 1.0f;
            float jx = (rnd() - 0.5f) * 0.06f;
            float jz = (rnd() - 0.5f) * 0.06f;
            float h = f.yTop - f.yBot;
            if (h < 0.2f) h = 0.2f;
            float life = std::sqrt(2.0f * h / G) + 0.12f;
            pushDrop(f.x + jx, f.yTop - rnd() * 0.03f, f.z + jz,
                jx * 1.4f, -0.5f - rnd() * 0.3f, jz * 1.4f,
                0.02f + rnd() * 0.02f,
                life);
        }
        gFalls[fw++] = f;
    }
    gFalls.resize(fw);

    size_t w = 0;
    for (size_t i = 0; i < gDrops.size(); ++i) {
        Drop d = gDrops[i];
        d.age += dt;
        if (d.age >= d.life) continue;
        d.vy -= G * dt;
        d.vx *= (1.0f - 0.35f * dt);
        d.vz *= (1.0f - 0.35f * dt);
        float nx = d.x + d.vx * dt;
        float ny = d.y + d.vy * dt;
        float nz = d.z + d.vz * dt;
        int ix = (int)std::floor(nx);
        int iy = (int)std::floor(ny);
        int iz = (int)std::floor(nz);
        if (solid(ix, iy, iz)) {
            continue;
        }
        else if (blockAt(ix, iy, iz) == Block::Water && d.age > 0.06f) {
            rippleDisturb(nx, nz, 0.16f, d.size * 1.1f);
            continue;
        }
        else {
            d.x = nx;
            d.y = ny;
            d.z = nz;
        }
        gDrops[w++] = d;
    }
    gDrops.resize(w);
}

void fluidDraw() {
    if (gDrops.empty()) return;
    glm::vec3 look = cameraLookDirection();
    glm::vec3 right = glm::cross(look, glm::vec3(0, 1, 0));
    float rl = glm::length(right);
    if (rl < 0.001f) right = glm::vec3(1, 0, 0);
    else right /= rl;
    glm::vec3 up = glm::normalize(glm::cross(right, look));

    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    for (const Drop& d : gDrops) {
        float fade = 1.0f - d.age / d.life;
        float s = d.size * (0.75f + 0.25f * fade);
        glm::vec3 vel(d.vx, d.vy, d.vz);
        float spd = glm::length(vel);
        glm::vec3 along = spd > 0.15f ? vel / spd : glm::vec3(0, -1, 0);
        float stretch = 1.0f + spd * 0.07f;
        if (stretch > 3.2f) stretch = 3.2f;
        glm::vec3 side = glm::cross(along, look);
        float sl = glm::length(side);
        if (sl < 0.001f) side = right;
        else side /= sl;
        glm::vec3 c(d.x, d.y, d.z);
        glm::vec3 r = side * s;
        glm::vec3 u = along * (s * stretch);
        glm::vec3 p[4] = { c - r - u, c + r - u, c + r + u, c - r + u };
        float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        for (int i = 0; i < 4; ++i) {
            glNormal3f(0, 0, 1);
            glTexCoord4f(0, 0, 3.0f, fade);
            glx.MultiTexCoord4f(GL_TEXTURE1, uv[i][0], uv[i][1], 0, 0);
            glVertex3f(p[i].x, p[i].y, p[i].z);
        }
    }
    glEnd();
    glDepthMask(GL_TRUE);
}
