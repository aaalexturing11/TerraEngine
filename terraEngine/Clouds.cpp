#include "GlIncludes.h"
#include "Clouds.h"

#include "Camera.h"
#include "GlExt.h"
#include "PostFX.h"
#include "World.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

static unsigned gProg = 0;

static const char* kVs = R"GLSL(
#version 330 compatibility
out vec2 vNdc;
void main() {
    vNdc = gl_Vertex.xy;
    gl_Position = vec4(gl_Vertex.xy, 1.0, 1.0);
}
)GLSL";

static const char* kFs = R"GLSL(
#version 330 compatibility
in vec2 vNdc;
out vec4 fragColor;
uniform vec3 uCam;
uniform vec3 uSun;
uniform vec3 uSky;
uniform float uSunAmt;
uniform float uDusk;
uniform float uNight;
uniform float uTime;
uniform float uSeed;
uniform float uY0;
uniform float uY1;
uniform mat4 uInvVP;

float hash(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash(i);
    float n100 = hash(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash(i + vec3(1.0, 1.0, 1.0));
    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    return mix(mix(nx00, nx10, f.y), mix(nx01, nx11, f.y), f.z);
}

float density(vec3 p, float lod) {
    vec3 w = p + vec3(uTime * 0.28, 0.0, uTime * 0.09 + uSeed);
    float bank = noise(w * 0.0032);
    float shape = noise(vec3(w.x * 0.011, w.y * 0.034, w.z * 0.011));
    float d = smoothstep(0.57, 0.78, shape);
    d *= smoothstep(0.46, 0.64, bank);
    if (lod < 0.45) {
        float detail = noise(w * vec3(0.028, 0.05, 0.028) + 3.1);
        d *= mix(mix(0.55, 1.0, smoothstep(0.3, 0.68, detail)), 1.0, lod / 0.45);
    }
    float thick = max(uY1 - uY0, 1.0);
    float belly = noise(vec3(w.x * 0.006, 2.7, w.z * 0.006));
    float h = clamp((p.y - uY0 - belly * 14.0) / thick, 0.0, 1.0);
    d *= smoothstep(0.0, 0.42, h) * smoothstep(1.0, 0.58, h);
    return d;
}

bool slab(vec3 ro, vec3 rd, out float t0, out float t1) {
    if (abs(rd.y) < 1e-4) return false;
    t0 = (uY0 - ro.y) / rd.y;
    t1 = (uY1 - ro.y) / rd.y;
    if (t0 > t1) { float s = t0; t0 = t1; t1 = s; }
    return t1 > 0.0;
}

void main() {
    vec4 farW = uInvVP * vec4(vNdc, 1.0, 1.0);
    vec3 ro = uCam;
    vec3 rd = normalize(farW.xyz / farW.w - ro);
    float t0, t1;
    if (!slab(ro, rd, t0, t1)) discard;
    t0 = max(t0, 0.0);
    t1 = min(t1, 340.0);
    if (t1 <= t0) discard;

    const int STEPS = 16;
    float jitter = hash(vec3(gl_FragCoord.xy, 1.0));
    float t = t0 + jitter * 2.0;
    float trans = 1.0;
    vec3 col = vec3(0.0);
    vec3 sun = normalize(uSun);
    float span = max(t1 - t0, 1.0);
    float dt = clamp(span / 12.0, 4.0, 8.0);
    for (int i = 0; i < STEPS; ++i) {
        if (t >= t1 || trans < 0.04) break;
        if (t + dt > t1) dt = t1 - t;
        if (dt < 0.4) break;
        float lod = smoothstep(90.0, 280.0, t);
        vec3 p = ro + rd * (t + dt * 0.5);
        float d = density(p, lod);
        if (d > 0.02) {
            float light = 0.55 + 0.45 * uSunAmt;
            float lit = light;
            if (lod < 0.9) {
                float ld = density(p + sun * 14.0, 1.0);
                lit = exp(-ld * 1.8) * (0.35 + 0.65 * uSunAmt);
            }
            light = mix(lit, light, smoothstep(0.45, 0.9, lod));
            light = clamp(light, 0.0, 1.0);
            vec3 shade = mix(vec3(0.58, 0.66, 0.76), vec3(1.0, 0.99, 0.97), light);
            shade += vec3(1.0, 0.94, 0.8) * pow(light, 3.0) * uSunAmt * 0.28;
            shade = mix(shade, vec3(1.0, 0.48, 0.28), uDusk * (1.0 - light) * 0.7);
            shade = mix(shade, vec3(0.10, 0.13, 0.22), uNight);
            float a = 1.0 - exp(-d * dt * 0.32);
            col += shade * a * trans;
            trans *= 1.0 - a;
        }
        t += dt;
    }
    float alpha = 1.0 - trans;
    if (alpha < 0.02) discard;
    float fog = smoothstep(250.0, 340.0, t0);
    col = mix(col, uSky * alpha, fog);
    alpha *= 1.0 - fog * 0.45;
    fragColor = vec4(col, alpha);
}
)GLSL";

bool initClouds() {
    gProg = makeGlProgram(kVs, kFs);
    return gProg != 0;
}

void renderClouds(const DayNightLighting& light, float time) {
    if (!gProg || cameraEyesInWater()) return;
    int sw = 1, sh = 1;
    scenePassSize(sw, sh);
    float aspect = sh > 0 ? (float)sw / (float)sh : 1.0f;
    glm::vec3 eye, focus;
    cameraRenderFrame(eye, focus);
    glm::mat4 proj = glm::perspective(glm::radians(cameraFov()), aspect, 0.12f, 360.0f);
    glm::mat4 view = glm::lookAt(eye, focus, cameraViewUp());
    glm::mat4 invVP = glm::inverse(proj * view);

    glx.UseProgram(gProg);
    auto loc = [&](const char* n) { return glx.GetUniformLocation(gProg, n); };
    glx.Uniform3f(loc("uCam"), eye.x, eye.y, eye.z);
    glx.Uniform3f(loc("uSun"), light.sunDir.x, light.sunDir.y, light.sunDir.z);
    glx.Uniform3f(loc("uSky"), light.skyColor.x, light.skyColor.y, light.skyColor.z);
    glx.Uniform1f(loc("uSunAmt"), light.sunIntensity);
    float dusk = light.dawnFactor > light.duskFactor ? light.dawnFactor : light.duskFactor;
    glx.Uniform1f(loc("uDusk"), dusk);
    glx.Uniform1f(loc("uNight"), 1.0f - light.dayFactor);
    glx.Uniform1f(loc("uTime"), time);
    glx.Uniform1f(loc("uSeed"), (float)(worldGetSeed() % 997u) * 19.0f);
    glx.Uniform1f(loc("uY0"), 142.0f);
    glx.Uniform1f(loc("uY1"), 208.0f);
    glx.UniformMatrix4fv(loc("uInvVP"), 1, GL_FALSE, glm::value_ptr(invVP));

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glx.BindVertexArray(0);
    glx.BindBuffer(GL_ARRAY_BUFFER, 0);

    glBegin(GL_QUADS);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(-1.0f, 1.0f);
    glEnd();

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}
