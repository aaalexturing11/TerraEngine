#include "ItemIcon.h"

#include "GlExt.h"
#include "GlIncludes.h"
#include "Renderer.h"

#include <glm/glm.hpp>
#include <cmath>

static const int kIcon = 96;
static unsigned gIcon[16] = {};
static unsigned gTorchAlt = 0;
static unsigned gFbo = 0;
static unsigned gDepth = 0;
static bool gReady = false;

static void face(float shade, unsigned tex, const glm::vec3& color,
    const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
    float u0, float v0, float u1, float v1) {
    if (tex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
    }
    else {
        glDisable(GL_TEXTURE_2D);
    }
    glColor4f(color.r * shade, color.g * shade, color.b * shade, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex3f(a.x, a.y, a.z);
    glTexCoord2f(u1, v0); glVertex3f(b.x, b.y, b.z);
    glTexCoord2f(u1, v1); glVertex3f(c.x, c.y, c.z);
    glTexCoord2f(u0, v1); glVertex3f(d.x, d.y, d.z);
    glEnd();
}

static void drawCube(unsigned top, unsigned side, unsigned bottom, const glm::vec3& color) {
    const float h = 0.5f;
    face(1.00f, top, color, { -h, h, -h }, { -h, h, h }, { h, h, h }, { h, h, -h }, 0, 0, 1, 1);
    face(0.42f, bottom, color, { -h, -h, h }, { -h, -h, -h }, { h, -h, -h }, { h, -h, h }, 0, 0, 1, 1);
    face(0.78f, side, color, { -h, -h, h }, { h, -h, h }, { h, h, h }, { -h, h, h }, 0, 0, 1, 1);
    face(0.62f, side, color, { h, -h, -h }, { -h, -h, -h }, { -h, h, -h }, { h, h, -h }, 0, 0, 1, 1);
    face(0.70f, side, color, { h, -h, h }, { h, -h, -h }, { h, h, -h }, { h, h, h }, 0, 0, 1, 1);
    face(0.55f, side, color, { -h, -h, -h }, { -h, -h, h }, { -h, h, h }, { -h, h, -h }, 0, 0, 1, 1);
}

static void drawCross(unsigned tex) {
    glDisable(GL_CULL_FACE);
    face(1.0f, tex, glm::vec3(1.0f),
        { -0.55f, -0.62f, -0.55f }, { 0.55f, -0.62f, 0.55f }, { 0.55f, 0.62f, 0.55f }, { -0.55f, 0.62f, -0.55f },
        0, 0, 1, 1);
    face(0.9f, tex, glm::vec3(1.0f),
        { 0.55f, -0.62f, -0.55f }, { -0.55f, -0.62f, 0.55f }, { -0.55f, 0.62f, 0.55f }, { 0.55f, 0.62f, -0.55f },
        0, 0, 1, 1);
    glEnable(GL_CULL_FACE);
}

static void drawTorchMesh(unsigned tex) {
    float half = 0.09f;
    float y0 = -0.58f;
    float y1 = 0.62f;
    float u0 = 7.0f / 16.0f, u1 = 9.0f / 16.0f, v0 = 0.0f, v1 = 10.0f / 16.0f;
    auto side = [&](const glm::vec3& out, float shade) {
        glm::vec3 across(-out.z, 0.0f, out.x);
        across *= half;
        glm::vec3 o = out * half;
        face(shade, tex, glm::vec3(1.0f),
            glm::vec3(0, y0, 0) - across + o,
            glm::vec3(0, y0, 0) + across + o,
            glm::vec3(0, y1, 0) + across + o,
            glm::vec3(0, y1, 0) - across + o,
            u0, v0, u1, v1);
    };
    side(glm::vec3(1, 0, 0), 0.78f);
    side(glm::vec3(-1, 0, 0), 0.62f);
    side(glm::vec3(0, 0, 1), 0.86f);
    side(glm::vec3(0, 0, -1), 0.55f);
    face(1.0f, tex, glm::vec3(1.0f),
        { -half, y1, half }, { half, y1, half }, { half, y1, -half }, { -half, y1, -half },
        u0, v1 - 1.0f / 16.0f, u1, v1);
}

static void paint(Block block, unsigned torchTex) {
    const BlockTextures& tex = blockTextures();
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.15f);
    glDisable(GL_BLEND);
    glDisable(GL_LIGHTING);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    switch (block) {
    case Block::Grass: drawCube(tex.grassTop, tex.grassSide, tex.dirt, glm::vec3(1.0f)); break;
    case Block::Dirt: drawCube(tex.dirt, tex.dirt, tex.dirt, glm::vec3(1.0f)); break;
    case Block::Stone: drawCube(tex.stone, tex.stone, tex.stone, glm::vec3(1.0f)); break;
    case Block::Log: drawCube(tex.logTop, tex.logSide, tex.logTop, glm::vec3(1.0f)); break;
    case Block::Leaves: drawCube(tex.leaves, tex.leaves, tex.leaves, glm::vec3(1.0f)); break;
    case Block::Water: drawCube(0, 0, 0, glm::vec3(0.16f, 0.45f, 0.82f)); break;
    case Block::Lava: drawCube(0, 0, 0, glm::vec3(0.95f, 0.38f, 0.08f)); break;
    case Block::Obsidian: drawCube(0, 0, 0, glm::vec3(0.16f, 0.10f, 0.20f)); break;
    case Block::Torch: drawTorchMesh(torchTex); break;
    case Block::TallGrass: drawCross(tex.tallGrass); break;
    case Block::Poppy: drawCross(tex.poppy); break;
    case Block::Triteleia: drawCross(tex.triteleia); break;
    case Block::TriteleiaRed: drawCross(tex.triteleiaRed); break;
    default: drawCube(0, 0, 0, glm::vec3(0.30f, 0.68f, 0.28f)); break;
    }
}

static unsigned makeTarget() {
    unsigned tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kIcon, kIcon, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

void itemIconsBuild() {
    if (gReady || !glx.GenFramebuffers) return;

    GLint prevFbo = 0, prevProg = 0, prevDraw = 0, prevVp[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_DRAW_BUFFER, &prevDraw);
    glGetIntegerv(GL_VIEWPORT, prevVp);
    glx.ActiveTexture(GL_TEXTURE0);

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();

    glx.GenFramebuffers(1, &gFbo);
    gDepth = makeTarget();
    glBindTexture(GL_TEXTURE_2D, gDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kIcon, kIcon, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    glx.BindFramebuffer(GL_FRAMEBUFFER, gFbo);
    glx.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gDepth, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, kIcon, kIcon);
    glx.UseProgram(0);
    glx.BindVertexArray(0);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.15, 1.15, -1.28, 1.02, -4.0, 4.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(30.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(-45.0f, 0.0f, 1.0f, 0.0f);

    const Block blocks[] = {
        Block::Grass, Block::Dirt, Block::Stone, Block::Log, Block::Leaves,
        Block::Water, Block::Lava, Block::Obsidian, Block::Torch,
        Block::TallGrass, Block::Poppy, Block::Triteleia, Block::TriteleiaRed
    };
    const BlockTextures& tex = blockTextures();
    bool ok = true;
    for (Block block : blocks) {
        int id = (int)block;
        if (id < 0 || id >= 16) continue;
        gIcon[id] = makeTarget();
        glx.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gIcon[id], 0);
        if (glx.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            ok = false;
            break;
        }
        unsigned flame = (block == Block::Torch) ? tex.torch : 0;
        paint(block, flame);
    }
    if (ok && tex.torchAlt) {
        gTorchAlt = makeTarget();
        glx.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gTorchAlt, 0);
        if (glx.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
            paint(Block::Torch, tex.torchAlt);
        else
            gTorchAlt = 0;
    }

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();

    glx.BindFramebuffer(GL_FRAMEBUFFER, (unsigned)prevFbo);
    glDrawBuffer(prevDraw);
    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
    glx.UseProgram((unsigned)prevProg);
    glx.ActiveTexture(GL_TEXTURE0);
    glx.ClientActiveTexture(GL_TEXTURE0);
    gReady = ok;
}

unsigned itemIcon(Block block, float time) {
    if (!gReady || block == Block::Air) return 0;
    if (block == Block::Torch && gTorchAlt && (((int)(time * 7.0f)) & 1))
        return gTorchAlt;
    int id = (int)block;
    if (id < 0 || id >= 16) return 0;
    return gIcon[id];
}
