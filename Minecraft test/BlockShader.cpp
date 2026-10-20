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
static unsigned gShadowFbo = 0;
static unsigned gShadowTex = 0;
static glm::mat4 gShadowMat(1.0f);
static const int kShadowSize = 1024;

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

float shadowVis(vec3 world, float ndl) {
    if (ndl < 0.02 || uSunAmt < 0.05) return 1.0;
    vec4 sc = uShadowMat * vec4(world, 1.0);
    if (sc.w <= 0.0) return 1.0;
    vec3 p = sc.xyz / sc.w;
    if (p.x < 0.0 || p.y < 0.0 || p.x > 1.0 || p.y > 1.0) return 1.0;
    vec2 texel = vec2(1.0 / 1024.0);
    float sum = 0.0;
    sum += (texture(uShadowMap, p.xy + vec2(-texel.x, -texel.y)).r + 0.0045 > p.z) ? 1.0 : 0.0;
    sum += (texture(uShadowMap, p.xy + vec2(texel.x, -texel.y)).r + 0.0045 > p.z) ? 1.0 : 0.0;
    sum += (texture(uShadowMap, p.xy + vec2(-texel.x, texel.y)).r + 0.0045 > p.z) ? 1.0 : 0.0;
    sum += (texture(uShadowMap, p.xy + vec2(texel.x, texel.y)).r + 0.0045 > p.z) ? 1.0 : 0.0;
    return mix(0.55, 1.0, sum * 0.25);
}

void main() {
    vec3 n = normalize(vNormal);
    float sky = clamp(vPack.x, 0.0, 1.0);
    float emit = vPack.y;
    float cau = vPack.z;
    float ao = vPack.w;
    float lamp = clamp(vLamp, 0.0, 1.0);
    float ndl = max(dot(n, normalize(uSunDir)), 0.0);
    float ndm = max(dot(n, normalize(uMoonDir)), 0.0);
    float vis = shadowVis(vWorld + n * 0.05, ndl);
    float skyVis = smoothstep(0.2, 0.78, sky);
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
    vec3 indirect = albedo * (vec3(0.028) + uAmbient * skyVis + uSky * sky * 0.42) * ao;
    vec3 lampCol = albedo * vec3(1.0, 0.62, 0.28) * lamp * 1.15;
    vec3 sun = albedo * uSunColor * ndl * vis * skyVis * uSunAmt;
    vec3 moon = albedo * uMoonColor * ndm * skyVis * ao;
    float wave = 0.6 + 0.4 * sin(uTime * 2.5 + vWorld.x * 7.5 + vWorld.z * 6.5);
    vec3 caustics = vec3(0.55, 0.95, 0.90) * cau * wave * uSunAmt * skyVis * 0.85;
    vec3 glow = albedo * emit * 2.6;
    vec3 col = indirect + lampCol + sun + moon + caustics + glow;
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
    if (gl_Normal.y > 0.5) {
        vec3 a = textureLod(uFft, p.xz / 73.0, 0.0).xyz;
        vec3 b = textureLod(uFft, p.xz / 29.0 + vec2(0.37, 0.18), 0.0).xyz;
        vec2 hUV = (p.xz - uHeightOrigin) / uWorld;
        float occ = textureLod(uHeight, hUV, 0.0).g;
        float edge = smoothstep(0.2, 0.9, occ);
        float rip = textureLod(uRipple, hUV, 0.0).r;
        p.y += (a.x * 0.42 + b.x * 0.28 + rip * 0.06) * edge;
    }
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

float sunVis(vec3 world) {
    if (uSunAmt < 0.08) return 0.0;
    vec4 sc = uShadowMat * vec4(world, 1.0);
    if (sc.w <= 0.0) return 1.0;
    vec3 p = sc.xyz / sc.w;
    if (p.x < 0.0 || p.y < 0.0 || p.x > 1.0 || p.y > 1.0) return 1.0;
    float d = texture(uShadowMap, p.xy).r;
    return (d + 0.0035 > p.z) ? 1.0 : 0.0;
}

vec3 waterGodRays(vec3 world, vec3 n, float thick) {
    if (uSunAmt < 0.12 || thick < 1.15) return vec3(0.0);
    vec3 sun = normalize(uSunDir);
    float hit = max(dot(n, sun), 0.0);
    if (hit < 0.02) return vec3(0.0);
    float vis0 = sunVis(world + n * 0.10);
    if (vis0 < 0.15) return vec3(0.0);
    vec3 into = -sun;
    float maxD = min(thick, 9.0);
    float acc = 0.0;
    for (int i = 1; i <= 3; ++i) {
        float t = float(i) / 3.0 * maxD;
        vec3 p = world + into * t;
        float surfaceH = texture(uHeight, (p.xz - uHeightOrigin) / uWorld).r;
        if (surfaceH < 0.08) continue;
        float under = surfaceH - p.y;
        if (under < 0.0) continue;
        float vis = sunVis(p + vec3(0.0, 0.12, 0.0));
        float beam = sin(p.x * 1.85 + p.z * 1.12 + uTime * 0.38);
        beam *= sin(p.x * 0.52 - p.z * 0.74 + uTime * 0.21);
        beam = smoothstep(0.12, 0.78, beam * 0.5 + 0.5);
        float scatter = exp(-under * 0.20) * exp(-t * 0.07);
        acc += vis * beam * scatter;
    }
    acc *= 0.33;
    float viewAlign = pow(max(dot(normalize(uCam - world), sun), 0.0), 3.5);
    float deep = clamp((thick - 1.1) / 4.2, 0.0, 1.0);
    return vec3(0.62, 0.90, 1.0) * acc * uSunAmt * hit * (0.28 + 0.95 * viewAlign) * deep * 1.35;
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

    vec2 uvA = vWorld.xz / 73.0;
    vec2 uvB = vWorld.xz / 29.0 + vec2(0.37, 0.18);
    float e = 0.018;
    float hL = texture(uFft, uvA - vec2(e, 0.0)).x * 0.62 + texture(uFft, uvB - vec2(e, 0.0)).x * 0.38;
    float hR = texture(uFft, uvA + vec2(e, 0.0)).x * 0.62 + texture(uFft, uvB + vec2(e, 0.0)).x * 0.38;
    float hD = texture(uFft, uvA - vec2(0.0, e)).x * 0.62 + texture(uFft, uvB - vec2(0.0, e)).x * 0.38;
    float hU = texture(uFft, uvA + vec2(0.0, e)).x * 0.62 + texture(uFft, uvB + vec2(0.0, e)).x * 0.38;
    float dist = length(vWorld.xz - uCam.xz);
    float detail = 1.0 - smoothstep(36.0, 110.0, dist);
    vec3 n = normalize(vec3((hL - hR) * detail, 1.05, (hD - hU) * detail));
    vec2 ruv = (vWorld.xz - uHeightOrigin) / max(uWorld, 1.0);
    float re = 1.5 / max(uWorld, 1.0);
    float hx = texture(uRipple, ruv + vec2(re, 0.0)).r - texture(uRipple, ruv - vec2(re, 0.0)).r;
    float hz = texture(uRipple, ruv + vec2(0.0, re)).r - texture(uRipple, ruv - vec2(0.0, re)).r;
    n = normalize(n + vec3(-hx, 0.0, -hz) * detail);

    vec3 view = normalize(uCam - vWorld);
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uScene, 0));
    vec2 distort = n.xz * 0.012 * detail;
    vec2 texCoordRefract = clamp(uv + distort, vec2(0.001), vec2(0.999));

    float sceneD = texture(uDepth, uv).r;
    float thick = sceneD > 0.998 ? 14.0 : max(linearDepth(sceneD) - linearDepth(gl_FragCoord.z), 0.0);
    float absorb = clamp(0.72 + thick * 0.06, 0.72, 0.96);

    vec3 L = normalize(uSunDir);
    vec3 H = normalize(L + view);
    float ndv = max(dot(n, view), 0.0);
    float fresnel = 0.02 + 0.55 * pow(1.0 - ndv, 4.0);

    vec3 deep = vec3(0.02, 0.18, 0.26);
    vec3 refr = mix(texture(uScene, texCoordRefract).rgb, deep, absorb);
    vec3 sky = uSky * vec3(0.62, 0.82, 0.95);
    vec3 col = mix(refr, sky, clamp(fresnel, 0.0, 0.72));
    col += vec3(1.0, 0.98, 0.92) * pow(max(dot(n, H), 0.0), 80.0) * uSunAmt * 0.65;

    float occ = texture(uHeight, ruv).g;
    float shore = (1.0 - smoothstep(0.15, 0.72, occ)) * (1.0 - smoothstep(0.05, 1.3, thick));
    col = mix(col, vec3(0.78, 0.92, 0.94), shore * 0.42);
    float fog = smoothstep(118.0, 168.0, dist);
    col = mix(col, uSky, fog);
    fragColor = vec4(col, 0.94);
}
)GLSL";

static void bindShadowTexture() {
    glx.ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gShadowTex);
}

bool initBlockShaders() {
    gOpaque = makeGlProgram(kOpaqueVs, kOpaqueFs);
    gWater = makeGlProgram(kWaterVs, kWaterFs);
    if (!gOpaque || !gWater) return false;

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

void beginShadowPass(const DayNightLighting& light, const glm::vec3& focus) {
    glm::vec3 dir = glm::normalize(light.sunDir);
    glm::vec3 up = std::abs(dir.y) > 0.92f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::vec3 eye = focus + dir * 70.0f;
    glm::mat4 view = glm::lookAt(eye, focus, up);
    const float ext = 42.0f;
    glm::mat4 proj = glm::ortho(-ext, ext, -ext, ext, 2.0f, 160.0f);
    glm::mat4 bias = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
    gShadowMat = bias * proj * view;

    glx.BindFramebuffer(GL_FRAMEBUFFER, gShadowFbo);
    glViewport(0, 0, kShadowSize, kShadowSize);
    glx.UseProgram(0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.5f, 4.0f);
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
    bindShadowTexture();
    glx.ActiveTexture(GL_TEXTURE1);
    glEnable(GL_TEXTURE_2D);
    glx.ActiveTexture(GL_TEXTURE0);
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
