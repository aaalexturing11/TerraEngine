#include "GlIncludes.h"
#include "GlExt.h"
#include "Renderer.h"
#include "Camera.h"
#include "FluidSolver.h"
#include "World.h"
#include <algorithm>
#include <cmath>
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
    std::vector<MeshVert> v;
};

static std::vector<MeshBatch>* gCap = nullptr;
static std::vector<MeshVert>* gCapWater = nullptr;

static void pushMesh(unsigned tex, const glm::vec3& p, const glm::vec3& n, float r, float g, float b, float a,
    float t0x, float t0y, float t0z, float t0w,
    float t1x, float t1y, float t1z, float t1w,
    float t2x, float t2y, float t2z, float t2w) {
    MeshVert m{ p.x, p.y, p.z, n.x, n.y, n.z, r, g, b, a, t0x, t0y, t0z, t0w, t1x, t1y, t1z, t1w, t2x, t2y, t2z, t2w };
    if (gCapWater) {
        gCapWater->push_back(m);
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
    gluPerspective(cameraFov(), (double)w / h, 0.1, 250.0);
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

struct WaterCorner {
    float h = 0.0f;
    float fx = 0.0f;
    float fz = 0.0f;
    float down = 0.0f;
};

static WaterCorner sampleCorner(int cx, int cz, int y) {
    WaterCorner c;
    float h = 0.0f, fx = 0.0f, fz = 0.0f, down = 0.0f;
    int n = 0;
    int air = 0;
    bool column = false;
    for (int ox = -1; ox <= 0; ++ox) {
        for (int oz = -1; oz <= 0; ++oz) {
            int wx = cx + ox, wz = cz + oz;
            int lv = waterLevelAt(wx, y, wz);
            if (lv < 0) {
                ++air;
                continue;
            }
            float ffx, ffz, fd;
            waterFlow(wx, y, wz, ffx, ffz, fd);
            Block under = blockAt(wx, y - 1, wz);
            bool lip = under == Block::Air || under == Block::Torch || isPlant(under);
            bool falling = fd > 0.5f || waterLevelAt(wx, y + 1, wz) >= 0;
            float top = (float)y + waterFill01(wx, y, wz);
            if (top < (float)y + 0.04f) top = (float)y + 0.04f;
            if (solid(wx, y + 1, wz) && top > (float)y + 0.97f) top = (float)y + 0.97f;
            if (!lip && falling) {
                top = y + 1.0f;
                if (solid(wx, y + 1, wz)) top = y + 0.97f;
                column = true;
            }
            h += top;
            fx += ffx;
            fz += ffz;
            down += fd;
            ++n;
        }
    }
    if (!n) {
        c.h = (float)y;
        return c;
    }
    c.h = h / (float)n;
    c.fx = fx / (float)n;
    c.fz = fz / (float)n;
    c.down = down / (float)n;
    if (!column && air > 0) {
        float edge = air / 4.0f;
        c.h = c.h * (1.0f - edge) + ((float)y + 0.04f) * edge;
    }
    return c;
}

static void pushWater(const glm::vec3& p, const glm::vec3& n, float fx, float fz, float kind, float strength, float across, float lipY) {
    if (gCapWater) {
        pushMesh(0, p, n, 1, 1, 1, 1, fx, fz, kind, strength, across, lipY, 0, 0, 0, 0, 0, 0);
        return;
    }
    glNormal3f(n.x, n.y, n.z);
    glTexCoord4f(fx, fz, kind, strength);
    glx.MultiTexCoord4f(GL_TEXTURE1, across, lipY, 0, 0);
    glVertex3f(p.x, p.y, p.z);
}

static bool cellOpen(int x, int y, int z) {
    Block b = blockAt(x, y, z);
    return b == Block::Air || b == Block::Torch || isPlant(b);
}

static void drawWaterFace(float x0, float z0, float x1, float z1, float y0, float y1,
    float fx, float fz, float kind, float strength, float lipY) {
    glm::vec3 edge(x1 - x0, 0.0f, z1 - z0);
    glm::vec3 n = glm::cross(edge, glm::vec3(0, 1, 0));
    float len = glm::length(n);
    if (len < 0.0001f) n = glm::vec3(1, 0, 0);
    else n /= len;
    pushWater(glm::vec3(x0, y0, z0), n, fx, fz, kind, strength, 0, lipY);
    pushWater(glm::vec3(x1, y0, z1), n, fx, fz, kind, strength, 1, lipY);
    pushWater(glm::vec3(x1, y1, z1), n, fx, fz, kind, strength, 1, lipY);
    pushWater(glm::vec3(x0, y1, z0), n, fx, fz, kind, strength, 0, lipY);
}

static void drawWater(int bx, int by, int bz) {
    float fx, fz, down;
    waterFlow(bx, by, bz, fx, fz, down);
    bool top = waterLevelAt(bx, by + 1, bz) < 0;
    int lv = waterLevelAt(bx, by, bz);
    if (lv < 0) lv = 0;
    float fill = waterFill01(bx, by, bz);
    float strength = fill;
    if (strength < 0.18f) strength = 0.18f;

    float surfaceY = (float)by + fill;
    bool lip = by > 0 && cellOpen(bx, by - 1, bz);
    if (top && !lip) {
        WaterCorner c00 = sampleCorner(bx, bz, by);
        WaterCorner c10 = sampleCorner(bx + 1, bz, by);
        WaterCorner c11 = sampleCorner(bx + 1, bz + 1, by);
        WaterCorner c01 = sampleCorner(bx, bz + 1, by);
        surfaceY = (c00.h + c10.h + c11.h + c01.h) * 0.25f;
        float x0 = (float)bx, x1 = (float)bx + 1.0f;
        float z0 = (float)bz, z1 = (float)bz + 1.0f;
        if (solid(bx - 1, by, bz)) x0 += 0.03f;
        if (solid(bx + 1, by, bz)) x1 -= 0.03f;
        if (solid(bx, by, bz - 1)) z0 += 0.03f;
        if (solid(bx, by, bz + 1)) z1 -= 0.03f;
        if (!gCapWater) {
            glBegin(GL_QUADS);
            glNormal3f(0, 1, 0);
        }
        auto vert = [&](const WaterCorner& c, float x, float z) {
            float h = c.h;
            int ix = (int)std::floor(x - (x > (float)bx + 0.5f ? 0.01f : 0.0f));
            int iz = (int)std::floor(z - (z > (float)bz + 0.5f ? 0.01f : 0.0f));
            if (solid(ix, by + 1, iz) && h > (float)by + 0.97f) h = (float)by + 0.97f;
            pushWater(glm::vec3(x, h, z), glm::vec3(0, 1, 0), c.fx, c.fz, c.down, 0, 0, 0);
        };
        vert(c00, x0, z0);
        vert(c01, x0, z1);
        vert(c11, x1, z1);
        vert(c10, x1, z0);
        if (!gCapWater) glEnd();
    }
    if (!gCapWater) glBegin(GL_QUADS);
    if (lip) {
        int y = by - 1;
        while (y > 0 && cellOpen(bx, y, bz) && !solid(bx, y, bz)) --y;
        if (solid(bx, y, bz) || blockAt(bx, y, bz) == Block::Water) {
            float yBot = (blockAt(bx, y, bz) == Block::Water) ? (float)y + waterFill01(bx, y, bz) : (float)(y + 1);
            float yTop = surfaceY;
            if (yTop < (float)by + 0.2f) yTop = (float)by + 0.2f;
            if (yTop - yBot > 0.15f) {
                float cx = bx + 0.5f;
                float cz = bz + 0.5f;
                float hw = 0.04f + 0.03f * strength;
                drawWaterFace(cx - hw, cz - hw, cx + hw, cz + hw, yBot, yTop, fx, fz, 1.0f, strength, yTop);
                drawWaterFace(cx - hw, cz + hw, cx + hw, cz - hw, yBot, yTop, fx, fz, 1.0f, strength, yTop);
            }
        }
    }
    else {
        float y1 = top ? surfaceY : (float)by + 1.0f;
        float y0 = (float)by;
        float xA = (float)bx + 0.04f, xB = (float)bx + 0.96f;
        float zA = (float)bz + 0.04f, zB = (float)bz + 0.96f;
        if (cellOpen(bx + 1, by, bz)) drawWaterFace(xB, zA, xB, zB, y0, y1, fx, fz, 2.0f, strength, 0);
        if (cellOpen(bx - 1, by, bz)) drawWaterFace(xA, zB, xA, zA, y0, y1, fx, fz, 2.0f, strength, 0);
        if (cellOpen(bx, by, bz + 1)) drawWaterFace(xB, zB, xA, zB, y0, y1, fx, fz, 2.0f, strength, 0);
        if (cellOpen(bx, by, bz - 1)) drawWaterFace(xA, zA, xB, zA, y0, y1, fx, fz, 2.0f, strength, 0);
    }
    if (!gCapWater) glEnd();
}

static void drawCutoutQuad(int bx, int by, int bz, unsigned tex, const glm::vec3& n,
    const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
    glm::vec2 ta, glm::vec2 tb, glm::vec2 tc, glm::vec2 td) {
    FaceLight lit = evaluateFace(bx, by, bz, n);
    glm::vec3 center(bx + 0.5f, by + 0.5f, bz + 0.5f);
    auto put = [&](const glm::vec3& p, const glm::vec2& uv) {
        if (gCap) {
            pushMesh(tex, p, n, lit.albedo.r, lit.albedo.g, lit.albedo.b, lit.blockLight,
                lit.gi, lit.emission, lit.caustic, lit.ao, uv.x, uv.y, tex ? 1.0f : 0.0f, 0.0f,
                center.x, center.y, center.z, 0.0f);
            return;
        }
        glx.MultiTexCoord4f(GL_TEXTURE0, lit.gi, lit.emission, lit.caustic, lit.ao);
        glx.MultiTexCoord3f(GL_TEXTURE1, uv.x, uv.y, tex ? 1.0f : 0.0f);
        glx.MultiTexCoord4f(GL_TEXTURE2, center.x, center.y, center.z, 0.0f);
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
    drawCutoutQuad(bx, by, bz, tex, up, { x0, y0, z0 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z0 }, uv00, uv10, uv11, uv01);
    drawCutoutQuad(bx, by, bz, tex, up, { x1, y0, z1 }, { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z1 }, uv00, uv10, uv11, uv01);
    drawCutoutQuad(bx, by, bz, tex, up, { x1, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x1, y1, z0 }, uv00, uv10, uv11, uv01);
    drawCutoutQuad(bx, by, bz, tex, up, { x0, y0, z1 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z1 }, uv00, uv10, uv11, uv01);
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
            t00, t10, t11, t01);
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
        b00, b10, b11, b01);
    drawCutoutQuad(bx, by, bz, tex, axis,
        tip - right * half + fwd * half, tip + right * half + fwd * half,
        tip + right * half - fwd * half, tip - right * half - fwd * half,
        f00, f01, f11, f10);
}

static const int kChunk = 16;
static const int kChunkCount = GRID / kChunk;
static const int kMeshY = 96;

struct ChunkMesh {
    std::vector<MeshBatch> opaque;
    std::vector<MeshBatch> cutout;
    std::vector<MeshBatch> leaves;
    std::vector<MeshVert> water;
};

static ChunkMesh gChunks[kChunkCount][kChunkCount];

static void buildChunk(int cx, int cz) {
    ChunkMesh& chunk = gChunks[cx][cz];
    chunk.opaque.clear();
    chunk.cutout.clear();
    chunk.leaves.clear();
    chunk.water.clear();
    int x0 = cx * kChunk;
    int z0 = cz * kChunk;
    int x1 = std::min(GRID - 1, x0 + kChunk - 1);
    int z1 = std::min(GRID - 1, z0 + kChunk - 1);
    auto solidBlock = [](Block b) {
        return b == Block::Grass || b == Block::Dirt || b == Block::Lava || b == Block::Stone
            || b == Block::Obsidian || b == Block::Log;
    };
    gCap = &chunk.opaque;
    gCapWater = nullptr;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            for (int y = 0; y <= kMeshY && y < HEIGHT; ++y)
                if (solidBlock(blocks[x][y][z])) emitBox(x, y, z, 0.5f, true);
    gCap = &chunk.leaves;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            for (int y = 0; y <= kMeshY && y < HEIGHT; ++y)
                if (blocks[x][y][z] == Block::Leaves) emitBox(x, y, z, 0.5f, true);
    gCap = &chunk.cutout;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            for (int y = 0; y <= kMeshY && y < HEIGHT; ++y) {
                Block b = blocks[x][y][z];
                if (b == Block::Torch) drawTorch(x, y, z);
                else if (isPlant(b)) drawPlant(x, y, z);
            }
    gCap = nullptr;
    gCapWater = nullptr;
}

static void updateWorldMeshes() {
    int x0, z0, x1, z1;
    if (!consumeMeshDirty(x0, z0, x1, z1)) return;
    int cx0 = std::max(0, x0 / kChunk);
    int cx1 = std::min(kChunkCount - 1, x1 / kChunk);
    int cz0 = std::max(0, z0 / kChunk);
    int cz1 = std::min(kChunkCount - 1, z1 / kChunk);
    for (int cx = cx0; cx <= cx1; ++cx)
        for (int cz = cz0; cz <= cz1; ++cz)
            buildChunk(cx, cz);
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

static void bindMeshArrays(const MeshVert* p) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(MeshVert), &p->px);
    glNormalPointer(GL_FLOAT, sizeof(MeshVert), &p->nx);
    glColorPointer(4, GL_FLOAT, sizeof(MeshVert), &p->r);
    glx.ClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(4, GL_FLOAT, sizeof(MeshVert), &p->t0x);
    glx.ClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(4, GL_FLOAT, sizeof(MeshVert), &p->t1x);
    glx.ClientActiveTexture(GL_TEXTURE2);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(4, GL_FLOAT, sizeof(MeshVert), &p->t2x);
}

static void unbindMeshArrays() {
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glx.ClientActiveTexture(GL_TEXTURE2);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glx.ClientActiveTexture(GL_TEXTURE1);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glx.ClientActiveTexture(GL_TEXTURE0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

static void drawBatches(const std::vector<MeshBatch>& batches, bool animateTorch) {
    for (const MeshBatch& batch : batches) {
        if (batch.v.empty()) continue;
        unsigned tex = batch.tex;
        if (animateTorch && tex == gBlockTex.torch && gBlockTex.torchAlt && (((int)(glfwGetTime() * 7.0)) & 1))
            tex = gBlockTex.torchAlt;
        bindAlbedo(tex);
        bindMeshArrays(batch.v.data());
        glDrawArrays(GL_QUADS, 0, (GLsizei)batch.v.size());
        unbindMeshArrays();
    }
}

static void drawWaterMesh(const std::vector<MeshVert>& water) {
    if (water.empty()) return;
    bindMeshArrays(water.data());
    glDrawArrays(GL_QUADS, 0, (GLsizei)water.size());
    unbindMeshArrays();
}

void renderShadowCasters(int renderRadiusXZ, int) {
    updateWorldMeshes();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    for (int cx = 0; cx < kChunkCount; ++cx)
        for (int cz = 0; cz < kChunkCount; ++cz)
            if (chunkVisible(cx, cz, renderRadiusXZ))
                drawBatches(gChunks[cx][cz].opaque, false);
    glDisable(GL_CULL_FACE);
}

void renderOpaqueBlocks(int renderRadiusXZ, int) {
    updateWorldMeshes();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    for (int cx = 0; cx < kChunkCount; ++cx)
        for (int cz = 0; cz < kChunkCount; ++cz) {
            if (!chunkVisible(cx, cz, renderRadiusXZ)) continue;
            drawBatches(gChunks[cx][cz].opaque, false);
        }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int cx = 0; cx < kChunkCount; ++cx)
        for (int cz = 0; cz < kChunkCount; ++cz)
            if (chunkVisible(cx, cz, renderRadiusXZ))
                drawBatches(gChunks[cx][cz].leaves, false);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    for (int cx = 0; cx < kChunkCount; ++cx)
        for (int cz = 0; cz < kChunkCount; ++cz)
            if (chunkVisible(cx, cz, renderRadiusXZ))
                drawBatches(gChunks[cx][cz].cutout, true);
}

void renderWaterBlocks(int renderRadiusXZ, int) {
    updateWorldMeshes();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    gCapWater = nullptr;
    int n = waterCellCount();
    for (int i = 0; i < n; ++i) {
        int x, y, z;
        getWaterCell(i, x, y, z);
        int cx = x / kChunk, cz = z / kChunk;
        if (cx < 0 || cz < 0 || cx >= kChunkCount || cz >= kChunkCount) continue;
        if (!chunkVisible(cx, cz, renderRadiusXZ)) continue;
        drawWater(x, y, z);
    }
    fluidDraw();
    glDisable(GL_BLEND);
}
