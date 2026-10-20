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

static std::vector<Drop> gDrops;
static unsigned gRng = 1u;
static float gClock = 0.0f;
static float rnd();

struct Impulse { float x, z, birth, amp; };
static Impulse gRings[8];
static int gRingCursor = 0;

struct Trail { float x, z, birth, hx, hz; };
static Trail gTrail[16];
static int gTrailCursor = 0;
static int gTrailCount = 0;
static float gTrailX = 0.0f;
static float gTrailZ = 0.0f;
static bool gTrailHas = false;

static void pushRing(float x, float z, float when, float amp) {
    if (amp < 0.02f) return;
    if (amp > 0.22f) amp = 0.22f;
    gRings[gRingCursor].x = x;
    gRings[gRingCursor].z = z;
    gRings[gRingCursor].birth = when;
    gRings[gRingCursor].amp = amp;
    gRingCursor = (gRingCursor + 1) % 8;
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
        float pa = (i >= 0 && j >= 0 && k >= 0 && i < NX && j < NY && k < NZ && kind[i][j][k] == 1) ? p[i][j][k] : 0.0f;
        float pb = (ni >= 0 && nj >= 0 && nk >= 0 && ni < NX && nj < NY && nk < NZ && kind[ni][nj][nk] == 1) ? p[ni][nj][nk] : 0.0f;
        vel -= pb - pa;
    };
    for (int k = 0; k < NZ; ++k)
        for (int j = 0; j < NY; ++j)
            for (int i = 0; i < NX; ++i) {
                face(i - 1, j, k, i, j, k, vx[i][j][k]);
                face(i, j - 1, k, i, j, k, vy[i][j][k]);
                face(i, j, k - 1, i, j, k, vz[i][j][k]);
            }

    outX = vx[ci + 1][cj][ck] - vx[ci][cj][ck];
    outY = vy[ci][cj + 1][ck];
    if (outY < 0.0f) outY = -outY * 0.35f;
    outZ = vz[ci][cj][ck + 1] - vz[ci][cj][ck];
}

static void pushDrop(float x, float y, float z, float vx, float vy, float vz, float size, float life) {
    if (gDrops.size() >= 220) {
        size_t kill = 0;
        float oldest = -1.0f;
        for (size_t i = 0; i < gDrops.size(); ++i) {
            float t = gDrops[i].age / gDrops[i].life;
            if (t > oldest) { oldest = t; kill = i; }
        }
        gDrops[kill] = { x, y, z, vx, vy, vz, life, 0.0f, size };
        return;
    }
    gDrops.push_back({ x, y, z, vx, vy, vz, life, 0.0f, size });
}

static const int RS = 1;
static const int RN = 64;
static float gHOriginX = 0.0f;
static float gHOriginZ = 0.0f;
static std::vector<float> gRipCur(RN * RN, 0.0f);
static std::vector<float> gRipPrev(RN * RN, 0.0f);
static std::vector<unsigned char> gRipMask(RN * RN, 0);
static unsigned gRipTex = 0;
static float gRipClock = 0.0f;
static float gMaskClock = 1.0f;
static float gRipEnergy = 0.0f;
static bool gRipDirty = true;

static void rippleDisturb(float wx, float wz, float radius, float amount) {
    float cx = wx - gHOriginX;
    float cz = wz - gHOriginZ;
    float r = radius * (float)RS;
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
    for (int z = 0; z < RN; ++z)
        for (int x = 0; x < RN; ++x) {
            int wx = (int)std::floor(gHOriginX) + x;
            int wz = (int)std::floor(gHOriginZ) + z;
            if (waterHasSurface(wx, wz))
                gRipMask[z * RN + x] = 1;
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
            gRipPrev[i] = (sum * 0.5f - gRipPrev[i]) * damp;
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

void fluidSetView(float x, float z) {
    gHOriginX = std::floor(x - (float)RN * 0.5f);
    gHOriginZ = std::floor(z - (float)RN * 0.5f);
}

void fluidHeightOrigin(float& x, float& z) {
    x = gHOriginX;
    z = gHOriginZ;
}

float fluidRippleWorldSize() { return (float)RN; }

static unsigned gHeightTex = 0;
static float gHeightPix[RN * RN * 2];

static const int FN = 32;
static const float FLEN = 16.0f;
static const float FG = 9.81f;
static const float FLAMBDA = -1.0f;
struct FComp { float r, i; };
static FComp gH0[FN * FN];
static FComp gH0c[FN * FN];
static FComp gSpec[FN * FN];
static FComp gDx[FN * FN];
static FComp gDz[FN * FN];
static float gFftPix[FN * FN * 3];
static unsigned gFftTex = 0;
static bool gFftReady = false;
static float gFftClock = 0.0f;
static float gFftTime = 0.0f;
static bool gFftDirty = true;

static FComp fmul(FComp a, FComp b) { return { a.r * b.r - a.i * b.i, a.r * b.i + a.i * b.r }; }
static FComp fadd(FComp a, FComp b) { return { a.r + b.r, a.i + b.i }; }
static FComp fscale(FComp a, float s) { return { a.r * s, a.i * s }; }

static float gauss() {
    float u = rnd();
    if (u < 1.0e-6f) u = 1.0e-6f;
    float v = rnd();
    return std::sqrt(-2.0f * std::log(u)) * std::cos(6.2831853f * v);
}

static float phillips(int n, int m) {
    float kx = 3.14159265f * (2.0f * (float)n - (float)FN) / FLEN;
    float kz = 3.14159265f * (2.0f * (float)m - (float)FN) / FLEN;
    float k2 = kx * kx + kz * kz;
    if (k2 < 1.0e-8f) return 0.0f;
    float klen = std::sqrt(k2);
    float wx = 16.0f, wz = 0.0f;
    float wlen = std::sqrt(wx * wx + wz * wz);
    if (wlen < 0.001f) wlen = 0.001f;
    float kd = (kx / klen) * (wx / wlen) + (kz / klen) * (wz / wlen);
    if (kd < 0.0f) kd = 0.0f;
    float L = wlen * wlen / FG;
    float L2 = L * L;
    float damp = 0.001f;
    float l2 = L2 * damp * damp;
    float A = 0.005f;
    return A * std::exp(-1.0f / (k2 * L2)) / (k2 * k2) * (kd * kd) * std::exp(-k2 * l2);
}

static void fftEvaluate(float t);

static void fft1d(FComp* data, int n, int stride, int offset) {
    int logn = 0;
    while ((1 << logn) < n) ++logn;
    for (int i = 0; i < n; ++i) {
        int rev = 0, x = i;
        for (int b = 0; b < logn; ++b) { rev = (rev << 1) | (x & 1); x >>= 1; }
        if (i < rev) {
            FComp a = data[i * stride + offset];
            data[i * stride + offset] = data[rev * stride + offset];
            data[rev * stride + offset] = a;
        }
    }
    for (int size = 2; size <= n; size <<= 1) {
        float ang = -6.2831853f / (float)size;
        FComp wlen = { std::cos(ang), std::sin(ang) };
        for (int i = 0; i < n; i += size) {
            FComp w = { 1.0f, 0.0f };
            for (int j = 0; j < size / 2; ++j) {
                int i0 = (i + j) * stride + offset;
                int i1 = (i + j + size / 2) * stride + offset;
                FComp u = data[i0];
                FComp v = fmul(data[i1], w);
                data[i0] = fadd(u, v);
                data[i1] = { u.r - v.r, u.i - v.i };
                w = fmul(w, wlen);
            }
        }
    }
}

static void fftInit() {
    if (gFftReady) return;
    for (int m = 0; m < FN; ++m)
        for (int n = 0; n < FN; ++n) {
            int i = m * FN + n;
            int nn = (FN - n) % FN, mm = (FN - m) % FN;
            float p = phillips(n, m);
            float pc = phillips(nn, mm);
            FComp r = { gauss(), gauss() };
            FComp rc = { gauss(), gauss() };
            float s = std::sqrt(p * 0.5f);
            float sc = std::sqrt(pc * 0.5f);
            gH0[i] = { r.r * s, r.i * s };
            gH0c[i] = { rc.r * sc, -rc.i * sc };
        }
    gFftReady = true;
    fftEvaluate(0.0f);
}

static void fftEvaluate(float t) {
    fftInit();
    const float w0 = 6.2831853f / 50.0f;
    for (int m = 0; m < FN; ++m)
        for (int n = 0; n < FN; ++n) {
            int i = m * FN + n;
            float kx = 3.14159265f * (2.0f * (float)n - (float)FN) / FLEN;
            float kz = 3.14159265f * (2.0f * (float)m - (float)FN) / FLEN;
            float klen = std::sqrt(kx * kx + kz * kz);
            float omega = std::floor(std::sqrt(FG * klen) / w0) * w0;
            float c = std::cos(omega * t), s = std::sin(omega * t);
            FComp c0 = { c, s }, c1 = { c, -s };
            FComp h = fadd(fmul(gH0[i], c0), fmul(gH0c[i], c1));
            gSpec[i] = h;
            if (klen < 1.0e-6f) {
                gDx[i] = { 0, 0 };
                gDz[i] = { 0, 0 };
            }
            else {
                gDx[i] = fmul(h, FComp{ 0.0f, -kx / klen });
                gDz[i] = fmul(h, FComp{ 0.0f, -kz / klen });
            }
        }
    for (int m = 0; m < FN; ++m) {
        fft1d(gSpec, FN, 1, m * FN);
        fft1d(gDx, FN, 1, m * FN);
        fft1d(gDz, FN, 1, m * FN);
    }
    for (int n = 0; n < FN; ++n) {
        fft1d(gSpec, FN, FN, n);
        fft1d(gDx, FN, FN, n);
        fft1d(gDz, FN, FN, n);
    }
    for (int m = 0; m < FN; ++m)
        for (int n = 0; n < FN; ++n) {
            int i = m * FN + n;
            float sign = ((n + m) & 1) ? -1.0f : 1.0f;
            gFftPix[i * 3 + 0] = gSpec[i].r * sign * 0.38f;
            gFftPix[i * 3 + 1] = gDx[i].r * sign * FLAMBDA * 0.30f;
            gFftPix[i * 3 + 2] = gDz[i].r * sign * FLAMBDA * 0.30f;
        }
    gFftDirty = true;
}

void fluidBindFft(int unit) {
    glx.ActiveTexture(GL_TEXTURE0 + unit);
    if (!gFftTex) {
        glGenTextures(1, &gFftTex);
        glBindTexture(GL_TEXTURE_2D, gFftTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, FN, FN, 0, GL_RGB, GL_FLOAT, gFftPix);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        gFftDirty = false;
    }
    else {
        glBindTexture(GL_TEXTURE_2D, gFftTex);
        if (gFftDirty) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, FN, FN, GL_RGB, GL_FLOAT, gFftPix);
            gFftDirty = false;
        }
    }
    glx.ActiveTexture(GL_TEXTURE0);
}

void fluidBindHeight(int unit) {
    static int skip = 0;
    if (gHeightTex && (++skip & 1)) {
        glx.ActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, gHeightTex);
        glx.ActiveTexture(GL_TEXTURE0);
        return;
    }
    for (int z = 0; z < RN; ++z)
        for (int x = 0; x < RN; ++x) {
            int wx = (int)std::floor(gHOriginX) + x;
            int wz = (int)std::floor(gHOriginZ) + z;
            int i = z * RN + x;
            bool wet = waterHasSurface(wx, wz);
            gHeightPix[i * 2 + 0] = wet ? waterSurfaceY(wx, wz) : 0.0f;
            gHeightPix[i * 2 + 1] = wet ? 1.0f : 0.0f;
        }
    glx.ActiveTexture(GL_TEXTURE0 + unit);
    if (!gHeightTex) {
        glGenTextures(1, &gHeightTex);
        glBindTexture(GL_TEXTURE_2D, gHeightTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, RN, RN, 0, GL_RG, GL_FLOAT, gHeightPix);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else {
        glBindTexture(GL_TEXTURE_2D, gHeightTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, RN, RN, GL_RG, GL_FLOAT, gHeightPix);
    }
    glx.ActiveTexture(GL_TEXTURE0);
}

void fluidSetClock(float time) { gClock = time; }

void fluidWake(float x, float z, float vx, float vz) {
    float sp = std::sqrt(vx * vx + vz * vz);
    if (sp < 0.55f) return;
    rippleDisturb(x, z, 0.35f + sp * 0.04f, 0.03f + sp * 0.006f);
    float dx = x - gTrailX;
    float dz = z - gTrailZ;
    if (!gTrailHas || dx * dx + dz * dz > 0.42f * 0.42f) {
        Trail& t = gTrail[gTrailCursor];
        t.x = x;
        t.z = z;
        t.birth = gClock;
        t.hx = vx / sp;
        t.hz = vz / sp;
        gTrailCursor = (gTrailCursor + 1) % 16;
        if (gTrailCount < 16) gTrailCount++;
        gTrailX = x;
        gTrailZ = z;
        gTrailHas = true;
    }
    static float dropAcc = 0.0f;
    dropAcc += sp * 0.02f;
    if (dropAcc > 1.0f) {
        dropAcc = 0.0f;
        float ang = rnd() * 6.2831853f;
        pushDrop(x + (rnd() - 0.5f) * 0.2f, waterSurfaceY((int)std::floor(x), (int)std::floor(z)) + 0.05f, z + (rnd() - 0.5f) * 0.2f,
            vx * 0.15f + std::cos(ang) * 0.8f, 1.1f + rnd() * 1.4f, vz * 0.15f + std::sin(ang) * 0.8f,
            0.022f + rnd() * 0.03f, 0.35f + rnd() * 0.3f);
    }
}

void fluidImpact(float x, float y, float z, float speed, float mass) {
    if (speed < 0.0f) speed = -speed;
    if (speed < 1.2f || mass <= 0.0f) return;
    float energy = 0.5f * mass * speed * speed;
    float sink = std::sqrt(energy) * 0.008f;
    if (sink > 1.0f) sink = 1.0f;
    float radius = 0.28f + std::sqrt(mass) * 0.04f;
    if (radius > 0.85f) radius = 0.85f;
    rippleDisturb(x, z, radius, sink);
    float amp = 0.07f + speed * 0.008f;
    if (amp > 0.18f) amp = 0.18f;
    pushRing(x, z, gClock, amp);
    for (int i = 0; i < 3; ++i) {
        float ang = rnd() * 6.2831853f;
        float rad = 0.25f + rnd() * 0.7f;
        pushRing(x + std::cos(ang) * rad, z + std::sin(ang) * rad, gClock + 0.06f + rnd() * 0.12f, amp * 0.42f);
    }

    int count = (int)(energy / 160.0f);
    if (count < 6) count = 6;
    if (count > 56) count = 56;
    float kick = speed * 0.42f;
    if (kick > 9.0f) kick = 9.0f;

    float gx, gy, gz;
    projectSplash(x, y, z, speed, gx, gy, gz);

    for (int i = 0; i < count; ++i) {
        float ang = rnd() * 6.2831853f;
        float rad = 0.12f + rnd() * 0.55f;
        float up = 0.40f + rnd() * 0.95f;
        float px = x + std::cos(ang) * rad * 0.28f;
        float pz = z + std::sin(ang) * rad * 0.28f;
        float py = y + rnd() * 0.10f;
        float svx = std::cos(ang) * kick * (0.35f + rnd() * 0.70f) + gx * 0.45f;
        float svz = std::sin(ang) * kick * (0.35f + rnd() * 0.70f) + gz * 0.45f;
        float svy = up * kick * 0.90f + gy * 0.5f;
        float size = 0.032f + rnd() * 0.05f + speed * 0.0014f;
        if (size > 0.13f) size = 0.13f;
        pushDrop(px, py, pz, svx, svy, svz, size, 0.50f + rnd() * 0.75f);
    }

    int crown = 6 + count / 8;
    if (crown > 14) crown = 14;
    for (int i = 0; i < crown; ++i) {
        float ang = (float)i * (6.2831853f / (float)crown) + rnd() * 0.2f;
        float svx = std::cos(ang) * kick * 0.85f;
        float svz = std::sin(ang) * kick * 0.85f;
        pushDrop(x + std::cos(ang) * 0.18f, y + 0.06f, z + std::sin(ang) * 0.18f,
            svx, kick * 0.55f + 1.2f, svz, 0.028f + rnd() * 0.03f, 0.4f + rnd() * 0.35f);
    }
}

static bool gAllowHeavy = true;

void fluidSetHeavy(bool enabled) { gAllowHeavy = enabled; }

void fluidTick(float dt) {
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;

    size_t w = 0;
    for (size_t i = 0; i < gDrops.size(); ++i) {
        Drop d = gDrops[i];
        d.age += dt;
        if (d.age >= d.life) continue;
        d.vy -= 18.0f * dt;
        d.vx *= (1.0f - 1.6f * dt);
        d.vz *= (1.0f - 1.6f * dt);
        float nx = d.x + d.vx * dt;
        float ny = d.y + d.vy * dt;
        float nz = d.z + d.vz * dt;
        int ix = (int)std::floor(nx);
        int iy = (int)std::floor(ny);
        int iz = (int)std::floor(nz);
        if (solid(ix, iy, iz)) {
            d.vy = -d.vy * 0.28f;
            d.vx *= 0.4f;
            d.vz *= 0.4f;
            if (d.vy < 0.8f && d.vy > -0.8f) continue;
        }
        else if (blockAt(ix, iy, iz) == Block::Water && d.age > 0.08f) {
            rippleDisturb(nx, nz, 0.16f, 0.04f);
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

    gFftTime += dt;
    gFftClock += dt;
    if (gAllowHeavy && gFftClock >= 1.0f / 12.0f) {
        gFftClock = 0.0f;
        bool anyWater = false;
        for (int i = 0; i < RN * RN && !anyWater; ++i)
            if (gRipMask[i]) anyWater = true;
        if (anyWater) fftEvaluate(gFftTime);
    }

    gMaskClock += dt;
    if (gMaskClock > 0.5f) {
        gMaskClock = 0.0f;
        rebuildMask();
    }
    if (gRipEnergy <= 0.0f) return;
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
        float s = d.size * (0.7f + 0.3f * fade);
        glm::vec3 c(d.x, d.y, d.z);
        glm::vec3 r = right * s;
        glm::vec3 u = up * s;
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

void fluidBindMotion(unsigned program) {
    float rings[8 * 4] = {};
    for (int i = 0; i < 8; ++i) {
        float age = gClock - gRings[i].birth;
        bool live = gRings[i].amp > 0.001f && age > -0.2f && age < 5.0f;
        rings[i * 4 + 0] = gRings[i].x;
        rings[i * 4 + 1] = gRings[i].z;
        rings[i * 4 + 2] = gRings[i].birth;
        rings[i * 4 + 3] = live ? gRings[i].amp : 0.0f;
    }
    float wake[16 * 4] = {};
    float dir[16 * 4] = {};
    int n = 0;
    for (int i = 0; i < gTrailCount && n < 16; ++i) {
        const Trail& t = gTrail[i];
        float age = gClock - t.birth;
        if (age < 0.0f || age > 4.4f) continue;
        wake[n * 4 + 0] = t.x;
        wake[n * 4 + 1] = t.z;
        wake[n * 4 + 2] = t.birth;
        wake[n * 4 + 3] = 1.0f;
        dir[n * 4 + 0] = t.hx;
        dir[n * 4 + 1] = t.hz;
        ++n;
    }
    auto loc = [&](const char* name) { return glx.GetUniformLocation(program, name); };
    int r = loc("uRings");
    int w = loc("uWake");
    int d = loc("uWakeDir");
    if (r >= 0) glx.Uniform4fv(r, 8, rings);
    if (w >= 0) glx.Uniform4fv(w, 16, wake);
    if (d >= 0) glx.Uniform4fv(d, 16, dir);
}
