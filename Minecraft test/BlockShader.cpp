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
    float ao = vPack.w;
    float lamp = clamp(vLamp, 0.0, 1.0);
    vec3 sunD = normalize(uSunDir);
    float ndl = max(dot(n, sunD), 0.0);
    float wrap = clamp(ndl * 0.78 + 0.22, 0.0, 1.0);
    float ndm = max(dot(n, normalize(uMoonDir)), 0.0);
    float vis = shadowVis(vWorld + n * 0.08, ndl);
    float skyVis = clamp(sky, 0.0, 1.0);
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
    vec3 skyFill = uSky * (0.08 + 0.32 * hemi) * skyVis;
    vec3 groundFill = vec3(0.16, 0.11, 0.07) * (1.0 - hemi) * 0.22 * skyVis;
    vec3 indirect = albedo * (skyFill + groundFill + uAmbient * skyVis) * ao;
    vec3 lampCol = albedo * vec3(1.0, 0.58, 0.24) * lamp * 1.35;
    vec3 sun = albedo * uSunColor * wrap * vis * skyVis * uSunAmt;
    vec3 shadowSky = albedo * uSky * (1.0 - vis) * skyVis * 0.22 * uSunAmt;
    vec3 moon = albedo * uMoonColor * ndm * (0.35 + 0.65 * hemi) * ao * skyVis;
    vec3 glow = albedo * emit * 2.6;
    vec3 col = indirect + lampCol + sun + shadowSky + moon + glow;
    float fog = smoothstep(118.0, 168.0, length(vWorld.xz - uCam.xz));
    col = mix(col, uSky, fog);
    fragColor = vec4(col, alpha);
}
)GLSL";

static const char* kWaterVs = R"GLSL(
#version 330 compatibility
out vec3 vWorld;
out vec3 vNormal;
out vec3 vWaveN;
out vec4 vFlow;
out float vAcross;
out float vLip;
out float vLift;
out float vOpen;
uniform float uTime;

void swell(inout vec3 p, inout vec3 n, vec2 dir, float amp, float len) {
    float k = 6.2831853 / len;
    float w = sqrt(9.8 * k);
    float f = k * dot(dir, p.xz) - w * uTime;
    float s = sin(f);
    float c = cos(f);
    p.y += amp * s;
    n.x -= dir.x * (k * amp) * c;
    n.z -= dir.y * (k * amp) * c;
}

void main() {
    vec3 p = gl_Vertex.xyz;
    float yDelta = gl_MultiTexCoord1.z;
    float growT = gl_MultiTexCoord1.w;
    float growDur = max(gl_MultiTexCoord2.y, 0.05);
    if (abs(yDelta) > 0.0001) {
        float u = clamp((uTime - growT) / growDur, 0.0, 1.0);
        u = u * u * (3.0 - 2.0 * u);
        p.y += yDelta * (1.0 - u);
    }
    vFlow = gl_MultiTexCoord0;
    vAcross = gl_MultiTexCoord1.x;
    vLip = gl_MultiTexCoord1.y;
    vNormal = gl_Normal;
    vLift = 0.0;
    vOpen = 0.0;
    vWaveN = gl_Normal;
    float wgt = gl_MultiTexCoord2.x;
    if (gl_Normal.y > 0.45 && wgt <= 0.01) {
        float flow = clamp(length(vFlow.xy), 0.0, 1.0);
        float amp = mix(0.05, 0.082, flow);
        float f1 = p.x * 1.37 + p.z * 0.81 + uTime * 1.05;
        float f2 = -p.x * 0.53 + p.z * 1.83 - uTime * 0.82;
        float f3 = p.x * 0.62 + p.z * 0.71 + uTime * 0.55;
        float h = sin(f1) * 0.50 + sin(f2) * 0.32 + sin(f3) * 0.18;
        p.y += h * amp;
        float dx = cos(f1) * 1.37 * 0.50 + cos(f2) * (-0.53) * 0.32 + cos(f3) * 0.62 * 0.18;
        float dz = cos(f1) * 0.81 * 0.50 + cos(f2) * 1.83 * 0.32 + cos(f3) * 0.71 * 0.18;
        vec3 n = vec3(-dx * amp, 1.0, -dz * amp);
        n.xz -= vFlow.xy * flow * 0.1;
        vWaveN = normalize(n);
        vOpen = 0.55 + flow * 0.35;
    }
    if (wgt > 0.01) {
        float baseY = p.y;
        vOpen = wgt;
        float scale = wgt;
        vec3 n = vec3(0.0, 1.0, 0.0);
        swell(p, n, normalize(vec2(1.0, 0.18)), 0.48 * scale, 36.0);
        swell(p, n, normalize(vec2(0.15, 1.0)), 0.28 * scale, 22.0);
        swell(p, n, normalize(vec2(-0.72, 0.68)), 0.14 * scale, 13.0);
        if (gl_Normal.y > 0.45) vWaveN = normalize(n);
        vLift = p.y - baseY;
    }
    vWorld = p;
    gl_Position = gl_ModelViewProjectionMatrix * vec4(p, 1.0);
}
)GLSL";

static const char* kWaterFs = R"GLSL(
#version 330 compatibility
in vec3 vWorld;
in vec3 vNormal;
in vec3 vWaveN;
in vec4 vFlow;
in float vAcross;
in float vLip;
in float vLift;
in float vOpen;
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
uniform vec4 uRings[8];
uniform vec4 uWake[16];
uniform vec4 uWakeDir[16];

float ringsH(vec2 p, inout float crest) {
    float h = 0.0;
    for (int i = 0; i < 8; ++i) {
        float amp = uRings[i].w;
        if (amp < 0.001) continue;
        float t = uTime - uRings[i].z;
        if (t < 0.0 || t > 4.6) continue;
        float dist = length(p - uRings[i].xy);
        float behind = t * 5.4 - dist;
        if (behind < 0.0 || behind > 8.0) continue;
        float env = exp(-behind * 0.26) * exp(-t * 0.30) * smoothstep(0.0, 0.2, behind);
        float s = sin(behind * 6.6);
        h += s * env * amp;
        h -= exp(-dist * dist * 2.4) * exp(-t * 1.7) * amp;
        crest += smoothstep(0.45, 1.0, s) * env * amp * 5.0;
    }
    return h;
}

float wakeH(vec2 p, inout float foam) {
    float h = 0.0;
    for (int i = 0; i < 16; ++i) {
        if (uWake[i].w < 0.5) continue;
        float age = uTime - uWake[i].z;
        if (age < 0.0 || age > 4.2) continue;
        vec2 d = p - uWake[i].xy;
        vec2 fwd = uWakeDir[i].xy;
        float lat = d.x * -fwd.y + d.y * fwd.x;
        float al = dot(d, fwd);
        float fade = exp(-age * 0.55);
        foam += exp(-dot(d, d) * 1.15) * fade;
        h += sin(lat * 6.5 - age * 7.5) * exp(-lat * lat * 0.85) * exp(-al * al * 0.7) * fade * 0.05;
        float v = abs(abs(lat) - max(-al, 0.0) * 0.58);
        h += sin(v * 4.6 - age * 6.0) * exp(-v * 0.72) * smoothstep(0.08, 0.9, -al) * fade * 0.035;
    }
    return h;
}

float motionH(vec2 p) {
    float c = 0.0;
    float f = 0.0;
    return ringsH(p, c) + wakeH(p, f);
}

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
        vec2 uv = gl_FragCoord.xy / vec2(textureSize(uScene, 0));
        float sceneD = texture(uDepth, uv).r;
        float thick = sceneD > 0.998 ? 6.0 : max(linearDepth(sceneD) - linearDepth(gl_FragCoord.z), 0.0);
        float deep = smoothstep(0.2, 4.2, thick);
        bool falling = vFlow.z > 0.45 && vFlow.z < 1.6;
        float slide = falling ? 1.0 : 0.28;
        float band = 0.5 + 0.5 * sin(vWorld.y * (falling ? 2.1 : 0.55) + vWorld.x * 0.35 - uTime * (falling ? 2.4 : 0.45));
        band = smoothstep(0.2, 0.85, band);
        vec2 distort = geo.xz * (0.006 + slide * 0.004);
        distort.y += (band - 0.5) * 0.006 * slide;
        vec3 refr = texture(uScene, clamp(uv + distort, vec2(0.001), vec2(0.999))).rgb;
        vec3 tint = mix(vec3(0.12, 0.48, 0.62), vec3(0.02, 0.16, 0.32), deep);
        vec3 view = normalize(uCam - vWorld);
        float fres = pow(1.0 - abs(dot(geo, view)), 3.0);
        vec3 col = mix(refr, tint, mix(0.22, 0.62, deep));
        col = mix(col, uSky * vec3(0.72, 0.88, 1.0), fres * 0.28);
        if (falling) col += vec3(0.55, 0.82, 0.9) * band * 0.18;
        float alpha = mix(0.22, 0.55, deep);
        if (geo.y < -0.45) alpha = mix(0.45, 0.72, deep);
        if (falling) alpha = mix(alpha, 0.48, 0.35);
        fragColor = vec4(col, alpha);
        return;
    }

    bool below = uCam.y < vWorld.y - 0.08;
    vec3 L = normalize(uSunDir);
    vec3 waveN = normalize(vWaveN);
    if (below) {
        vec3 view = normalize(uCam - vWorld);
        float up = clamp(view.y, 0.0, 1.0);
        float window = smoothstep(0.12, 0.78, up);
        vec3 waterCol = vec3(0.015, 0.14, 0.22);
        vec3 skyCol = uSky * vec3(0.82, 0.92, 1.02);
        vec3 col = mix(waterCol, skyCol, window * 0.72);
        float sun = pow(max(dot(normalize(vWorld - uCam), L), 0.0), 28.0) * window;
        col += vec3(1.0, 0.98, 0.92) * sun * uSunAmt * 0.28;
        fragColor = vec4(col, mix(0.9, 0.42, window));
        return;
    }

    float dist = length(vWorld.xz - uCam.xz);
    vec2 uvA = (vWorld.xz + vFlow.xy * uTime * 0.55 * clamp(vFlow.w, 0.0, 1.0)) / 16.0;
    float e = 0.035;
    float hL = texture(uFft, uvA - vec2(e, 0.0)).x;
    float hR = texture(uFft, uvA + vec2(e, 0.0)).x;
    float hD = texture(uFft, uvA - vec2(0.0, e)).x;
    float hU = texture(uFft, uvA + vec2(0.0, e)).x;
    float detail = (1.0 - smoothstep(50.0, 150.0, dist)) * mix(0.35, 1.0, vOpen);
    vec3 n = normalize(waveN + vec3((hL - hR) * detail, 0.0, (hD - hU) * detail));
    vec2 ruv = (vWorld.xz - uHeightOrigin) / max(uWorld, 1.0);
    float re = 1.5 / max(uWorld, 1.0);
    float hx = texture(uRipple, ruv + vec2(re, 0.0)).r - texture(uRipple, ruv - vec2(re, 0.0)).r;
    float hz = texture(uRipple, ruv + vec2(0.0, re)).r - texture(uRipple, ruv - vec2(0.0, re)).r;
    n = normalize(n + vec3(-hx, 0.0, -hz) * detail * 0.55);

    vec3 view = normalize(uCam - vWorld);
    vec3 H = normalize(L + view);
    float ndv = max(dot(n, view), 0.0);
    float ndh = max(dot(n, H), 0.0);
    float fresnel = 0.03 + 0.72 * pow(1.0 - ndv, 4.0);
    vec3 glint = vec3(1.0, 0.98, 0.92) * uSunAmt * (pow(ndh, 10.0) * 0.34 + pow(ndh, 42.0) * 0.72);
    glint *= 1.0 - smoothstep(150.0, 210.0, dist);

    if (dist > 88.0) {
        vec3 sea = mix(vec3(0.03, 0.18, 0.30), uSky * vec3(0.62, 0.82, 0.98), 0.35);
        vec3 col = mix(sea, uSky * vec3(0.78, 0.9, 1.0), clamp(fresnel, 0.0, 0.75));
        col += glint;
        float crest = smoothstep(0.28, 0.62, vLift) * vOpen;
        col = mix(col, vec3(0.82, 0.9, 0.93), crest * 0.35);
        float fog = smoothstep(130.0, 200.0, dist);
        col = mix(col, uSky, fog);
        fragColor = vec4(col, 0.92);
        return;
    }

    float splashCrest = 0.0;
    float wakeFoam = 0.0;
    if (dist < 78.0) {
        float e = 0.11;
        vec2 p = vWorld.xz;
        ringsH(p, splashCrest);
        wakeH(p, wakeFoam);
        float hxM = motionH(p - vec2(e, 0.0)) - motionH(p + vec2(e, 0.0));
        float hzM = motionH(p - vec2(0.0, e)) - motionH(p + vec2(0.0, e));
        n = normalize(n + vec3(hxM, 0.0, hzM) * 4.2);
    }

    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uScene, 0));
    vec2 distort = n.xz * (0.012 + 0.01 * detail);
    vec2 texCoordRefract = clamp(uv + distort, vec2(0.001), vec2(0.999));

    float sceneD = texture(uDepth, uv).r;
    float thick = sceneD > 0.998 ? 14.0 : max(linearDepth(sceneD) - linearDepth(gl_FragCoord.z), 0.0);
    float shallow = 1.0 - smoothstep(0.35, 5.5, thick);
    float absorb = mix(clamp(0.5 + thick * 0.045, 0.5, 0.9), 0.12, shallow);

    ndv = max(dot(n, view), 0.0);
    fresnel = 0.03 + 0.72 * pow(1.0 - ndv, 4.0);
    H = normalize(L + view);
    ndh = max(dot(n, H), 0.0);
    glint = vec3(1.0, 0.98, 0.92) * uSunAmt * (pow(ndh, 10.0) * 0.34 + pow(ndh, 42.0) * 0.72);

    vec3 deep = mix(vec3(0.015, 0.12, 0.22), vec3(0.05, 0.32, 0.38), shallow);
    vec3 refr = mix(texture(uScene, texCoordRefract).rgb, deep, absorb);
    vec3 sky = uSky * vec3(0.70, 0.86, 1.0);
    vec3 col = mix(refr, sky, clamp(fresnel, 0.0, 0.72));
    col += glint;
    float crest = smoothstep(0.22, 0.55, vLift) * vOpen;
    col = mix(col, vec3(0.86, 0.93, 0.95), crest * 0.45);

    float occ = texture(uHeight, ruv).g;
    float shore = (1.0 - smoothstep(0.15, 0.72, occ)) * shallow;
    col = mix(col, vec3(0.72, 0.90, 0.92), shore * 0.28);
    col += vec3(0.90, 0.97, 1.0) * clamp(splashCrest, 0.0, 0.85) * 0.7;
    col = mix(col, vec3(0.82, 0.96, 0.98), clamp(wakeFoam * 0.16, 0.0, 0.36));
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
    fluidBindMotion(gWater);
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
