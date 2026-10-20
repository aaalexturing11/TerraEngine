#include "PostFX.h"
#include "GlExt.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

static unsigned gSceneFbo = 0, gSceneColor = 0, gSceneDepth = 0;
static unsigned gCopyFbo = 0, gCopyColor = 0, gCopyDepth = 0;
static unsigned gAoFbo = 0, gAoTex = 0;
static unsigned gGodFbo = 0, gGodTex = 0;
static unsigned gBloomFbo[2] = {}, gBloomTex[2] = {};
static unsigned gProgSsao = 0, gProgGod = 0, gProgBright = 0, gProgBlur = 0, gProgComposite = 0;
static int gW = 0, gH = 0;

static const char* kFsVs = R"GLSL(
#version 330 compatibility
out vec2 vUv;
void main() {
    vUv = gl_MultiTexCoord0.xy;
    gl_Position = ftransform();
}
)GLSL";

static const char* kSsaoFs = R"GLSL(
#version 330 compatibility
in vec2 vUv;
out vec4 frag;
uniform sampler2D uDepth;
uniform vec2 uInvRes;
uniform mat4 uInvProj;

vec3 viewPos(vec2 uv) {
    float d = texture(uDepth, uv).r;
    vec4 c = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return c.xyz / max(c.w, 0.0001);
}

void main() {
    float d0 = texture(uDepth, vUv).r;
    if (d0 > 0.998) { frag = vec4(1.0); return; }
    vec3 p = viewPos(vUv);
    float occ = 0.0;
    for (int i = 0; i < 4; ++i) {
        float a = float(i) * 2.399963;
        vec2 k = vec2(cos(a), sin(a)) * uInvRes * (2.0 + float(i) * 1.35);
        vec2 uv = clamp(vUv + k, 0.0, 1.0);
        float ds = texture(uDepth, uv).r;
        if (ds < d0 - 0.00035 && abs(d0 - ds) < 0.018)
            occ += 1.0;
    }
    float ao = clamp(1.0 - occ * 0.22, 0.35, 1.0);
    frag = vec4(ao);
}
)GLSL";

static const char* kGodFs = R"GLSL(
#version 330 compatibility
in vec2 vUv;
out vec4 frag;
uniform sampler2D uDepth;
uniform vec2 uSunUv;
void main() {
    vec2 delta = (uSunUv - vUv) / 8.0;
    float illum = 0.0;
    vec2 p = vUv;
    for (int i = 0; i < 8; ++i) {
        p += delta;
        float d = 1.0;
        if (p.x >= 0.0 && p.x <= 1.0 && p.y >= 0.0 && p.y <= 1.0)
            d = texture(uDepth, p).r;
        float w = 1.0 - float(i) / 8.0;
        illum += ((d > 0.997) ? 1.0 : 0.1) * w;
    }
    illum /= 4.0;
    float dist = length(uSunUv - vUv);
    illum *= exp(-max(dist - 0.25, 0.0) * 0.42);
    frag = vec4(illum, illum, illum, 1.0);
}
)GLSL";

static const char* kBrightFs = R"GLSL(
#version 330 compatibility
in vec2 vUv;
out vec4 frag;
uniform sampler2D uScene;
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    frag = vec4(c * smoothstep(1.65, 2.5, l), 1.0);
}
)GLSL";

static const char* kBlurFs = R"GLSL(
#version 330 compatibility
in vec2 vUv;
out vec4 frag;
uniform sampler2D uTex;
uniform vec2 uDir;
void main() {
    float w0 = 0.227027;
    float w1 = 0.1945946;
    float w2 = 0.1216216;
    float w3 = 0.054054;
    float w4 = 0.016216;
    vec3 c = texture(uTex, vUv).rgb * w0;
    c += texture(uTex, vUv + uDir * 1.0).rgb * w1;
    c += texture(uTex, vUv - uDir * 1.0).rgb * w1;
    c += texture(uTex, vUv + uDir * 2.0).rgb * w2;
    c += texture(uTex, vUv - uDir * 2.0).rgb * w2;
    c += texture(uTex, vUv + uDir * 3.0).rgb * w3;
    c += texture(uTex, vUv - uDir * 3.0).rgb * w3;
    c += texture(uTex, vUv + uDir * 4.0).rgb * w4;
    c += texture(uTex, vUv - uDir * 4.0).rgb * w4;
    frag = vec4(c, 1.0);
}
)GLSL";

static const char* kCompositeFs = R"GLSL(
#version 330 compatibility
in vec2 vUv;
out vec4 frag;
uniform sampler2D uScene;
uniform sampler2D uAo;
uniform sampler2D uGod;
uniform sampler2D uBloom;
uniform float uDay;
uniform float uDusk;
uniform float uGodStrength;
uniform float uExposure;
uniform vec2 uSunUv;
uniform sampler2D uDepth;
uniform float uUnder;
uniform float uTime;

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 col = texture(uScene, vUv).rgb;
    float ao = texture(uAo, vUv).r;
    col *= mix(1.0, ao, 0.55);
    vec3 godCol = mix(vec3(0.85, 0.93, 1.0), vec3(1.0, 0.55, 0.22), clamp(uDusk, 0.0, 1.0));
    float sunDist = length(vUv - uSunUv);
    float depth = texture(uDepth, vUv).r;
    float inSky = smoothstep(0.985, 0.998, depth);
    float halo = exp(-sunDist * sunDist * 48.0) * uGodStrength * 0.18 * inSky;
    col += godCol * halo;
    col += texture(uGod, vUv).rgb * godCol * uGodStrength * 0.28 * inSky;
    col += texture(uBloom, vUv).rgb * 0.55;
    if (uUnder > 0.5) {
        float bright = dot(col, vec3(0.30, 0.50, 0.20));
        float window = smoothstep(0.12, 0.45, bright);
        float far = smoothstep(0.12, 0.7, depth) * (1.0 - window);
        col = mix(col, vec3(0.05, 0.28, 0.34), 0.18 + far * 0.35);
        float c1 = sin(vUv.x * 62.0 + uTime * 1.4) * sin(vUv.y * 54.0 - uTime * 1.1);
        float c2 = sin(vUv.x * 27.0 - uTime * 0.6 + vUv.y * 21.0);
        float cau = smoothstep(0.2, 0.85, c1 * 0.5 + 0.5) * smoothstep(0.25, 0.9, c2 * 0.5 + 0.5);
        col += vec3(0.25, 0.55, 0.50) * cau * 0.28 * uDay * (1.0 - window * 0.5);
        vec2 dir = uSunUv - vUv;
        vec2 p = vUv;
        vec2 stepUv = dir / 12.0;
        float acc = 0.0;
        for (int i = 0; i < 8; ++i) {
            p += stepUv;
            if (p.x < 0.0 || p.y < 0.0 || p.x > 1.0 || p.y > 1.0) break;
            float d = texture(uDepth, p).r;
            acc += smoothstep(0.35, 0.92, d) * (1.0 - float(i) / 8.0);
        }
        float face = clamp(1.2 - length(dir) * 0.35, 0.35, 1.0);
        col += vec3(0.75, 0.95, 0.88) * (acc / 4.0) * face * max(uGodStrength, 0.55) * uDay;
    }
    col *= uExposure;
    col = aces(col);
    vec3 grade = mix(vec3(0.78, 0.86, 1.08), vec3(0.90, 0.96, 1.08), clamp(uDay, 0.0, 1.0));
    col *= grade;
    col = mix(col, col * vec3(1.12, 0.82, 0.62), clamp(uDusk, 0.0, 1.0) * 0.35);
    frag = vec4(col, 1.0);
}
)GLSL";

static unsigned makeColorTex(int w, int h, int internalFormat) {
    unsigned tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

static unsigned makeDepthTex(int w, int h) {
    unsigned tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    return tex;
}

static unsigned makeTarget(unsigned& fbo, unsigned color, unsigned depth) {
    glx.GenFramebuffers(1, &fbo);
    glx.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (color) glx.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    if (depth) glx.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    return glx.CheckFramebufferStatus(GL_FRAMEBUFFER);
}

static void destroyTargets() {
    unsigned fbos[] = { gSceneFbo, gCopyFbo, gAoFbo, gGodFbo, gBloomFbo[0], gBloomFbo[1] };
    glx.DeleteFramebuffers(6, fbos);
    unsigned tex[] = { gSceneColor, gSceneDepth, gCopyColor, gCopyDepth, gAoTex, gGodTex, gBloomTex[0], gBloomTex[1] };
    glDeleteTextures(8, tex);
    gSceneFbo = gCopyFbo = gAoFbo = gGodFbo = gBloomFbo[0] = gBloomFbo[1] = 0;
    gSceneColor = gSceneDepth = gCopyColor = gCopyDepth = gAoTex = gGodTex = gBloomTex[0] = gBloomTex[1] = 0;
}

static void allocTargets(int w, int h) {
    if (gSceneFbo) destroyTargets();
    gW = w;
    gH = h;
    int hw = w / 2; if (hw < 1) hw = 1;
    int hh = h / 2; if (hh < 1) hh = 1;
    int qw = w / 4; if (qw < 1) qw = 1;
    int qh = h / 4; if (qh < 1) qh = 1;

    gSceneColor = makeColorTex(w, h, GL_RGBA16F);
    gSceneDepth = makeDepthTex(w, h);
    gCopyColor = makeColorTex(w, h, GL_RGBA16F);
    gCopyDepth = makeDepthTex(w, h);
    gAoTex = makeColorTex(hw, hh, GL_RGBA8);
    gGodTex = makeColorTex(qw, qh, GL_RGBA8);
    gBloomTex[0] = makeColorTex(qw, qh, GL_RGBA16F);
    gBloomTex[1] = makeColorTex(qw, qh, GL_RGBA16F);

    makeTarget(gSceneFbo, gSceneColor, gSceneDepth);
    makeTarget(gCopyFbo, gCopyColor, gCopyDepth);
    makeTarget(gAoFbo, gAoTex, 0);
    makeTarget(gGodFbo, gGodTex, 0);
    makeTarget(gBloomFbo[0], gBloomTex[0], 0);
    makeTarget(gBloomFbo[1], gBloomTex[1], 0);
    glx.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void drawQuad() {
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(-1, 1);
    glEnd();
}

static void setOrtho() {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
}

static void popOrtho() {
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

bool initPostFX() {
    gProgSsao = makeGlProgram(kFsVs, kSsaoFs);
    gProgGod = makeGlProgram(kFsVs, kGodFs);
    gProgBright = makeGlProgram(kFsVs, kBrightFs);
    gProgBlur = makeGlProgram(kFsVs, kBlurFs);
    gProgComposite = makeGlProgram(kFsVs, kCompositeFs);
    return gProgSsao && gProgGod && gProgBright && gProgBlur && gProgComposite;
}

void beginScenePass(int width, int height) {
    if (width != gW || height != gH) allocTargets(width, height);
    glx.BindFramebuffer(GL_FRAMEBUFFER, gSceneFbo);
    glViewport(0, 0, width, height);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
}

void blitSceneForWater() {
    glx.BindFramebuffer(GL_READ_FRAMEBUFFER, gSceneFbo);
    glx.BindFramebuffer(GL_DRAW_FRAMEBUFFER, gCopyFbo);
    glx.BlitFramebuffer(0, 0, gW, gH, 0, 0, gW, gH, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glx.BindFramebuffer(GL_FRAMEBUFFER, gSceneFbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
}

void bindWaterInputs() {
    glx.ActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gCopyColor);
    glx.ActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, gCopyDepth);
    glx.ActiveTexture(GL_TEXTURE0);
}

void presentScene(int width, int height, const DayNightLighting& light, const glm::vec2& sunUv, float godStrength, bool underwater, float time) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    float proj[16];
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glm::mat4 invProj = glm::inverse(glm::make_mat4(proj));

    setOrtho();

    int hw = width / 2; if (hw < 1) hw = 1;
    int hh = height / 2; if (hh < 1) hh = 1;
    int qw = width / 4; if (qw < 1) qw = 1;
    int qh = height / 4; if (qh < 1) qh = 1;

    glx.BindFramebuffer(GL_FRAMEBUFFER, gAoFbo);
    glViewport(0, 0, hw, hh);
    glx.UseProgram(gProgSsao);
    glx.Uniform1i(glx.GetUniformLocation(gProgSsao, "uDepth"), 0);
    glx.Uniform2f(glx.GetUniformLocation(gProgSsao, "uInvRes"), 1.0f / width, 1.0f / height);
    glx.UniformMatrix4fv(glx.GetUniformLocation(gProgSsao, "uInvProj"), 1, GL_FALSE, glm::value_ptr(invProj));
    glx.ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gSceneDepth);
    drawQuad();

    glx.BindFramebuffer(GL_FRAMEBUFFER, gGodFbo);
    glViewport(0, 0, qw, qh);
    glx.UseProgram(gProgGod);
    glx.Uniform1i(glx.GetUniformLocation(gProgGod, "uDepth"), 0);
    glx.Uniform2f(glx.GetUniformLocation(gProgGod, "uSunUv"), sunUv.x, sunUv.y);
    glBindTexture(GL_TEXTURE_2D, gSceneDepth);
    drawQuad();

    glx.BindFramebuffer(GL_FRAMEBUFFER, gBloomFbo[0]);
    glViewport(0, 0, qw, qh);
    glx.UseProgram(gProgBright);
    glx.Uniform1i(glx.GetUniformLocation(gProgBright, "uScene"), 0);
    glBindTexture(GL_TEXTURE_2D, gSceneColor);
    drawQuad();

    glx.UseProgram(gProgBlur);
    glx.Uniform1i(glx.GetUniformLocation(gProgBlur, "uTex"), 0);
    glx.BindFramebuffer(GL_FRAMEBUFFER, gBloomFbo[1]);
    glBindTexture(GL_TEXTURE_2D, gBloomTex[0]);
    glx.Uniform2f(glx.GetUniformLocation(gProgBlur, "uDir"), 1.0f / qw, 0.0f);
    drawQuad();
    glx.BindFramebuffer(GL_FRAMEBUFFER, gBloomFbo[0]);
    glBindTexture(GL_TEXTURE_2D, gBloomTex[1]);
    glx.Uniform2f(glx.GetUniformLocation(gProgBlur, "uDir"), 0.0f, 1.0f / qh);
    drawQuad();

    glx.BindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glViewport(0, 0, width, height);
    glx.UseProgram(gProgComposite);
    glx.Uniform1i(glx.GetUniformLocation(gProgComposite, "uScene"), 0);
    glx.Uniform1i(glx.GetUniformLocation(gProgComposite, "uAo"), 1);
    glx.Uniform1i(glx.GetUniformLocation(gProgComposite, "uGod"), 2);
    glx.Uniform1i(glx.GetUniformLocation(gProgComposite, "uBloom"), 3);
    glx.Uniform1i(glx.GetUniformLocation(gProgComposite, "uDepth"), 4);
    glx.Uniform1f(glx.GetUniformLocation(gProgComposite, "uDay"), light.dayFactor);
    glx.Uniform1f(glx.GetUniformLocation(gProgComposite, "uDusk"), light.duskFactor);
    glx.Uniform1f(glx.GetUniformLocation(gProgComposite, "uGodStrength"), godStrength);
    glx.Uniform2f(glx.GetUniformLocation(gProgComposite, "uSunUv"), sunUv.x, sunUv.y);
    float exposure = underwater ? 0.92f : (0.96f + 0.12f * (1.0f - light.dayFactor));
    glx.Uniform1f(glx.GetUniformLocation(gProgComposite, "uExposure"), exposure);
    glx.Uniform1f(glx.GetUniformLocation(gProgComposite, "uUnder"), underwater ? 1.0f : 0.0f);
    glx.Uniform1f(glx.GetUniformLocation(gProgComposite, "uTime"), time);

    glx.ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gSceneColor);
    glx.ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gAoTex);
    glx.ActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gGodTex);
    glx.ActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, gBloomTex[0]);
    glx.ActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, gSceneDepth);
    drawQuad();

    glx.UseProgram(0);
    glx.ActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);
    popOrtho();
}
