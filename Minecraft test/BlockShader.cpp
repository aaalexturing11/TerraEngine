#include "BlockShader.h"
#include "GlIncludes.h"
#include "GlExt.h"
#include "Camera.h"
#include "FluidSolver.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <iostream>

static unsigned gOpaque = 0;
static unsigned gWater = 0;
static unsigned gPlantShadow = 0;
static unsigned gShadowFbo = 0;
static unsigned gShadowTex = 0;
static glm::mat4 gShadowMat(1.0f);
static const int kShadowSize = 1024;
static int gShadowMapSize = 1024;

static const char* kOpaqueVs = R"GLSL(
#version 330 compatibility
out vec3 vWorld;
out vec3 vNormal;
out vec3 vAlbedo;
out vec4 vPack;
out vec2 vUv;
out float vTex;
out float vLeaf;
out float vLamp;
uniform float uTime;
uniform vec3 uPlayer;
uniform vec2 uPlayerVel;
void main() {
    vec3 p = gl_Vertex.xyz;
    float wind = gl_MultiTexCoord2.w;
    if (wind > 0.01) {
        vec3 c = gl_MultiTexCoord2.xyz;
        float gustX = sin(uTime * 0.7 + c.x * 0.16 + c.z * 0.12);
        float gustZ = cos(uTime * 0.55 + c.z * 0.16 + c.x * 0.09);
        float flutterX = sin(uTime * 1.8 + c.x * 1.6 + c.z * 0.7);
        float flutterZ = cos(uTime * 1.45 + c.z * 1.5 + c.y * 0.4);
        p.x += gustX * 0.035 + flutterX * 0.014 * wind;
        p.z += gustZ * 0.03 + flutterZ * 0.014 * wind;
        p.y += abs(gustX) * 0.008 * wind;
        if (wind > 1.2) {
            vec2 d = p.xz - uPlayer.xz;
            float dist = length(d);
            float h = clamp((p.y - (c.y - 0.5)) / 1.05, 0.0, 1.0);
            float spd = length(uPlayerVel);
            float push = smoothstep(1.35, 0.10, dist) * h * (0.40 + 0.60 * smoothstep(0.0, 3.2, spd));
            vec2 dir = dist > 0.001 ? normalize(d) : vec2(1.0, 0.0);
            float shake = sin(uTime * 17.0 + c.x * 4.4 + c.z * 3.2) * 0.11;
            float sway = sin(uTime * 10.0 + dist * 7.0) * 0.07;
            p.xz += dir * push * (0.38 + shake);
            p.xz += vec2(-dir.y, dir.x) * push * sway;
            p.y -= push * 0.10;
        }
    }
    vWorld = p;
    vNormal = normalize(gl_Normal);
    vAlbedo = gl_Color.rgb;
    vLamp = gl_Color.a;
    vPack = gl_MultiTexCoord0;
    vUv = gl_MultiTexCoord1.xy;
    vTex = gl_MultiTexCoord1.z;
    vLeaf = gl_MultiTexCoord1.w;
    gl_Position = gl_ModelViewProjectionMatrix * vec4(p, 1.0);
}
)GLSL";

static const char* kOpaqueFs = R"GLSL(
#version 330 compatibility
in vec3 vWorld;
in vec3 vNormal;
in vec3 vAlbedo;
in vec4 vPack;
in vec2 vUv;
in float vTex;
in float vLeaf;
in float vLamp;
out vec4 fragColor;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uMoonDir;
uniform vec3 uMoonColor;
uniform vec3 uAmbient;
uniform vec3 uSky;
uniform float uTime;
uniform float uSunAmt;
uniform vec3 uCam;
uniform sampler2D uShadowMap;
uniform sampler2D uAlbedo;
uniform mat4 uShadowMat;
uniform float uShadowSize;

float shadowVis(vec3 world, float ndl) {
    if (ndl < 0.02 || uSunAmt < 0.05) return 1.0;
    vec4 sc = uShadowMat * vec4(world, 1.0);
    if (sc.w <= 0.0) return 1.0;
    vec3 p = sc.xyz / sc.w;
    if (p.x < 0.0 || p.y < 0.0 || p.x > 1.0 || p.y > 1.0) return 1.0;
    float bias = 0.0012 + 0.012 * (1.0 - clamp(ndl, 0.0, 1.0));
    vec2 texel = vec2(1.0 / max(uShadowSize, 1.0));
    float sum = 0.0;
    sum += (texture(uShadowMap, p.xy + vec2(-texel.x, -texel.y)).r + bias > p.z) ? 1.0 : 0.0;
    sum += (texture(uShadowMap, p.xy + vec2(texel.x, -texel.y)).r + bias > p.z) ? 1.0 : 0.0;
    sum += (texture(uShadowMap, p.xy + vec2(-texel.x, texel.y)).r + bias > p.z) ? 1.0 : 0.0;
    sum += (texture(uShadowMap, p.xy + vec2(texel.x, texel.y)).r + bias > p.z) ? 1.0 : 0.0;
    return sum * 0.25;
}

void main() {
    vec3 n = normalize(vNormal);
    float sky = clamp(vPack.x, 0.0, 1.0);
    float emit = vPack.y;
    float cau = vPack.z;
    float ao = vPack.w;
    float lamp = clamp(vLamp, 0.0, 1.0);
    vec3 sunD = normalize(uSunDir);
    float ndl = max(dot(n, sunD), 0.0);
    float wrap = clamp(ndl * 0.78 + 0.22, 0.0, 1.0);
    float ndm = max(dot(n, normalize(uMoonDir)), 0.0);
    float vis = shadowVis(vWorld + n * 0.08, ndl);
    float skyVis = mix(0.5, 1.0, smoothstep(0.2, 0.9, sky));
    float hemi = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 albedo = vAlbedo;
    float alpha = 1.0;
    if (vTex > 0.5) {
        vec4 texel = texture(uAlbedo, vUv);
        if (vLeaf > 0.5) {
            if (texel.a < 0.15) discard;
            alpha = texel.a;
        } else if (texel.a < 0.4) discard;
        albedo *= texel.rgb;
    }
    vec3 skyFill = uSky * (0.10 + 0.34 * hemi) * skyVis;
    vec3 groundFill = vec3(0.20, 0.14, 0.08) * (1.0 - hemi) * 0.28 * skyVis;
    vec3 indirect = albedo * (skyFill + groundFill + uAmbient * skyVis) * ao;
    vec3 lampCol = albedo * vec3(1.0, 0.58, 0.24) * lamp * 1.25;
    vec3 sun = albedo * uSunColor * wrap * vis * skyVis * uSunAmt;
    vec3 shadowSky = albedo * uSky * (1.0 - vis) * skyVis * 0.28 * uSunAmt;
    vec3 moon = albedo * uMoonColor * ndm * (0.35 + 0.65 * hemi) * ao;
    float wave = 0.6 + 0.4 * sin(uTime * 2.5 + vWorld.x * 7.5 + vWorld.z * 6.5);
    vec3 caustics = vec3(0.55, 0.95, 0.90) * cau * wave * uSunAmt * skyVis * 0.85;
    vec3 glow = albedo * emit * 2.6;
    vec3 col = indirect + lampCol + sun + shadowSky + moon + caustics + glow;
    float fog = smoothstep(118.0, 168.0, length(vWorld.xz - uCam.xz));
    col = mix(col, uSky, fog);
    fragColor = vec4(col, alpha);
}
)GLSL";

static const char* kWaterVs = R"GLSL(
#version 330 compatibility
out vec3 vWorld;
out vec3 vNormal;
out vec4 vFlow;
out float vAcross;
out float vLip;
uniform float uTime;
uniform sampler2D uRipple;
uniform sampler2D uHeight;
uniform sampler2D uFft;
uniform float uWorld;
uniform vec2 uHeightOrigin;

void main() {
    vec3 p = gl_Vertex.xyz;
    vFlow = gl_MultiTexCoord0;
    vAcross = gl_MultiTexCoord1.x;
    vLip = gl_MultiTexCoord1.y;
    vNormal = gl_Normal;
    vWorld = p;
    gl_Position = gl_ModelViewProjectionMatrix * vec4(p, 1.0);
}
)GLSL";

static const char* kWaterFs = R"GLSL(
#version 330 compatibility
in vec3 vWorld;
in vec3 vNormal;
in vec4 vFlow;
in float vAcross;
in float vLip;
out vec4 fragColor;
uniform vec3 uCam;
uniform vec3 uSky;
uniform vec3 uSunDir;
uniform float uSunAmt;
uniform float uTime;
uniform mat4 uViewProj;
uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform sampler2D uRipple;
uniform sampler2D uHeight;
uniform sampler2D uFft;
uniform sampler2D uShadowMap;
uniform mat4 uShadowMat;
uniform float uWorld;
uniform vec2 uHeightOrigin;

float linearDepth(float d) {
    float z = d * 2.0 - 1.0;
    return (2.0 * 0.12 * 360.0) / (360.12 - z * (360.0 - 0.12));
}

void main() {
    vec3 geo = normalize(vNormal);
    bool surface = geo.y > 0.45;
    if (!surface) {
        if (vFlow.z > 2.5) {
            vec2 uv = vec2(vAcross, vLip);
            float d = length(uv - 0.5) * 2.0;
            if (d > 1.0) discard;
            float rim = smoothstep(1.0, 0.32, d);
            vec3 col = mix(vec3(0.08, 0.38, 0.55), vec3(0.88, 0.96, 1.0), rim);
            fragColor = vec4(col, 0.86 * rim * clamp(vFlow.w, 0.2, 1.0));
            return;
        }
        vec3 view = normalize(uCam - vWorld);
        float fres = pow(1.0 - abs(dot(geo, view)), 3.0);
        vec3 col = mix(vec3(0.02, 0.28, 0.42), uSky * vec3(0.70, 0.88, 1.0), fres * 0.58);
        if (vFlow.z > 0.45 && vFlow.z < 1.6) {
            float along = fract(vWorld.y * 1.65 - uTime * 2.6);
            float streak = smoothstep(0.0, 0.12, along) * smoothstep(0.62, 0.22, along);
            float edge = abs(vAcross * 2.0 - 1.0);
            col += vec3(0.12, 0.22, 0.24) * streak * 0.55;
            float alpha = 0.34 * (1.0 - edge * 0.55) * (0.75 + streak * 0.35);
            fragColor = vec4(col, clamp(alpha, 0.16, 0.48));
            return;
        }
        fragColor = vec4(col, 0.62);
        return;
    }

    bool below = uCam.y < vWorld.y - 0.08;
    vec3 L = normalize(uSunDir);
    if (below) {
        vec3 toSurf = normalize(vWorld - uCam);
        float up = max(dot(toSurf, vec3(0.0, 1.0, 0.0)), 0.0);
        float sun = pow(max(dot(toSurf, L), 0.0), 5.0);
        vec3 windowCol = uSky * vec3(0.82, 0.93, 1.05);
        vec3 waterCol = vec3(0.08, 0.42, 0.48);
        vec3 col = mix(waterCol, windowCol, 0.35 + up * 0.55);
        col += vec3(1.0, 0.97, 0.88) * sun * uSunAmt * 2.2;
        float shaft = sin(vWorld.x * 0.35 + uTime * 0.5) * sin(vWorld.z * 0.28 - uTime * 0.32);
        shaft = smoothstep(0.05, 0.65, shaft * 0.5 + 0.5);
        col += vec3(0.65, 0.95, 0.88) * shaft * uSunAmt * (0.35 + sun);
        fragColor = vec4(col, mix(0.72, 0.35, sun));
        return;
    }

    float dist = length(vWorld.xz - uCam.xz);
    if (dist > 96.0) {
        vec3 farCol = mix(vec3(0.04, 0.24, 0.36), uSky * vec3(0.72, 0.88, 1.0), 0.55);
        float fog = smoothstep(118.0, 168.0, dist);
        farCol = mix(farCol, uSky, fog);
        fragColor = vec4(farCol, 0.86);
        return;
    }
    vec2 uvA = vWorld.xz / 67.0;
    float e = 0.02;
    float hL = texture(uFft, uvA - vec2(e, 0.0)).x;
    float hR = texture(uFft, uvA + vec2(e, 0.0)).x;
    float hD = texture(uFft, uvA - vec2(0.0, e)).x;
    float hU = texture(uFft, uvA + vec2(0.0, e)).x;
    float detail = 1.0 - smoothstep(40.0, 120.0, dist);
    vec3 n = normalize(vec3((hL - hR) * detail, 1.15, (hD - hU) * detail));
    vec2 ruv = (vWorld.xz - uHeightOrigin) / max(uWorld, 1.0);
    float re = 1.5 / max(uWorld, 1.0);
    float hx = texture(uRipple, ruv + vec2(re, 0.0)).r - texture(uRipple, ruv - vec2(re, 0.0)).r;
    float hz = texture(uRipple, ruv + vec2(0.0, re)).r - texture(uRipple, ruv - vec2(0.0, re)).r;
    n = normalize(n + vec3(-hx, 0.0, -hz) * detail * 0.8);

    vec3 view = normalize(uCam - vWorld);
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uScene, 0));
    vec2 distort = n.xz * (0.004 + 0.006 * detail);
    vec2 texCoordRefract = clamp(uv + distort, vec2(0.001), vec2(0.999));

    float sceneD = texture(uDepth, uv).r;
    float thick = sceneD > 0.998 ? 14.0 : max(linearDepth(sceneD) - linearDepth(gl_FragCoord.z), 0.0);
    float shallow = 1.0 - smoothstep(0.35, 1.35, thick);
    float absorb = mix(clamp(0.58 + thick * 0.045, 0.58, 0.94), 0.22, shallow);

    vec3 H = normalize(L + view);
    float ndv = max(dot(n, view), 0.0);
    float fresnel = 0.02 + 0.42 * pow(1.0 - ndv, 4.0);

    vec3 deep = vec3(0.02, 0.20, 0.30);
    vec3 refr = mix(texture(uScene, texCoordRefract).rgb, deep, absorb);
    vec3 sky = uSky * vec3(0.72, 0.88, 1.0);
    vec3 col = mix(refr, sky, clamp(fresnel, 0.0, 0.62));
    col += vec3(0.95, 0.98, 1.0) * pow(max(dot(n, H), 0.0), 64.0) * uSunAmt * 0.35;

    if (thick > 2.4 && uSunAmt > 0.12) {
        float shaft = sin(vWorld.x * 0.48 + vWorld.z * 0.27 + uTime * 0.40);
        shaft *= sin(vWorld.x * 0.17 - vWorld.z * 0.36 + uTime * 0.22);
        shaft = smoothstep(0.12, 0.78, shaft * 0.5 + 0.5);
        float deepAmt = clamp((thick - 2.4) / 5.0, 0.0, 1.0);
        col += vec3(0.45, 0.82, 0.78) * shaft * max(dot(n, L), 0.0) * uSunAmt * deepAmt * 0.42;
    }

    float occ = texture(uHeight, ruv).g;
    float shore = (1.0 - smoothstep(0.15, 0.72, occ)) * shallow;
    col = mix(col, vec3(0.72, 0.90, 0.92), shore * 0.28);
    float fog = smoothstep(118.0, 168.0, dist);
    col = mix(col, uSky, fog);
    float alpha = mix(0.90, 0.62, shallow);
    fragColor = vec4(col, alpha);
}
)GLSL";

static const char* kPlantShadowVs = R"GLSL(
#version 330 compatibility
out vec2 vUv;
out float vTex;
uniform float uTime;
uniform vec3 uPlayer;
uniform vec2 uPlayerVel;
void main() {
    vec3 p = gl_Vertex.xyz;
    float wind = gl_MultiTexCoord2.w;
    if (wind > 0.01) {
        vec3 c = gl_MultiTexCoord2.xyz;
        float gustX = sin(uTime * 0.7 + c.x * 0.16 + c.z * 0.12);
        float gustZ = cos(uTime * 0.55 + c.z * 0.16 + c.x * 0.09);
        float flutterX = sin(uTime * 1.8 + c.x * 1.6 + c.z * 0.7);
        float flutterZ = cos(uTime * 1.45 + c.z * 1.5 + c.y * 0.4);
        p.x += gustX * 0.035 + flutterX * 0.014 * wind;
        p.z += gustZ * 0.03 + flutterZ * 0.014 * wind;
        p.y += abs(gustX) * 0.008 * wind;
        if (wind > 1.2) {
            vec2 d = p.xz - uPlayer.xz;
            float dist = length(d);
            float h = clamp((p.y - (c.y - 0.5)) / 1.05, 0.0, 1.0);
            float spd = length(uPlayerVel);
            float push = smoothstep(1.35, 0.10, dist) * h * (0.40 + 0.60 * smoothstep(0.0, 3.2, spd));
            vec2 dir = dist > 0.001 ? normalize(d) : vec2(1.0, 0.0);
            float shake = sin(uTime * 17.0 + c.x * 4.4 + c.z * 3.2) * 0.11;
            float sway = sin(uTime * 10.0 + dist * 7.0) * 0.07;
            p.xz += dir * push * (0.38 + shake);
            p.xz += vec2(-dir.y, dir.x) * push * sway;
            p.y -= push * 0.10;
        }
    }
    vUv = gl_MultiTexCoord1.xy;
    vTex = gl_MultiTexCoord1.z;
    gl_Position = gl_ModelViewProjectionMatrix * vec4(p, 1.0);
}
)GLSL";

static const char* kPlantShadowFs = R"GLSL(
#version 330 compatibility
in vec2 vUv;
in float vTex;
uniform sampler2D uAlbedo;
void main() {
    if (vTex > 0.5 && texture(uAlbedo, vUv).a < 0.45) discard;
}
)GLSL";

static void bindShadowTexture() {
    glx.ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gShadowTex);
}

bool initBlockShaders() {
    gOpaque = makeGlProgram(kOpaqueVs, kOpaqueFs);
    gWater = makeGlProgram(kWaterVs, kWaterFs);
    gPlantShadow = makeGlProgram(kPlantShadowVs, kPlantShadowFs);
    if (!gOpaque || !gWater || !gPlantShadow) return false;

    glGenTextures(1, &gShadowTex);
    glBindTexture(GL_TEXTURE_2D, gShadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowSize, kShadowSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = { 1, 1, 1, 1 };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

    glx.GenFramebuffers(1, &gShadowFbo);
    glx.BindFramebuffer(GL_FRAMEBUFFER, gShadowFbo);
    glx.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gShadowTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glx.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "Shadow FBO incompleto\n";
    glx.BindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    return true;
}

void beginShadowPass(const DayNightLighting& light, const glm::vec3& focus, int mapSize) {
    if (mapSize < 256) mapSize = 256;
    if (mapSize > kShadowSize) mapSize = kShadowSize;
    if (mapSize != gShadowMapSize) {
        glBindTexture(GL_TEXTURE_2D, gShadowTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, mapSize, mapSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        gShadowMapSize = mapSize;
    }
    glm::vec3 dir = glm::normalize(light.sunDir);
    glm::vec3 up = std::abs(dir.y) > 0.92f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::vec3 eye = focus + dir * 70.0f;
    glm::mat4 view = glm::lookAt(eye, focus, up);
    const float ext = 42.0f;
    glm::mat4 proj = glm::ortho(-ext, ext, -ext, ext, 2.0f, 160.0f);
    glm::mat4 bias = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
    gShadowMat = bias * proj * view;

    glx.BindFramebuffer(GL_FRAMEBUFFER, gShadowFbo);
    glViewport(0, 0, gShadowMapSize, gShadowMapSize);
    glx.UseProgram(0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.6f, 3.0f);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glClear(GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(glm::value_ptr(proj));
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(glm::value_ptr(view));
}

void endShadowPass(int viewW, int viewH) {
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glx.BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, viewW, viewH);
    glDrawBuffer(GL_BACK);
}

void bindOpaqueShader(const DayNightLighting& light, float time) {
    glx.UseProgram(gOpaque);
    auto loc = [&](const char* n) { return glx.GetUniformLocation(gOpaque, n); };
    glx.Uniform3f(loc("uSunDir"), light.sunDir.x, light.sunDir.y, light.sunDir.z);
    glm::vec3 sun = light.sunColor * light.sunIntensity;
    glx.Uniform3f(loc("uSunColor"), sun.x, sun.y, sun.z);
    glx.Uniform3f(loc("uMoonDir"), light.moonDir.x, light.moonDir.y, light.moonDir.z);
    glx.Uniform3f(loc("uMoonColor"), light.moonColor.x, light.moonColor.y, light.moonColor.z);
    glx.Uniform3f(loc("uAmbient"), light.ambientRGB.x, light.ambientRGB.y, light.ambientRGB.z);
    glx.Uniform3f(loc("uSky"), light.skyColor.x, light.skyColor.y, light.skyColor.z);
    glx.Uniform1f(loc("uTime"), time);
    glx.Uniform1f(loc("uSunAmt"), light.sunIntensity);
    glm::vec3 pp = cameraPosition();
    glm::vec3 pv = cameraVelocity();
    glx.Uniform3f(loc("uPlayer"), pp.x, pp.y, pp.z);
    glx.Uniform2f(loc("uPlayerVel"), pv.x, pv.z);
    glx.Uniform3f(loc("uCam"), pp.x, pp.y, pp.z);
    glx.Uniform1i(loc("uShadowMap"), 0);
    glx.Uniform1i(loc("uAlbedo"), 1);
    glx.UniformMatrix4fv(loc("uShadowMat"), 1, GL_FALSE, glm::value_ptr(gShadowMat));
    glx.Uniform1f(loc("uShadowSize"), (float)gShadowMapSize);
    bindShadowTexture();
    glx.ActiveTexture(GL_TEXTURE1);
    glEnable(GL_TEXTURE_2D);
    glx.ActiveTexture(GL_TEXTURE0);
}

void bindPlantShadowShader(float time) {
    glx.UseProgram(gPlantShadow);
    auto loc = [&](const char* n) { return glx.GetUniformLocation(gPlantShadow, n); };
    glm::vec3 pp = cameraPosition();
    glm::vec3 pv = cameraVelocity();
    glx.Uniform1f(loc("uTime"), time);
    glx.Uniform3f(loc("uPlayer"), pp.x, pp.y, pp.z);
    glx.Uniform2f(loc("uPlayerVel"), pv.x, pv.z);
    glx.Uniform1i(loc("uAlbedo"), 1);
}

void bindWaterShader(const glm::mat4& viewProj, const glm::vec3& camPos, const glm::vec3& sky, const glm::vec3& sunDir, float sunAmt, float time) {
    glx.UseProgram(gWater);
    auto loc = [&](const char* n) { return glx.GetUniformLocation(gWater, n); };
    glx.Uniform3f(loc("uCam"), camPos.x, camPos.y, camPos.z);
    glx.Uniform3f(loc("uSky"), sky.x, sky.y, sky.z);
    glx.Uniform3f(loc("uSunDir"), sunDir.x, sunDir.y, sunDir.z);
    glx.Uniform1f(loc("uSunAmt"), sunAmt);
    glx.Uniform1f(loc("uTime"), time);
    glx.UniformMatrix4fv(loc("uViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
    glx.Uniform1i(loc("uScene"), 2);
    glx.Uniform1i(loc("uDepth"), 3);
    glx.Uniform1i(loc("uRipple"), 5);
    glx.Uniform1i(loc("uHeight"), 4);
    glx.Uniform1i(loc("uFft"), 7);
    glx.Uniform1i(loc("uShadowMap"), 6);
    glx.Uniform1f(loc("uWorld"), fluidRippleWorldSize());
    float ox = 0.0f, oz = 0.0f;
    fluidHeightOrigin(ox, oz);
    glx.Uniform2f(loc("uHeightOrigin"), ox, oz);
    glx.UniformMatrix4fv(loc("uShadowMat"), 1, GL_FALSE, glm::value_ptr(gShadowMat));
    fluidBindHeight(4);
    fluidBindRipple(5);
    fluidBindFft(7);
    glx.ActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, gShadowTex);
    glx.ActiveTexture(GL_TEXTURE0);
}

void bindFixedPipeline() {
    glx.UseProgram(0);
    glx.ActiveTexture(GL_TEXTURE1);
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    glx.ActiveTexture(GL_TEXTURE0);
}
