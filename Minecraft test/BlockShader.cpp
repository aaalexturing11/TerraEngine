#include "BlockShader.h"
#include "GlExt.h"
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
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float d = texture(uShadowMap, p.xy + vec2(x, y) * texel).r;
            sum += (d + 0.0022 > p.z) ? 1.0 : 0.0;
        }
    }
    return mix(0.42, 1.0, sum / 9.0);
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
    fragColor = vec4(indirect + lampCol + sun + moon + caustics + glow, alpha);
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
uniform float uWorld;
void main() {
    vec3 p = gl_Vertex.xyz;
    vFlow = gl_MultiTexCoord0;
    vAcross = gl_MultiTexCoord1.x;
    vLip = gl_MultiTexCoord1.y;
    vNormal = gl_Normal;
    float wave = sin(p.x * 1.55 + uTime * 1.2) * 0.012;
    wave += cos(p.z * 1.28 - uTime * 0.85) * 0.01;
    wave += sin((p.x + p.z) * 0.62 + uTime * 0.55) * 0.008;
    if (gl_Normal.y > 0.5) {
        p.y += wave + textureLod(uRipple, p.xz / uWorld, 0.0).r * 0.07;
    }
    else if (vFlow.z < 1.5 && vLip > 1.0) {
        float h = max(vLip - p.y, 0.0);
        p += gl_Normal * sin(p.y * 5.5 - uTime * 3.2) * 0.018 * sqrt(h + 0.05);
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
uniform float uWorld;

float linearDepth(float d) {
    float z = d * 2.0 - 1.0;
    return (2.0 * 0.1 * 250.0) / (250.1 - z * (250.0 - 0.1));
}

void main() {
    vec3 geo = normalize(vNormal);
    bool surface = geo.y > 0.45;
    if (!surface) {
        if (vFlow.z > 2.5) {
            vec2 uv = vec2(vAcross, vLip);
            float d = length(uv - 0.5) * 2.0;
            if (d > 1.0) discard;
            float rim = smoothstep(1.0, 0.35, d);
            vec3 col = mix(vec3(0.08, 0.38, 0.55), vec3(0.88, 0.96, 1.0), rim);
            fragColor = vec4(col, 0.86 * rim * clamp(vFlow.w, 0.2, 1.0));
            return;
        }
        vec3 view = normalize(uCam - vWorld);
        float fres = pow(1.0 - abs(dot(geo, view)), 3.0);
        if (vFlow.z > 1.5) {
            vec3 col = mix(vec3(0.03, 0.32, 0.46), uSky * vec3(0.72, 0.88, 1.0), fres * 0.55);
            fragColor = vec4(col, 0.58);
            return;
        }
        float stream = clamp(vFlow.w, 0.0, 1.0);
        float h = max(vLip - vWorld.y, 0.0);
        float speed = sqrt(h * 2.2 + 0.15);
        float along = fract(sqrt(h + 0.05) * 3.4 - uTime * (0.55 + speed * 0.85));
        float breakup = smoothstep(0.8, 4.5, h);
        float dropLen = mix(0.85, 0.16, breakup);
        float drop = smoothstep(0.0, 0.04, along) * smoothstep(dropLen, dropLen * 0.28, along);
        float satellite = fract(along * 1.7 + 0.38);
        satellite = smoothstep(0.0, 0.03, satellite) * smoothstep(0.13, 0.04, satellite) * breakup;
        float core = (1.0 - breakup * 0.85) * mix(0.15, 0.65, stream);
        float across = abs(vAcross * 2.0 - 1.0);
        float halfW = mix(0.22, 0.55, stream) * mix(1.0, 0.32, breakup);
        float bulge = mix(0.75, 1.15, max(drop, satellite));
        if (across > halfW * bulge) discard;
        if (core < 0.04 && drop < 0.05 && satellite < 0.05) discard;
        vec3 body = mix(vec3(0.03, 0.30, 0.46), vec3(0.72, 0.93, 0.98), max(drop, satellite));
        vec3 col = mix(body, uSky * vec3(0.75, 0.9, 1.0), fres * 0.72);
        float alpha = mix(0.22, 0.78, max(core, max(drop, satellite)));
        fragColor = vec4(col, alpha);
        return;
    }

    vec3 n = normalize(vec3(
        sin(vWorld.x * 1.55 + uTime * 1.2) * 0.045 + vFlow.x * 0.04,
        1.0,
        cos(vWorld.z * 1.28 - uTime * 0.85) * 0.045 + vFlow.y * 0.04));
    {
        vec2 ruv = vWorld.xz / uWorld;
        float e = 1.0 / (uWorld * 4.0);
        float hx = texture(uRipple, ruv + vec2(e, 0)).r - texture(uRipple, ruv - vec2(e, 0)).r;
        float hz = texture(uRipple, ruv + vec2(0, e)).r - texture(uRipple, ruv - vec2(0, e)).r;
        n = normalize(n + vec3(-hx, 0.0, -hz) * 2.2);
    }
    vec3 view = normalize(uCam - vWorld);
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uScene, 0));
    vec2 refr = clamp(uv + n.xz * 0.015, vec2(0.001), vec2(0.999));

    float sceneD = texture(uDepth, uv).r;
    float thick = sceneD > 0.998 ? 14.0 : max(linearDepth(sceneD) - linearDepth(gl_FragCoord.z), 0.0);
    float deepT = clamp(thick / 6.0, 0.0, 1.0);

    float cau = sin(vWorld.x * 5.4 + uTime * 1.3) * sin(vWorld.z * 4.6 - uTime * 1.05);
    cau += sin((vWorld.x + vWorld.z) * 3.1 - uTime * 0.7);
    float cauMul = mix(clamp(0.72 + cau * 0.22, 0.45, 1.25), 1.0, deepT);
    vec3 floorCol = texture(uScene, refr).rgb * cauMul;

    vec3 absorb = exp(-thick * vec3(0.16, 0.05, 0.03));
    vec3 fog = mix(vec3(0.02, 0.48, 0.52), vec3(0.01, 0.10, 0.28), deepT);
    fog = mix(fog, vec3(0.005, 0.04, 0.14), clamp((thick - 5.0) / 9.0, 0.0, 1.0));
    vec3 transmitted = floorCol * absorb + fog * (1.0 - absorb);

    vec3 reflDir = reflect(-view, n);
    vec2 hitUv = uv;
    bool found = false;
    vec3 pos = vWorld + n * 0.06;
    for (int i = 0; i < 5; ++i) {
        pos += reflDir * (0.7 + float(i) * 0.55);
        vec4 clip = uViewProj * vec4(pos, 1.0);
        if (clip.w <= 0.0) break;
        vec3 ndc = clip.xyz / clip.w;
        vec2 ruv = ndc.xy * 0.5 + 0.5;
        if (ruv.x < 0.0 || ruv.y < 0.0 || ruv.x > 1.0 || ruv.y > 1.0) break;
        float hitD = texture(uDepth, ruv).r;
        float rayD = ndc.z * 0.5 + 0.5;
        if (hitD + 0.002 < rayD && hitD < 0.998) {
            hitUv = ruv;
            found = true;
            break;
        }
    }
    vec3 reflected = found ? texture(uScene, hitUv).rgb : uSky * vec3(0.78, 0.9, 1.0);
    float ndv = max(dot(n, view), 0.0);
    float fres = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);
    vec3 col = mix(transmitted, reflected, fres * 0.9);
    float spec = pow(max(dot(reflDir, normalize(uSunDir)), 0.0), 140.0);
    col += vec3(1.0, 0.98, 0.92) * spec * uSunAmt * fres;
    fragColor = vec4(col, 1.0);
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
    glx.Uniform1f(loc("uWorld"), fluidRippleWorldSize());
    fluidBindHeight(4);
    fluidBindRipple(5);
}

void bindFixedPipeline() {
    glx.UseProgram(0);
    glx.ActiveTexture(GL_TEXTURE1);
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    glx.ActiveTexture(GL_TEXTURE0);
}
