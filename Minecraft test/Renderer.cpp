#include "GlIncludes.h"
#include "GlExt.h"
#include "Renderer.h"
#include "Camera.h"
#include "FluidSolver.h"
#include "World.h"
#include "BlockShader.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <vector>

struct MeshVert {
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b, a;
    float t0x, t0y, t0z, t0w;
    float t1x, t1y, t1z, t1w;
    float t2x, t2y, t2z, t2w;
};

struct MeshBatch {
    unsigned tex = 0;
    unsigned vbo = 0;
    unsigned vao = 0;
    int count = 0;
    std::vector<MeshVert> v;
    void release() {
        if (vao && glx.DeleteVertexArrays) glx.DeleteVertexArrays(1, &vao);
        if (vbo && glx.DeleteBuffers) glx.DeleteBuffers(1, &vbo);
        vao = 0;
        vbo = 0;
    }
    MeshBatch() = default;
    MeshBatch(const MeshBatch&) = delete;
    MeshBatch& operator=(const MeshBatch&) = delete;
    MeshBatch(MeshBatch&& o) noexcept
        : tex(o.tex), vbo(o.vbo), vao(o.vao), count(o.count), v(std::move(o.v)) {
        o.vbo = 0;
        o.vao = 0;
        o.count = 0;
    }
    MeshBatch& operator=(MeshBatch&& o) noexcept {
        if (this != &o) {
            release();
            tex = o.tex;
            vbo = o.vbo;
            vao = o.vao;
            count = o.count;
            v = std::move(o.v);
            o.vbo = 0;
            o.vao = 0;
            o.count = 0;
        }
        return *this;
    }
    ~MeshBatch() { release(); }
};

static std::vector<MeshBatch>* gCap = nullptr;
static MeshBatch* gCapWater = nullptr;

static void pushMesh(unsigned tex, const glm::vec3& p, const glm::vec3& n, float r, float g, float b, float a,
    float t0x, float t0y, float t0z, float t0w,
    float t1x, float t1y, float t1z, float t1w,
    float t2x, float t2y, float t2z, float t2w) {
    MeshVert m{ p.x, p.y, p.z, n.x, n.y, n.z, r, g, b, a, t0x, t0y, t0z, t0w, t1x, t1y, t1z, t1w, t2x, t2y, t2z, t2w };
    if (gCapWater) {
        gCapWater->v.push_back(m);
        return;
    }
    for (MeshBatch& batch : *gCap) {
        if (batch.tex == tex) {
            batch.v.push_back(m);
            return;
        }
    }
    MeshBatch created;
    created.tex = tex;
    created.v.push_back(m);
    gCap->push_back(std::move(created));
}

static BlockTextures gBlockTex;

void setBlockTextures(const BlockTextures& textures) {
    gBlockTex = textures;
}

static void bindAlbedo(unsigned tex) {
    glx.ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex);
    glx.ActiveTexture(GL_TEXTURE0);
}

void setup3D(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(cameraFov(), (double)w / h, 0.12, 360.0);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
}

static void emitBox(int bx, int by, int bz, float hs, bool shaded) {
    Block self = blockAt(bx, by, bz);
    auto hidden = [&](int nx, int ny, int nz) {
        Block nb = blockAt(bx + nx, by + ny, bz + nz);
        if (self == Block::Water) return nb == Block::Water;
        if (self == Block::Torch) return false;
        if (self == Block::Leaves) return nb == Block::Leaves;
        if (nb == Block::Leaves) return false;
        return solid(bx + nx, by + ny, bz + nz);
    };

    float x = bx + 0.5f, y = by + 0.5f, z = bz + 0.5f;
    unsigned topTex = 0, sideTex = 0, bottomTex = 0;
    if (self == Block::Grass) {
        topTex = gBlockTex.grassTop;
        sideTex = gBlockTex.grassSide;
        bottomTex = gBlockTex.dirt;
    }
    else if (self == Block::Dirt) {
        topTex = sideTex = bottomTex = gBlockTex.dirt;
    }
    else if (self == Block::Stone) {
        topTex = sideTex = bottomTex = gBlockTex.stone;
    }
    else if (self == Block::Log) {
        topTex = bottomTex = gBlockTex.logTop;
        sideTex = gBlockTex.logSide;
    }
    else if (self == Block::Leaves) {
        topTex = sideTex = bottomTex = gBlockTex.leaves;
    }

    auto face = [&](const glm::vec3& normal, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3, glm::vec3 v4,
        unsigned tex, glm::vec2 t1, glm::vec2 t2, glm::vec2 t3, glm::vec2 t4) {
        if (hidden((int)normal.x, (int)normal.y, (int)normal.z)) return;
        if (self == Block::Log) {
            int axis = blockMeta(bx, by, bz);
            bool cap = (axis == 1) ? std::abs(normal.x) > 0.5f : (axis == 2) ? std::abs(normal.z) > 0.5f : std::abs(normal.y) > 0.5f;
            tex = cap ? gBlockTex.logTop : gBlockTex.logSide;
            if (!cap) {
                auto bark = [&](const glm::vec3& p) {
                    float lx = p.x - (x - hs);
                    float ly = p.y - (y - hs);
                    float lz = p.z - (z - hs);
                    float u, v;
                    if (axis == 1) { v = lx; u = std::abs(normal.y) > 0.5f ? lz : ly; }
                    else if (axis == 2) { v = lz; u = std::abs(normal.y) > 0.5f ? lx : ly; }
                    else { v = ly; u = std::abs(normal.x) > 0.5f ? lz : lx; }
                    return glm::vec2(u, v);
                };
                t1 = bark(v1); t2 = bark(v2); t3 = bark(v3); t4 = bark(v4);
            }
        }
        if (shaded) {
            FaceLight lit = evaluateFace(bx, by, bz, normal);
            float useTex = tex ? 1.0f : 0.0f;
            float wind = 0.0f;
            if (self == Block::Leaves) {
                int cover = 0;
                const int d[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
                for (int i = 0; i < 6; ++i)
                    if (blockAt(bx + d[i][0], by + d[i][1], bz + d[i][2]) == Block::Leaves) ++cover;
                wind = 1.0f - cover * 0.12f;
                if (wind < 0.28f) wind = 0.28f;
            }
            float leaf = (self == Block::Leaves) ? 1.0f : 0.0f;
            auto put = [&](const glm::vec3& v, const glm::vec2& uv) {
                if (gCap) {
                    pushMesh(tex, v, normal, lit.albedo.r, lit.albedo.g, lit.albedo.b, lit.blockLight,
                        lit.gi, lit.emission, lit.caustic, lit.ao, uv.x, uv.y, useTex, leaf, x, y, z, wind);
                    return;
                }
                glx.MultiTexCoord4f(GL_TEXTURE0, lit.gi, lit.emission, lit.caustic, lit.ao);
                glx.MultiTexCoord4f(GL_TEXTURE1, uv.x, uv.y, useTex, leaf);
                glx.MultiTexCoord4f(GL_TEXTURE2, x, y, z, wind);
                glVertex3f(v.x, v.y, v.z);
            };
            if (!gCap) {
                bindAlbedo(tex);
                glBegin(GL_QUADS);
                glColor4f(lit.albedo.r, lit.albedo.g, lit.albedo.b, lit.blockLight);
                glNormal3f(normal.x, normal.y, normal.z);
            }
            put(v1, t1);
            put(v2, t2);
            put(v3, t3);
            put(v4, t4);
            if (!gCap) glEnd();
        }
        else {
            glBegin(GL_QUADS);
            glNormal3f(normal.x, normal.y, normal.z);
            glVertex3f(v1.x, v1.y, v1.z);
            glVertex3f(v2.x, v2.y, v2.z);
            glVertex3f(v3.x, v3.y, v3.z);
            glVertex3f(v4.x, v4.y, v4.z);
            glEnd();
        }
    };

    const glm::vec2 uv00(0, 0), uv10(1, 0), uv11(1, 1), uv01(0, 1);
    // Cada cara va en sentido antihorario vista desde afuera.
    face(glm::vec3(0, 1, 0), { x - hs, y + hs, z - hs }, { x - hs, y + hs, z + hs }, { x + hs, y + hs, z + hs }, { x + hs, y + hs, z - hs }, topTex, uv00, uv01, uv11, uv10);
    face(glm::vec3(0, -1, 0), { x - hs, y - hs, z - hs }, { x + hs, y - hs, z - hs }, { x + hs, y - hs, z + hs }, { x - hs, y - hs, z + hs }, bottomTex, uv00, uv10, uv11, uv01);
    face(glm::vec3(0, 0, 1), { x - hs, y - hs, z + hs }, { x + hs, y - hs, z + hs }, { x + hs, y + hs, z + hs }, { x - hs, y + hs, z + hs }, sideTex, uv00, uv10, uv11, uv01);
    face(glm::vec3(0, 0, -1), { x + hs, y - hs, z - hs }, { x - hs, y - hs, z - hs }, { x - hs, y + hs, z - hs }, { x + hs, y + hs, z - hs }, sideTex, uv00, uv10, uv11, uv01);
    face(glm::vec3(1, 0, 0), { x + hs, y - hs, z + hs }, { x + hs, y - hs, z - hs }, { x + hs, y + hs, z - hs }, { x + hs, y + hs, z + hs }, sideTex, uv00, uv10, uv11, uv01);
    face(glm::vec3(-1, 0, 0), { x - hs, y - hs, z - hs }, { x - hs, y - hs, z + hs }, { x - hs, y + hs, z + hs }, { x - hs, y + hs, z - hs }, sideTex, uv00, uv10, uv11, uv01);
}

void drawCubeOutline(float cx, float cy, float cz, float size) {
    float hs = size * 0.5f;
    glm::vec3 corners[8] = {
        {cx - hs, cy + hs, cz - hs}, {cx + hs, cy + hs, cz - hs},
        {cx + hs, cy + hs, cz + hs}, {cx - hs, cy + hs, cz + hs},
        {cx - hs, cy - hs, cz - hs}, {cx + hs, cy - hs, cz - hs},
        {cx + hs, cy - hs, cz + hs}, {cx - hs, cy - hs, cz + hs}
    };
    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    glColor3f(1.0f, 0.5f, 0.0f);
    glLineWidth(4.0f);
    glBegin(GL_LINES);
    for (int i = 0; i < 12; ++i) {
        glVertex3fv(&corners[edges[i][0]].x);
        glVertex3fv(&corners[edges[i][1]].x);
    }
    glEnd();
    glLineWidth(1.0f);
}

void drawCrosshair(int w, int h) {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glColor3f(1, 1, 1);
    glLineWidth(1.0f);
    int cx = w / 2, cy = h / 2, s = 5;
    glBegin(GL_LINES);
    glVertex2i(cx, cy - s); glVertex2i(cx + s, cy);
    glVertex2i(cx + s, cy); glVertex2i(cx, cy + s);
    glVertex2i(cx, cy + s); glVertex2i(cx - s, cy);
    glVertex2i(cx - s, cy); glVertex2i(cx, cy - s);
    glEnd();
    glEnable(GL_DEPTH_TEST);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void drawBillboardTextured(const glm::vec3& pos, const glm::vec3& viewDir, float radius, unsigned int tex, float brightness) {
    if (tex == 0) return;
    glm::vec3 upWorld(0.0f, 1.0f, 0.0f);
    glm::vec3 dir = glm::normalize(viewDir);
    glm::vec3 right = glm::normalize(glm::cross(dir, upWorld));
    glm::vec3 upVec = glm::normalize(glm::cross(right, dir));
    glm::vec3 p1 = pos - right * radius - upVec * radius;
    glm::vec3 p2 = pos + right * radius - upVec * radius;
    glm::vec3 p3 = pos + right * radius + upVec * radius;
    glm::vec3 p4 = pos - right * radius + upVec * radius;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBegin(GL_QUADS);
    glColor4f(brightness, brightness, brightness, 1);
    glTexCoord2f(0, 0); glVertex3fv(&p1.x);
    glTexCoord2f(1, 0); glVertex3fv(&p2.x);
    glTexCoord2f(1, 1); glVertex3fv(&p3.x);
    glTexCoord2f(0, 1); glVertex3fv(&p4.x);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

static float cornerVis(int vx, int vz) {
    float s = 0.0f;
    int n = 0;
    for (int ox = -1; ox <= 0; ++ox)
        for (int oz = -1; oz <= 0; ++oz) {
            int cx = vx + ox, cz = vz + oz;
            if (!waterHasSurface(cx, cz)) continue;
            s += waterSurfaceY(cx, cz);
            ++n;
        }
    if (!n) return 0.0f;
    return s / (float)n;
}

static void sampleFlowXZ(int x, int z, float& fx, float& fz, float& down) {
    fx = fz = down = 0.0f;
    float h = waterSurfaceY(x, z);
    int y = (int)std::floor(h - 0.001f);
    if (y < 0) y = 0;
    waterFlow(x, y, z, fx, fz, down);
}

static void pushWater(const glm::vec3& p, const glm::vec3& n, float fx, float fz, float kind, float strength, float across, float lipY, float wave) {
    if (gCapWater) {
        pushMesh(0, p, n, 1, 1, 1, 1, fx, fz, kind, strength, across, lipY, 0, 0, wave, 0, 0, 0);
        return;
    }
    glNormal3f(n.x, n.y, n.z);
    glTexCoord4f(fx, fz, kind, strength);
    glx.MultiTexCoord4f(GL_TEXTURE1, across, lipY, 0, 0);
    glx.MultiTexCoord4f(GL_TEXTURE2, wave, 0, 0, 0);
    glVertex3f(p.x, p.y, p.z);
}

static bool cellOpen(int x, int y, int z) {
    Block b = blockAt(x, y, z);
    return b == Block::Air || b == Block::Torch || isPlant(b);
}

static void drawWaterFace(float x0, float z0, float x1, float z1, float y0, float y1,
    float fx, float fz, float kind, float strength, float lipY, float waveLo, float waveHi) {
    glm::vec3 edge(x1 - x0, 0.0f, z1 - z0);
    glm::vec3 n = glm::cross(edge, glm::vec3(0, 1, 0));
    float len = glm::length(n);
    if (len < 0.0001f) n = glm::vec3(1, 0, 0);
    else n /= len;
    pushWater(glm::vec3(x0, y0, z0), n, fx, fz, kind, strength, 0, lipY, waveLo);
    pushWater(glm::vec3(x1, y0, z1), n, fx, fz, kind, strength, 1, lipY, waveLo);
    pushWater(glm::vec3(x1, y1, z1), n, fx, fz, kind, strength, 1, lipY, waveHi);
    pushWater(glm::vec3(x0, y1, z0), n, fx, fz, kind, strength, 0, lipY, waveHi);
}

static float clampCeil(int x, int z, float h) {
    int iy = (int)std::floor(h);
    if (iy + 1 < HEIGHT && solid(x, iy + 1, z) && h > (float)iy + 0.97f)
        return (float)iy + 0.97f;
    return h;
}

static float fieldH(float x, float z) {
    int x0 = (int)std::floor(x), z0 = (int)std::floor(z);
    float tx = x - (float)x0, tz = z - (float)z0;
    auto at = [](int ix, int iz) {
        if (!waterHasSurface(ix, iz)) return -1.0f;
        return waterSurfaceY(ix, iz);
    };
    float h00 = at(x0, z0), h10 = at(x0 + 1, z0), h01 = at(x0, z0 + 1), h11 = at(x0 + 1, z0 + 1);
    float acc = 0.0f, w = 0.0f;
    auto add = [&](float h, float wt) {
        if (h < 0.0f) return;
        acc += h * wt;
        w += wt;
    };
    add(h00, (1.0f - tx) * (1.0f - tz));
    add(h10, tx * (1.0f - tz));
    add(h01, (1.0f - tx) * tz);
    add(h11, tx * tz);
    if (w < 0.001f) return 0.0f;
    return acc / w;
}

static bool clipWaterVert(float& x, float& y, float& z) {
    const float e = 0.045f;
    auto blocked = [](float px, float py, float pz) {
        return solid((int)std::floor(px), (int)std::floor(py), (int)std::floor(pz));
    };
    if (blocked(x - e, y, z)) x += e;
    if (blocked(x + e, y, z)) x -= e;
    if (blocked(x, y, z - e)) z += e;
    if (blocked(x, y, z + e)) z -= e;
    int iy = (int)std::floor(y);
    if (solid((int)std::floor(x), iy + 1, (int)std::floor(z)) && y > (float)iy + 0.96f)
        y = (float)iy + 0.96f;
    return !blocked(x, y, z);
}

static bool waterPoint(float x, float z) {
    int ix = (int)std::floor(x);
    int iz = (int)std::floor(z);
    if (waterHasSurface(ix, iz)) return !solid(ix, (int)std::floor(waterSurfaceY(ix, iz)), iz);
    return false;
}

static float waterBodyBottom(int x, int z, float topH) {
    int y = (int)std::floor(topH - 0.001f);
    if (y >= HEIGHT) y = HEIGHT - 1;
    if (y < 0) return 0.0f;
    while (y > 0 && blockAt(x, y - 1, z) == Block::Water) --y;
    return (float)y;
}

static void waterSideQuad(int x, int z, int dx, int dz, float y0, float y1, float kind, float waveLo, float waveHi) {
    if (y1 <= y0 + 0.08f) return;
    float x0, z0, x1, z1;
    if (dx == 1) { x0 = (float)x + 1.0f; z0 = (float)z + 1.0f; x1 = x0; z1 = (float)z; }
    else if (dx == -1) { x0 = (float)x; z0 = (float)z; x1 = x0; z1 = (float)z + 1.0f; }
    else if (dz == 1) { x0 = (float)x; z0 = (float)z + 1.0f; x1 = (float)x + 1.0f; z1 = z0; }
    else { x0 = (float)x + 1.0f; z0 = (float)z; x1 = (float)x; z1 = z0; }
    drawWaterFace(x0, z0, x1, z1, y0, y1, (float)dx, (float)dz, kind, 1.0f, 0.0f, waveLo, waveHi);
}

static int waterDepthAt(int x, int z) {
    if (!waterHasSurface(x, z)) return 0;
    int y = (int)std::floor(waterSurfaceY(x, z) - 0.001f);
    int d = 0;
    while (y >= 0 && d < 6 && blockAt(x, y, z) == Block::Water) {
        ++d;
        --y;
    }
    return d;
}

static float seaWaveAt(int x, int z) {
    if (waterDepthAt(x, z) < 4) return 0.0f;
    const int off[8][2] = { { 10, 0 }, { -10, 0 }, { 0, 10 }, { 0, -10 }, { 8, 8 }, { -8, 8 }, { 8, -8 }, { -8, -8 } };
    int wet = 0;
    for (int i = 0; i < 8; ++i)
        if (waterDepthAt(x + off[i][0], z + off[i][1]) >= 4) ++wet;
    if (wet < 6) return 0.0f;
    if (!waterHasSurface(x + 1, z) || !waterHasSurface(x - 1, z)
        || !waterHasSurface(x, z + 1) || !waterHasSurface(x, z - 1))
        return 0.22f;
    return 1.0f;
}

static void emitExposedSides(int x, int z, int dx, int dz, float y0, float y1, float kind, float seaHere) {
    if (y1 <= y0 + 0.08f) return;
    int nx = x + dx, nz = z + dz;
    bool hasN = waterHasSurface(nx, nz);
    float nh = hasN ? waterSurfaceY(nx, nz) : 0.0f;
    int a = (int)std::floor(y0);
    int b = (int)std::floor(y1 - 0.001f);
    if (a < 0) a = 0;
    if (b >= HEIGHT) b = HEIGHT - 1;
    float run0 = -1.0f;
    auto flush = [&](float run1) {
        if (run0 < 0.0f) return;
        float lo = run0 < y0 ? y0 : run0;
        float hi = run1 > y1 ? y1 : run1;
        float waveHi = kind < 0.5f ? seaHere : 0.0f;
        float waveLo = 0.0f;
        if (waveHi > 0.05f && hasN && lo < nh + 0.12f && lo > nh - 0.12f)
            waveLo = seaWaveAt(nx, nz);
        waterSideQuad(x, z, dx, dz, lo, hi, kind, waveLo, waveHi);
        run0 = -1.0f;
    };
    for (int y = a; y <= b; ++y) {
        if (y < 0 || solid(nx, y, nz)) { flush((float)y); continue; }
        if (blockAt(nx, y, nz) == Block::Water && hasN && (float)(y + 1) <= nh + 0.02f) {
            flush((float)y);
            continue;
        }
        if (run0 < 0.0f) {
            run0 = (float)y;
            if (hasN && nh > run0 && nh < (float)(y + 1)) run0 = nh;
        }
    }
    flush((float)(b + 1));
}

static float columnLanding(int x, int z, float fromY) {
    int y = (int)std::floor(fromY - 0.02f);
    if (y >= HEIGHT) y = HEIGHT - 1;
    int limit = y - 48;
    if (limit < -1) limit = -1;
    for (; y > limit; --y) {
        if (y < 0) return 0.0f;
        if (solid(x, y, z)) return (float)(y + 1);
        if (blockAt(x, y, z) == Block::Water) {
            if (waterHasSurface(x, z)) {
                float nh = waterSurfaceY(x, z);
                if (nh < fromY) return nh;
            }
            return (float)(y + 1);
        }
    }
    return (float)(limit + 1);
}

static void emitWaterVolume(int x, int z, float h, float seaHere) {
    float bot = waterBodyBottom(x, z, h);
    if (h < bot + 0.04f) return;
    const int dir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    int below = (int)std::floor(bot - 0.02f);
    bool openBelow = below < 0 || (!solid(x, below, z) && blockAt(x, below, z) != Block::Water);
    if (openBelow && h > bot + 0.08f) {
        float y = bot;
        float x0 = (float)x, z0 = (float)z;
        glm::vec3 n(0.0f, -1.0f, 0.0f);
        pushWater(glm::vec3(x0, y, z0), n, 0, 0, 0, 1, 0, 0, 0);
        pushWater(glm::vec3(x0 + 1.0f, y, z0), n, 0, 0, 0, 1, 0, 0, 0);
        pushWater(glm::vec3(x0 + 1.0f, y, z0 + 1.0f), n, 0, 0, 0, 1, 0, 0, 0);
        pushWater(glm::vec3(x0, y, z0 + 1.0f), n, 0, 0, 0, 1, 0, 0, 0);
        float land = columnLanding(x, z, bot);
        if (bot - land > 0.75f) {
            for (int d = 0; d < 4; ++d)
                emitExposedSides(x, z, dir[d][0], dir[d][1], land, bot, 1.0f, 0.0f);
        }
    }
    for (int d = 0; d < 4; ++d) {
        int nx = x + dir[d][0], nz = z + dir[d][1];
        emitExposedSides(x, z, dir[d][0], dir[d][1], bot, h, 0.0f, seaHere);
        if (openBelow) continue;
        int probe = (int)std::floor(bot - 0.02f);
        if (probe < 0 || probe >= HEIGHT) continue;
        if (solid(nx, probe, nz) || blockAt(nx, probe, nz) == Block::Water) continue;
        float land = columnLanding(nx, nz, bot);
        if (bot - land > 0.75f)
            emitExposedSides(x, z, dir[d][0], dir[d][1], land, bot, 1.0f, 0.0f);
    }
}

static float shoreFoam(float x, float z) {
    if (!waterHasSurface((int)std::floor(x - 0.12f), (int)std::floor(z))) return 1.0f;
    if (!waterHasSurface((int)std::floor(x + 0.12f), (int)std::floor(z))) return 1.0f;
    if (!waterHasSurface((int)std::floor(x), (int)std::floor(z - 0.12f))) return 1.0f;
    if (!waterHasSurface((int)std::floor(x), (int)std::floor(z + 0.12f))) return 1.0f;
    return 0.0f;
}

static void drawCutoutQuad(int bx, int by, int bz, unsigned tex, const glm::vec3& n,
    const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
    glm::vec2 ta, glm::vec2 tb, glm::vec2 tc, glm::vec2 td, float wind) {
    FaceLight lit = evaluateFace(bx, by, bz, n);
    glm::vec3 center(bx + 0.5f, by + 0.5f, bz + 0.5f);
    auto put = [&](const glm::vec3& p, const glm::vec2& uv) {
        if (gCap) {
            pushMesh(tex, p, n, lit.albedo.r, lit.albedo.g, lit.albedo.b, lit.blockLight,
                lit.gi, lit.emission, lit.caustic, lit.ao, uv.x, uv.y, tex ? 1.0f : 0.0f, 0.0f,
                center.x, center.y, center.z, wind);
            return;
        }
        glx.MultiTexCoord4f(GL_TEXTURE0, lit.gi, lit.emission, lit.caustic, lit.ao);
        glx.MultiTexCoord4f(GL_TEXTURE1, uv.x, uv.y, tex ? 1.0f : 0.0f, 0.0f);
        glx.MultiTexCoord4f(GL_TEXTURE2, center.x, center.y, center.z, wind);
        glVertex3f(p.x, p.y, p.z);
    };
    if (!gCap) {
        bindAlbedo(tex);
        glBegin(GL_QUADS);
        glColor4f(lit.albedo.r, lit.albedo.g, lit.albedo.b, lit.blockLight);
        glNormal3f(n.x, n.y, n.z);
    }
    put(a, ta);
    put(b, tb);
    put(c, tc);
    put(d, td);
    if (!gCap) glEnd();
}

static void drawPlant(int bx, int by, int bz) {
    Block b = blockAt(bx, by, bz);
    unsigned tex = gBlockTex.tallGrass;
    if (b == Block::Poppy) tex = gBlockTex.poppy;
    else if (b == Block::Triteleia) tex = gBlockTex.triteleia;
    else if (b == Block::TriteleiaRed) tex = gBlockTex.triteleiaRed;
    float x0 = (float)bx, x1 = x0 + 1.0f;
    float y0 = (float)by, y1 = y0 + 1.0f;
    float z0 = (float)bz, z1 = z0 + 1.0f;
    glm::vec3 up(0, 1, 0);
    glm::vec2 uv00(0, 0), uv10(1, 0), uv11(1, 1), uv01(0, 1);
    drawCutoutQuad(bx, by, bz, tex, up, { x0, y0, z0 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z0 }, uv00, uv10, uv11, uv01, 1.7f);
    drawCutoutQuad(bx, by, bz, tex, up, { x1, y0, z1 }, { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z1 }, uv00, uv10, uv11, uv01, 1.7f);
    drawCutoutQuad(bx, by, bz, tex, up, { x1, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x1, y1, z0 }, uv00, uv10, uv11, uv01, 1.7f);
    drawCutoutQuad(bx, by, bz, tex, up, { x0, y0, z1 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z1 }, uv00, uv10, uv11, uv01, 1.7f);
}

static void drawTorch(int bx, int by, int bz) {
    int facing = blockMeta(bx, by, bz);
    glm::vec3 axis(0, 1, 0);
    glm::vec3 base(bx + 0.5f, (float)by, bz + 0.5f);
    if (facing == 1) {
        axis = glm::normalize(glm::vec3(1, 1, 0));
        base = glm::vec3((float)bx, by + 0.22f, bz + 0.5f);
    }
    else if (facing == 2) {
        axis = glm::normalize(glm::vec3(-1, 1, 0));
        base = glm::vec3(bx + 1.0f, by + 0.22f, bz + 0.5f);
    }
    else if (facing == 3) {
        axis = glm::normalize(glm::vec3(0, 1, 1));
        base = glm::vec3(bx + 0.5f, by + 0.22f, (float)bz);
    }
    else if (facing == 4) {
        axis = glm::normalize(glm::vec3(0, 1, -1));
        base = glm::vec3(bx + 0.5f, by + 0.22f, bz + 1.0f);
    }
    glm::vec3 hint = std::abs(axis.y) > 0.95f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::vec3 right = glm::normalize(glm::cross(axis, hint));
    glm::vec3 fwd = glm::normalize(glm::cross(right, axis));
    float half = 1.0f / 16.0f;
    float len = 10.0f / 16.0f;
    glm::vec3 tip = base + axis * len;
    unsigned tex = gBlockTex.torch;
    if (gBlockTex.torchAlt && ((int)(glfwGetTime() * 7.0) & 1))
        tex = gBlockTex.torchAlt;
    float u0 = 7.0f / 16.0f, u1 = 9.0f / 16.0f, v0 = 0.0f, v1 = 10.0f / 16.0f;
    glm::vec2 t00(u0, v0), t10(u1, v0), t11(u1, v1), t01(u0, v1);
    auto side = [&](const glm::vec3& out) {
        glm::vec3 o = out * half;
        glm::vec3 across = glm::normalize(glm::cross(axis, out)) * half;
        drawCutoutQuad(bx, by, bz, tex, out,
            base - across + o, base + across + o, tip + across + o, tip - across + o,
            t00, t10, t11, t01, 0.0f);
    };
    side(right);
    side(-right);
    side(fwd);
    side(-fwd);
    glm::vec2 b00(u0, v0), b10(u1, v0), b11(u1, v0 + 1.0f / 16.0f), b01(u0, v0 + 1.0f / 16.0f);
    glm::vec2 f00(u0, v1 - 1.0f / 16.0f), f10(u1, v1 - 1.0f / 16.0f), f11(u1, v1), f01(u0, v1);
    drawCutoutQuad(bx, by, bz, tex, -axis,
        base - right * half - fwd * half, base + right * half - fwd * half,
        base + right * half + fwd * half, base - right * half + fwd * half,
        b00, b10, b11, b01, 0.0f);
    drawCutoutQuad(bx, by, bz, tex, axis,
        tip - right * half + fwd * half, tip + right * half + fwd * half,
        tip + right * half - fwd * half, tip - right * half - fwd * half,
        f00, f01, f11, f10, 0.0f);
}

static const int kChunk = CHUNK;
static const int kMeshY = HEIGHT - 2;

struct ChunkMesh {
    int cx = 0, cz = 0;
    std::vector<MeshBatch> opaque;
    std::vector<MeshBatch> cutout;
    std::vector<MeshBatch> leaves;
    MeshBatch water;
};

static std::unordered_map<unsigned long long, ChunkMesh> gMeshes;

static unsigned long long meshKey(int cx, int cz) {
    return ((unsigned long long)(unsigned)cx << 32) | (unsigned)cz;
}

static int divFloorR(int a, int b) {
    int q = a / b, r = a % b;
    if (r != 0 && ((a < 0) != (b < 0))) --q;
    return q;
}

static void bindMeshPointers() {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(MeshVert), (const void*)offsetof(MeshVert, px));
    glNormalPointer(GL_FLOAT, sizeof(MeshVert), (const void*)offsetof(MeshVert, nx));
    glColorPointer(4, GL_FLOAT, sizeof(MeshVert), (const void*)offsetof(MeshVert, r));
    glx.ClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(4, GL_FLOAT, sizeof(MeshVert), (const void*)offsetof(MeshVert, t0x));
    glx.ClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(4, GL_FLOAT, sizeof(MeshVert), (const void*)offsetof(MeshVert, t1x));
    glx.ClientActiveTexture(GL_TEXTURE2);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(4, GL_FLOAT, sizeof(MeshVert), (const void*)offsetof(MeshVert, t2x));
    glx.ClientActiveTexture(GL_TEXTURE0);
}

static void uploadBatch(MeshBatch& batch) {
    batch.count = (int)batch.v.size();
    if (batch.count <= 0) {
        batch.release();
        batch.v.clear();
        return;
    }
    if (!batch.vbo) glx.GenBuffers(1, &batch.vbo);
    if (!batch.vao) glx.GenVertexArrays(1, &batch.vao);
    glx.BindVertexArray(batch.vao);
    glx.BindBuffer(GL_ARRAY_BUFFER, batch.vbo);
    glx.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(batch.v.size() * sizeof(MeshVert)), batch.v.data(), GL_STATIC_DRAW);
    bindMeshPointers();
    glx.BindVertexArray(0);
    glx.BindBuffer(GL_ARRAY_BUFFER, 0);
    batch.v.clear();
    batch.v.shrink_to_fit();
}

static void buildChunk(int cx, int cz) {
    ChunkMesh& chunk = gMeshes[meshKey(cx, cz)];
    chunk.cx = cx;
    chunk.cz = cz;
    chunk.opaque.clear();
    chunk.cutout.clear();
    chunk.leaves.clear();
    chunk.water = MeshBatch();
    int x0 = cx * kChunk;
    int z0 = cz * kChunk;
    int x1 = x0 + kChunk - 1;
    int z1 = z0 + kChunk - 1;
    auto solidBlock = [](Block b) {
        return b == Block::Grass || b == Block::Dirt || b == Block::Lava || b == Block::Stone
            || b == Block::Obsidian || b == Block::Log;
    };
    gCap = &chunk.opaque;
    gCapWater = nullptr;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) {
            int yLo = 0, yHi = -1;
            worldColumnSpan(x, z, yLo, yHi);
            if (yHi > kMeshY) yHi = kMeshY;
            for (int y = yLo; y <= yHi; ++y)
                if (solidBlock(blockAt(x, y, z))) emitBox(x, y, z, 0.5f, true);
        }
    gCap = &chunk.leaves;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) {
            int yLo = 0, yHi = -1;
            worldColumnSpan(x, z, yLo, yHi);
            if (yHi > kMeshY) yHi = kMeshY;
            for (int y = yLo; y <= yHi; ++y)
                if (blockAt(x, y, z) == Block::Leaves) emitBox(x, y, z, 0.5f, true);
        }
    gCap = &chunk.cutout;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) {
            int yLo = 0, yHi = -1;
            worldColumnSpan(x, z, yLo, yHi);
            if (yHi > kMeshY) yHi = kMeshY;
            for (int y = yLo; y <= yHi; ++y) {
                Block b = blockAt(x, y, z);
                if (b == Block::Torch) drawTorch(x, y, z);
                else if (isPlant(b)) drawPlant(x, y, z);
            }
        }
    gCap = nullptr;
    gCapWater = &chunk.water;
    auto colH = [](int x, int z) {
        if (!waterHasSurface(x, z)) return 0.0f;
        float h = waterSurfaceY(x, z);
        if (h < 0.02f) return 0.0f;
        if (solid(x, (int)std::floor(h), z)) return 0.0f;
        return h;
    };
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            float y00 = colH(x, z);
            if (y00 < 0.02f) continue;
            float foam = 0.0f;
            if (!waterHasSurface(x - 1, z) || !waterHasSurface(x + 1, z)
                || !waterHasSurface(x, z - 1) || !waterHasSurface(x, z + 1))
                foam = 1.0f;
            float fx0 = (float)x, fz0 = (float)z, fx1 = fx0 + 1.0f, fz1 = fz0 + 1.0f;
            float h = y00;
            float wave = seaWaveAt(x, z);
            pushWater(glm::vec3(fx0, h, fz0), glm::vec3(0, 1, 0), 0, 0, 0, foam, 0, 0, wave);
            pushWater(glm::vec3(fx0, h, fz1), glm::vec3(0, 1, 0), 0, 0, 0, foam, 0, 0, wave);
            pushWater(glm::vec3(fx1, h, fz1), glm::vec3(0, 1, 0), 0, 0, 0, foam, 0, 0, wave);
            pushWater(glm::vec3(fx1, h, fz0), glm::vec3(0, 1, 0), 0, 0, 0, foam, 0, 0, wave);
            emitWaterVolume(x, z, h, wave);
        }
    gCapWater = nullptr;
    auto uploadList = [](std::vector<MeshBatch>& list) {
        for (MeshBatch& batch : list) uploadBatch(batch);
    };
    uploadList(chunk.opaque);
    uploadList(chunk.leaves);
    uploadList(chunk.cutout);
    uploadBatch(chunk.water);
}

static int gFrame = 0;
static int gMeshFrame = -1;
static int gMeshBudget = 1;
static std::unordered_map<unsigned long long, char> gPending;

void beginFrameMeshes() { ++gFrame; }

void setMeshBudget(int budget) {
    gMeshBudget = budget < 0 ? 0 : budget;
}

static void queueMeshRect(int x0, int z0, int x1, int z1) {
    int n = loadedChunkCount();
    for (int i = 0; i < n; ++i) {
        int cx, cz;
        loadedChunkCoord(i, cx, cz);
        if (cx * kChunk > x1 || (cx + 1) * kChunk - 1 < x0) continue;
        if (cz * kChunk > z1 || (cz + 1) * kChunk - 1 < z0) continue;
        gPending[meshKey(cx, cz)] = 1;
    }
}

static void updateWorldMeshes() {
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    if (consumeMeshDirty(x0, z0, x1, z1))
        queueMeshRect(x0, z0, x1, z1);
    if (consumeWaterMeshDirty()) {
        glm::vec3 cam = cameraPosition();
        int ccx = (int)std::floor(cam.x / (float)kChunk);
        int ccz = (int)std::floor(cam.z / (float)kChunk);
        queueMeshRect((ccx - 1) * kChunk, (ccz - 1) * kChunk, (ccx + 2) * kChunk - 1, (ccz + 2) * kChunk - 1);
    }

    int n = loadedChunkCount();
    glm::vec3 cam = cameraPosition();
    int ccx = (int)std::floor(cam.x / (float)kChunk);
    int ccz = (int)std::floor(cam.z / (float)kChunk);
    std::unordered_map<unsigned long long, char> keep;
    struct Job { int cx, cz, d; };
    std::vector<Job> jobs;
    jobs.reserve(8);
    for (int i = 0; i < n; ++i) {
        int cx, cz;
        loadedChunkCoord(i, cx, cz);
        unsigned long long k = meshKey(cx, cz);
        keep[k] = 1;
        bool missing = gMeshes.find(k) == gMeshes.end();
        bool pending = gPending.find(k) != gPending.end();
        if (!missing && !pending) continue;
        int adx = cx - ccx; if (adx < 0) adx = -adx;
        int adz = cz - ccz; if (adz < 0) adz = -adz;
        Job job;
        job.cx = cx;
        job.cz = cz;
        job.d = adx > adz ? adx : adz;
        jobs.push_back(job);
    }
    std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.d < b.d; });
    int cap = gMeshBudget;
    if (cap > 1) cap = 1;
    if (cap < 1 && !jobs.empty() && jobs[0].d <= 1) cap = 1;
    int built = 0;
    for (size_t i = 0; i < jobs.size() && built < cap; ++i) {
        buildChunk(jobs[i].cx, jobs[i].cz);
        gPending.erase(meshKey(jobs[i].cx, jobs[i].cz));
        ++built;
    }
    for (auto it = gPending.begin(); it != gPending.end(); ) {
        if (keep.find(it->first) == keep.end()) it = gPending.erase(it);
        else ++it;
    }
    for (auto it = gMeshes.begin(); it != gMeshes.end(); ) {
        if (keep.find(it->first) == keep.end()) it = gMeshes.erase(it);
        else ++it;
    }
}

static bool chunkVisible(int cx, int cz, int radius) {
    glm::vec3 cam = cameraPosition();
    glm::vec3 look = cameraLookDirection();
    float minX = (float)(cx * kChunk);
    float maxX = minX + kChunk;
    float minZ = (float)(cz * kChunk);
    float maxZ = minZ + kChunk;
    float dx = std::max(minX - cam.x, std::max(0.0f, cam.x - maxX));
    float dz = std::max(minZ - cam.z, std::max(0.0f, cam.z - maxZ));
    if (dx * dx + dz * dz > (float)(radius + kChunk) * (radius + kChunk)) return false;
    float mx = (minX + maxX) * 0.5f - cam.x;
    float mz = (minZ + maxZ) * 0.5f - cam.z;
    if (mx * mx + mz * mz < (float)(kChunk * kChunk)) return true;
    float fl = std::sqrt(look.x * look.x + look.z * look.z);
    if (fl < 0.001f) return true;
    float dot = (look.x * mx + look.z * mz) / (fl * std::sqrt(mx * mx + mz * mz));
    return dot > -0.2f;
}

static void drawBatches(const std::vector<MeshBatch>& batches, bool animateTorch) {
    for (const MeshBatch& batch : batches) {
        if (!batch.vao || batch.count <= 0) continue;
        unsigned tex = batch.tex;
        if (animateTorch && tex == gBlockTex.torch && gBlockTex.torchAlt && (((int)(glfwGetTime() * 7.0)) & 1))
            tex = gBlockTex.torchAlt;
        bindAlbedo(tex);
        glx.BindVertexArray(batch.vao);
        glDrawArrays(GL_QUADS, 0, batch.count);
    }
    glx.BindVertexArray(0);
}

static void drawWaterMesh(const MeshBatch& water) {
    if (!water.vao || water.count <= 0) return;
    glx.BindVertexArray(water.vao);
    glDrawArrays(GL_QUADS, 0, water.count);
    glx.BindVertexArray(0);
}

void clearWorldMeshes() {
    gMeshes.clear();
    gPending.clear();
}

int pumpWorldMeshes(int maxBuild) {
    if (maxBuild < 1) maxBuild = 1;
    int n = loadedChunkCount();
    glm::vec3 cam = cameraPosition();
    int ccx = (int)std::floor(cam.x / (float)kChunk);
    int ccz = (int)std::floor(cam.z / (float)kChunk);
    struct Miss { int cx, cz, d; };
    std::vector<Miss> miss;
    miss.reserve(32);
    for (int i = 0; i < n; ++i) {
        int cx, cz;
        loadedChunkCoord(i, cx, cz);
        if (gMeshes.find(meshKey(cx, cz)) != gMeshes.end()) continue;
        int adx = cx - ccx; if (adx < 0) adx = -adx;
        int adz = cz - ccz; if (adz < 0) adz = -adz;
        Miss m;
        m.cx = cx; m.cz = cz; m.d = adx > adz ? adx : adz;
        miss.push_back(m);
    }
    std::sort(miss.begin(), miss.end(), [](const Miss& a, const Miss& b) { return a.d < b.d; });
    int built = 0;
    for (size_t i = 0; i < miss.size() && built < maxBuild; ++i) {
        buildChunk(miss[i].cx, miss[i].cz);
        ++built;
    }
    return (int)miss.size() - built;
}

static void ensureMeshes() {
    if (gMeshFrame == gFrame) return;
    gMeshFrame = gFrame;
    updateWorldMeshes();
}

static bool plantShadowTex(unsigned tex) {
    return tex && (tex == gBlockTex.tallGrass || tex == gBlockTex.poppy
        || tex == gBlockTex.triteleia || tex == gBlockTex.triteleiaRed);
}

void renderShadowCasters(int renderRadiusXZ, int) {
    ensureMeshes();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    for (auto it = gMeshes.begin(); it != gMeshes.end(); ++it)
        if (chunkVisible(it->second.cx, it->second.cz, renderRadiusXZ))
            drawBatches(it->second.opaque, false);
    glDisable(GL_CULL_FACE);
    int plantRadius = renderRadiusXZ < 36 ? renderRadiusXZ : 36;
    bindPlantShadowShader((float)glfwGetTime());
    for (auto it = gMeshes.begin(); it != gMeshes.end(); ++it) {
        if (!chunkVisible(it->second.cx, it->second.cz, plantRadius)) continue;
        for (const MeshBatch& batch : it->second.cutout) {
            if (!plantShadowTex(batch.tex) || !batch.vao || batch.count <= 0) continue;
            bindAlbedo(batch.tex);
            glx.BindVertexArray(batch.vao);
            glDrawArrays(GL_QUADS, 0, batch.count);
        }
    }
    glx.BindVertexArray(0);
}

void renderOpaqueBlocks(int renderRadiusXZ, int) {
    ensureMeshes();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    for (auto it = gMeshes.begin(); it != gMeshes.end(); ++it) {
        if (!chunkVisible(it->second.cx, it->second.cz, renderRadiusXZ)) continue;
        drawBatches(it->second.opaque, false);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (auto it = gMeshes.begin(); it != gMeshes.end(); ++it)
        if (chunkVisible(it->second.cx, it->second.cz, renderRadiusXZ))
            drawBatches(it->second.leaves, false);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    for (auto it = gMeshes.begin(); it != gMeshes.end(); ++it)
        if (chunkVisible(it->second.cx, it->second.cz, renderRadiusXZ))
            drawBatches(it->second.cutout, true);
}

void renderWaterBlocks(int renderRadiusXZ, int) {
    ensureMeshes();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    gCapWater = nullptr;
    for (auto it = gMeshes.begin(); it != gMeshes.end(); ++it)
        if (chunkVisible(it->second.cx, it->second.cz, renderRadiusXZ))
            drawWaterMesh(it->second.water);
    fluidDraw();
    glDisable(GL_BLEND);
}
