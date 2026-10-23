#include "Character.h"

#include "GlExt.h"
#include "GlIncludes.h"
#include "BlockSelection.h"
#include "BlockShader.h"
#include "Camera.h"
#include "Inventory.h"
#include "Config.h"
#include "MathUtil.h"
#include "Renderer.h"
#include "TextureUtil.h"
#include "World.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static const float kEye = 1.62f;
static const float kScale = kEye / 30.0f;

enum PartId {
    Head = 0, Hat, Body, Jacket,
    ArmR, SleeveR, ArmL, SleeveL,
    LegR, PantsR, LegL, PantsL,
    PartCount
};

struct SkinVert { float x, y, z, nx, ny, nz, u, v; };

struct Part {
    unsigned vbo = 0;
    unsigned vao = 0;
    int count = 0;
    std::vector<SkinVert> cpu;
};

struct Pose {
    float bodyYaw = -90.0f;
    float headYaw = 0.0f;
    float headPitch = 0.0f;
    float bodyPitch = 0.0f;
    float armR = 0.08f;
    float armL = 0.08f;
    float legR = 0.0f;
    float legL = 0.0f;
    float breathe = 0.0f;
};

struct HandFrame {
    glm::mat4 arm{ 1.0f };
    glm::mat4 item{ 1.0f };
    glm::vec3 flame{ 0.0f };
};

enum class HeldKind { None, Cube, Cross, Torch };

struct HeldDraw {
    HeldKind kind = HeldKind::None;
    unsigned top = 0;
    unsigned side = 0;
    unsigned bottom = 0;
    glm::vec3 color{ 1.0f };
    float emission = 0.0f;
    bool cutout = false;
};

static Part gPart[PartCount];
static unsigned gProg = 0;
static unsigned gSkin = 0;
static std::string gSkinFile;
static glm::mat4 gView{ 1.0f };
static glm::mat4 gViewInv{ 1.0f };
static int gTexW = 64;
static int gTexH = 64;
static int gScale = 1;
static bool gLegacy = false;
static bool gReady = false;
static bool gStyleForced = false;
static SkinArmStyle gStyle = SkinArmStyle::Classic;
static Pose gPose;
static float gLegYaw = 0.0f;
static float gPrevBodyYaw = -90.0f;
static bool gLegReady = false;
static float gStepDebt = 0.0f;
static float gStepPhase = 0.0f;
static int gStepFoot = 0;
static float gFootYawR = 0.0f;
static float gFootYawL = 0.0f;
static float gLegSpread = 0.0f;
static float gWalk = 0.0f;
static float gPhase = 0.0f;
static float gSwim = 0.0f;
static float gTime = 0.0f;
static float gSwing = 0.0f;
static float gSwingRate = 5.0f;
static float gSwingVis = 0.0f;
static bool gMine = true;
static float gItemGrowMain = 1.0f;
static float gItemGrowOff = 1.0f;
static Block gSeenMain = Block::Air;
static Block gSeenOff = Block::Air;
static bool gHandsPrimed = false;
static float gLand = 0.0f;
static float gGlow = 0.0f;
static glm::vec3 gLightPos(0.0f);
static glm::vec3 gLightColor(0.0f);
static float gLightRange = 0.0f;
static bool gWasGround = true;
static float gPrevVy = 0.0f;

static const char* kVs = R"GLSL(
#version 330 compatibility
out vec3 vWorld;
out vec3 vNormal;
out vec2 vUv;
uniform mat4 uViewInv;
void main() {
    vec4 viewPos = gl_ModelViewMatrix * vec4(gl_Vertex.xyz, 1.0);
    if (viewPos.z < 0.0 && viewPos.z > -0.22)
        viewPos.z = -0.22;
    vWorld = (uViewInv * viewPos).xyz;
    vNormal = normalize(gl_NormalMatrix * gl_Normal);
    vUv = gl_MultiTexCoord0.xy;
    gl_Position = gl_ProjectionMatrix * viewPos;
}
)GLSL";

static const char* kFs = R"GLSL(
#version 330 compatibility
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uSkin;
uniform sampler2D uShadowMap;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uMoonDir;
uniform vec3 uMoonColor;
uniform vec3 uAmbient;
uniform vec3 uSky;
uniform vec3 uCam;
uniform float uSunAmt;
uniform float uSkyLight;
uniform float uBlockLight;
uniform vec3 uHeldPos;
uniform vec3 uHeldColor;
uniform float uHeldRange;
uniform mat4 uShadowMat;
uniform float uShadowSize;
uniform float uFog;

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
    vec4 texel = texture(uSkin, vUv);
    if (texel.a < 0.12) discard;
    vec3 n = normalize(vNormal);
    vec3 albedo = texel.rgb;
    float sky = clamp(uSkyLight, 0.0, 1.0);
    float lamp = clamp(uBlockLight, 0.0, 1.0);
    vec3 sunD = normalize(uSunDir);
    float ndl = max(dot(n, sunD), 0.0);
    float wrap = clamp(ndl * 0.78 + 0.22, 0.0, 1.0);
    float ndm = max(dot(n, normalize(uMoonDir)), 0.0);
    float vis = shadowVis(vWorld + n * 0.04, ndl);
    float hemi = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyFill = uSky * (0.08 + 0.32 * hemi) * sky;
    vec3 groundFill = vec3(0.16, 0.11, 0.07) * (1.0 - hemi) * 0.22 * sky;
    vec3 indirect = albedo * (skyFill + groundFill + uAmbient * sky);
    vec3 lampCol = albedo * vec3(1.0, 0.58, 0.24) * lamp * 1.35;
    vec3 sun = albedo * uSunColor * wrap * vis * sky * uSunAmt;
    vec3 shadowSky = albedo * uSky * (1.0 - vis) * sky * 0.22 * uSunAmt;
    vec3 moon = albedo * uMoonColor * ndm * (0.35 + 0.65 * hemi) * sky;
    vec3 held = vec3(0.0);
    if (uHeldRange > 0.05) {
        vec3 toL = uHeldPos - vWorld;
        float dist = length(toL);
        vec3 dir = toL / max(dist, 0.001);
        float nd = clamp(dot(n, dir) * 0.65 + 0.35, 0.0, 1.0);
        float atten = clamp(1.0 - dist / uHeldRange, 0.0, 1.0);
        atten *= atten;
        held = albedo * uHeldColor * nd * atten;
    }
    vec3 col = indirect + lampCol + sun + shadowSky + moon + held;
    if (uFog > 0.5) {
        float fog = smoothstep(118.0, 168.0, length(vWorld.xz - uCam.xz));
        col = mix(col, uSky, fog);
    }
    fragColor = vec4(col, texel.a);
}
)GLSL";

static float damp(float cur, float target, float dt, float rate) {
    return cur + (target - cur) * (1.0f - std::exp(-dt * rate));
}

static float wrapDeg(float d) {
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

static float dampAngle(float cur, float target, float dt, float rate) {
    return cur + wrapDeg(target - cur) * (1.0f - std::exp(-dt * rate));
}


struct Rect { int u, v, w, h; };
struct BoxUV { Rect right, left, top, bottom, front, back; };

static BoxUV layout(int u, int v, int w, int h, int d) {
    int s = gScale;
    u *= s; v *= s; w *= s; h *= s; d *= s;
    BoxUV b;
    b.right = { u, v + d, d, h };
    b.front = { u + d, v + d, w, h };
    b.left = { u + d + w, v + d, d, h };
    b.back = { u + d + w + d, v + d, w, h };
    b.top = { u + d, v, w, d };
    b.bottom = { u + d + w, v, w, d };
    return b;
}

static void addFace(std::vector<SkinVert>& out,
    glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n, Rect r) {
    float tw = (float)gTexW;
    float th = (float)gTexH;
    float u0 = r.u / tw;
    float u1 = (r.u + r.w) / tw;
    float v0 = r.v / th;
    float v1 = (r.v + r.h) / th;
    glm::vec3 p[4] = { a * kScale, b * kScale, c * kScale, d * kScale };
    float us[4] = { u0, u0, u1, u1 };
    float vs[4] = { v0, v1, v1, v0 };
    for (int i = 0; i < 4; ++i)
        out.push_back({ p[i].x, p[i].y, p[i].z, n.x, n.y, n.z, us[i], vs[i] });
}

static void addBox(std::vector<SkinVert>& out, float x0, float y0, float z0, float x1, float y1, float z1, BoxUV uv, float inflate) {
    x0 -= inflate; y0 -= inflate; z0 -= inflate;
    x1 += inflate; y1 += inflate; z1 += inflate;
    addFace(out, { x0, y1, z1 }, { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { 0, 0, 1 }, uv.front);
    addFace(out, { x1, y1, z0 }, { x1, y0, z0 }, { x0, y0, z0 }, { x0, y1, z0 }, { 0, 0, -1 }, uv.back);
    addFace(out, { x0, y1, z0 }, { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { -1, 0, 0 }, uv.right);
    addFace(out, { x1, y1, z1 }, { x1, y0, z1 }, { x1, y0, z0 }, { x1, y1, z0 }, { 1, 0, 0 }, uv.left);
    addFace(out, { x0, y1, z0 }, { x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 }, { 0, 1, 0 }, uv.top);
    addFace(out, { x0, y0, z1 }, { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { 0, -1, 0 }, uv.bottom);
}

static void uploadPart(Part& part, const std::vector<SkinVert>& verts) {
    if (part.vbo) glx.DeleteBuffers(1, &part.vbo);
    if (part.vao) glx.DeleteVertexArrays(1, &part.vao);
    part.vbo = 0;
    part.vao = 0;
    part.cpu = verts;
    part.count = (int)verts.size();
    if (part.count <= 0) return;
    glx.GenBuffers(1, &part.vbo);
    glx.GenVertexArrays(1, &part.vao);
    glx.BindVertexArray(part.vao);
    glx.BindBuffer(GL_ARRAY_BUFFER, part.vbo);
    glx.BufferData(GL_ARRAY_BUFFER, (int)(verts.size() * sizeof(SkinVert)), verts.data(), GL_STATIC_DRAW);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glx.ClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(SkinVert), (void*)0);
    glNormalPointer(GL_FLOAT, sizeof(SkinVert), (void*)12);
    glTexCoordPointer(2, GL_FLOAT, sizeof(SkinVert), (void*)24);
    glx.BindVertexArray(0);
    glx.BindBuffer(GL_ARRAY_BUFFER, 0);
}

static void buildMeshes() {
    float aw = (gStyle == SkinArmStyle::Slim) ? 1.5f : 2.0f;
    int armW = (gStyle == SkinArmStyle::Slim) ? 3 : 4;
    auto put = [&](int id, float x0, float y0, float z0, float x1, float y1, float z1, int u, int v, int w, int h, int d, float inflate) {
        std::vector<SkinVert> verts;
        addBox(verts, x0, y0, z0, x1, y1, z1, layout(u, v, w, h, d), inflate);
        uploadPart(gPart[id], verts);
    };
    put(Head, -4, 0, -4, 4, 8, 4, 0, 0, 8, 8, 8, 0);
    put(Hat, -4, 0, -4, 4, 8, 4, 32, 0, 8, 8, 8, 0.5f);
    put(Body, -4, -6, -2, 4, 6, 2, 16, 16, 8, 12, 4, 0);
    put(ArmR, -aw, -12, -2, aw, 0, 2, 40, 16, armW, 12, 4, 0);
    put(LegR, -2, -12, -2, 2, 0, 2, 0, 16, 4, 12, 4, 0);
    if (gLegacy) {
        uploadPart(gPart[Jacket], {});
        uploadPart(gPart[SleeveR], {});
        uploadPart(gPart[ArmL], {});
        uploadPart(gPart[SleeveL], {});
        uploadPart(gPart[PantsR], {});
        uploadPart(gPart[LegL], {});
        uploadPart(gPart[PantsL], {});
        return;
    }
    put(Jacket, -4, -6, -2, 4, 6, 2, 16, 32, 8, 12, 4, 0.5f);
    put(SleeveR, -aw, -12, -2, aw, 0, 2, 40, 32, armW, 12, 4, 0.5f);
    put(ArmL, -aw, -12, -2, aw, 0, 2, 32, 48, armW, 12, 4, 0);
    put(SleeveL, -aw, -12, -2, aw, 0, 2, 48, 48, armW, 12, 4, 0.5f);
    put(PantsR, -2, -12, -2, 2, 0, 2, 0, 32, 4, 12, 4, 0.5f);
    put(LegL, -2, -12, -2, 2, 0, 2, 16, 48, 4, 12, 4, 0);
    put(PantsL, -2, -12, -2, 2, 0, 2, 0, 48, 4, 12, 4, 0.5f);
}

static SkinArmStyle readArmStyle() {
    FILE* file = nullptr;
    if (fopen_s(&file, "textures/skins/model.txt", "rb") != 0 || !file)
        return SkinArmStyle::Classic;
    char buf[64] = {};
    size_t n = fread(buf, 1, sizeof(buf) - 1, file);
    fclose(file);
    for (size_t i = 0; i < n; ++i) {
        if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] = (char)(buf[i] - 'A' + 'a');
    }
    if (std::strstr(buf, "slim") || std::strstr(buf, "alex")) return SkinArmStyle::Slim;
    return SkinArmStyle::Classic;
}

static void openSkinConsole() {}

static void skinLog(const std::string&) {}

static std::string absolutePath(const std::string& path) {
    char buf[MAX_PATH];
    DWORD n = GetFullPathNameA(path.c_str(), MAX_PATH, buf, nullptr);
    if (n == 0 || n >= MAX_PATH) return path;
    return buf;
}

static std::string executableDirectory() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string path = n ? std::string(buf, buf + n) : std::string(".");
    size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) return ".";
    return path.substr(0, slash);
}

static bool fileExists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static std::string findSkinFile() {
    char cwd[MAX_PATH];
    DWORD n = GetCurrentDirectoryA(MAX_PATH, cwd);
    skinLog(std::string("Directorio de trabajo: ") + (n ? cwd : "(desconocido)"));
    std::string exe = executableDirectory();
    skinLog("Directorio del ejecutable: " + exe);
    const std::string rel = "textures\\skins\\default.png";
    std::string candidates[] = {
        absolutePath(rel),
        exe + "\\" + rel,
        absolutePath(exe + "\\..\\" + rel),
        absolutePath(exe + "\\..\\..\\" + rel)
    };
    for (const std::string& candidate : candidates) {
        bool exists = fileExists(candidate);
        skinLog(std::string("Probando skin: ") + candidate + (exists ? " [existe]" : " [no existe]"));
        if (exists) return candidate;
    }
    return "";
}

static bool acceptSkinSize(int w, int h) {
    if (w == 64 && (h == 64 || h == 32)) return true;
    if (w == 128 && (h == 128 || h == 64)) return true;
    return false;
}

static void uploadSkin(const unsigned char* pixels, int w, int h, bool legacy) {
    if (gSkin) glDeleteTextures(1, &gSkin);
    gSkin = uploadNearestRGBA(pixels, w, h);
    gTexW = w;
    gTexH = h;
    gScale = w / 64;
    gLegacy = legacy;
    buildMeshes();
}

static bool loadSkinFile() {
    gSkinFile = findSkinFile();
    if (gSkinFile.empty()) {
        skinLog("ERROR: textures/skins/default.png no existe en ninguna ruta buscada. No se genera una skin de reserva.");
        return false;
    }
    int w = 0, h = 0, channels = 0;
    std::vector<unsigned char> pixels;
    while (glGetError() != GL_NO_ERROR) {}
    if (!loadImageRGBA(gSkinFile.c_str(), w, h, pixels, &channels)) {
        skinLog("ERROR: no se pudo decodificar " + gSkinFile);
        return false;
    }
    skinLog("PNG decodificado: " + gSkinFile + " " + std::to_string(w) + "x" + std::to_string(h)
        + " canales de origen " + std::to_string(channels) + " subido como RGBA8");
    if (!acceptSkinSize(w, h)) {
        skinLog("ERROR: resolucion no soportada. Se aceptan 64x64, 64x32, 128x128 y 128x64.");
        return false;
    }
    uploadSkin(pixels.data(), w, h, h * 2 == w);
    GLenum err = glGetError();
    if (!gSkin || err != GL_NO_ERROR) {
        skinLog("ERROR OpenGL al subir la skin. id=" + std::to_string(gSkin) + " glError=" + std::to_string((int)err));
        return false;
    }
    skinLog("Textura OpenGL valida. id=" + std::to_string(gSkin)
        + (gStyle == SkinArmStyle::Slim ? " slim" : " classic"));
    return true;
}

void characterSetArmStyle(SkinArmStyle style) {
    gStyleForced = true;
    if (gStyle == style && gReady) return;
    gStyle = style;
    if (gSkin) buildMeshes();
}

bool initCharacter() {
    openSkinConsole();
    gProg = makeGlProgram(kVs, kFs);
    if (!gProg) {
        std::cerr << "No se pudo compilar el shader del personaje.\n" << lastGlShaderError() << "\n";
        MessageBoxA(nullptr,
            "No se pudo crear el shader del personaje.\nEl mundo seguira sin el modelo.",
            "TerraEngine", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (!gStyleForced) gStyle = readArmStyle();
    if (!loadSkinFile()) {
        skinLog("El personaje no se dibuja porque default.png no cargo.");
        gReady = false;
        return false;
    }
    gReady = true;
    return true;
}

static bool overlayPart(int id);

static void refreshView() {
    glm::vec3 eye, focus;
    cameraRenderFrame(eye, focus);
    gView = glm::lookAt(eye, focus, cameraViewUp());
    gViewInv = glm::inverse(gView);
}

static bool partLocalBounds(int id, glm::vec3& mn, glm::vec3& mx) {
    float aw = (gStyle == SkinArmStyle::Slim) ? 1.5f : 2.0f;
    float inf = overlayPart(id) ? 0.5f : 0.0f;
    auto box = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
        mn = glm::vec3(x0 - inf, y0 - inf, z0 - inf) * kScale;
        mx = glm::vec3(x1 + inf, y1 + inf, z1 + inf) * kScale;
    };
    switch (id) {
    case Body: case Jacket: box(-4, -6, -2, 4, 6, 2); return true;
    case ArmR: case SleeveR: case ArmL: case SleeveL: box(-aw, -12, -2, aw, 0, 2); return true;
    case LegR: case PantsR: case LegL: case PantsL: box(-2, -12, -2, 2, 0, 2); return true;
    default: return false;
    }
}

static bool entirelyInFront(const glm::mat4& world, const glm::vec3& mn, const glm::vec3& mx) {
    for (int i = 0; i < 8; ++i) {
        glm::vec3 p((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
        float viewZ = (gView * world * glm::vec4(p, 1.0f)).z;
        if (viewZ > -0.20f) return false;
    }
    return true;
}

static glm::mat4 pushArmInFront(glm::mat4 viewM) {
    float aw = ((gStyle == SkinArmStyle::Slim) ? 1.5f : 2.0f) + 0.5f;
    float xs = aw * kScale;
    float zs = 2.5f * kScale;
    float y0 = -12.5f * kScale;
    float y1 = 0.5f * kScale;
    float worst = -100.0f;
    for (int i = 0; i < 8; ++i) {
        glm::vec3 p((i & 1) ? xs : -xs, (i & 2) ? y1 : y0, (i & 4) ? zs : -zs);
        float z = (viewM * glm::vec4(p, 1.0f)).z;
        if (z > worst) worst = z;
    }
    if (worst > -0.85f)
        viewM = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -0.85f - worst)) * viewM;
    return viewM;
}

static glm::mat4 playerRoot() {
    float feet = (float)camY - kEye + 0.02f - gLand * 0.07f + gPose.breathe;
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3((float)camX, feet, (float)camZ));
    m = glm::rotate(m, glm::radians(90.0f - gPose.bodyYaw), glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::translate(m, glm::vec3(0.0f, 0.0f, -4.0f * kScale));
}

static glm::mat4 limbMatrix(const glm::vec3& pivotPx, float forward, float outZ, bool mirror, float yawDeg = 0.0f) {
    glm::mat4 m = glm::translate(playerRoot(), pivotPx * kScale);
    if (yawDeg != 0.0f)
        m = glm::rotate(m, glm::radians(yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));
    if (mirror) m = glm::scale(m, glm::vec3(-1.0f, 1.0f, 1.0f));
    m = glm::rotate(m, outZ, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::rotate(m, -forward, glm::vec3(1.0f, 0.0f, 0.0f));
}

static bool lookingDown() {
    return (float)pitch < -22.0f;
}

static glm::mat4 viewmodelArm() {
    float len = 12.0f * kScale;
    float bob = std::sin(gPhase * 2.0f) * 0.018f * gWalk;
    glm::vec3 handPos(0.78f, -0.82f + bob, -1.35f);
    glm::vec3 upArm = glm::normalize(glm::vec3(0.72f, -0.68f, 0.10f));
    glm::vec3 shoulder = handPos + upArm * len;
    glm::vec3 axisX = glm::normalize(glm::cross(upArm, glm::vec3(0.0f, 0.0f, -1.0f)));
    glm::vec3 axisZ = glm::normalize(glm::cross(axisX, upArm));
    float swing = manualClamp(gSwingVis * 0.22f + gPose.armR * 0.08f, -0.12f, 0.28f);
    glm::mat4 basis(1.0f);
    basis[0] = glm::vec4(axisX, 0.0f);
    basis[1] = glm::vec4(upArm, 0.0f);
    basis[2] = glm::vec4(axisZ, 0.0f);
    glm::mat4 viewArm = glm::translate(glm::mat4(1.0f), shoulder)
        * glm::rotate(glm::mat4(1.0f), -swing, axisX)
        * basis;
    return gViewInv * pushArmInFront(viewArm);
}

static glm::mat4 bodyRightArm() {
    float arm = (gStyle == SkinArmStyle::Slim) ? 5.5f : 6.0f;
    float swing = 1.05f + gPose.armR * 0.22f + gSwingVis * 0.40f;
    return limbMatrix(glm::vec3(-arm, 24.0f, 0.0f), swing, -0.12f, false);
}

static glm::mat4 heldMatrixAt(const glm::mat4& armWorld, Block block, glm::vec3& flame, bool left) {
    float side = left ? -1.0f : 1.0f;
    float len = 12.0f * kScale;
    glm::vec3 grip = glm::vec3(armWorld * glm::vec4(0.02f * side, -len + 0.01f, 0.03f, 1.0f));
    glm::vec3 fwd = glm::vec3(playerRoot()[2]);
    fwd.y = 0.0f;
    if (glm::dot(fwd, fwd) < 0.0001f) fwd = glm::vec3(0.0f, 0.0f, -1.0f);
    fwd = glm::normalize(fwd);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(up, fwd));
    if (block == Block::Torch) {
        float s = 0.42f;
        glm::vec3 dir = glm::normalize(up * 0.82f + fwd * 0.50f + right * (0.12f * side));
        glm::vec3 axis = glm::normalize(glm::cross(dir, fwd));
        glm::vec3 bin = glm::normalize(glm::cross(axis, dir));
        glm::mat4 m(1.0f);
        m[0] = glm::vec4(axis * s, 0.0f);
        m[1] = glm::vec4(dir * s, 0.0f);
        m[2] = glm::vec4(bin * s, 0.0f);
        m[3] = glm::vec4(grip, 1.0f);
        flame = grip + dir * (10.0f / 16.0f * s);
        return m;
    }
    float size = isPlant(block) ? 0.30f : 0.20f;
    float yaw = glm::radians((isPlant(block) ? 0.0f : 38.0f) * side);
    glm::vec3 face = glm::normalize(fwd * std::cos(yaw) + right * std::sin(yaw));
    glm::vec3 faceSide = glm::normalize(glm::cross(up, face));
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(faceSide * size, 0.0f);
    m[1] = glm::vec4(up * size, 0.0f);
    m[2] = glm::vec4(face * size, 0.0f);
    m[3] = glm::vec4(grip + fwd * (size * 0.35f), 1.0f);
    flame = grip;
    return m;
}

static glm::mat4 leftArmMatrix() {
    float arm = (gStyle == SkinArmStyle::Slim) ? 5.5f : 6.0f;
    bool raised = cameraFirstPerson() && !lookingDown();
    if (!raised) {
        float out = 0.10f + std::sin(gTime * 1.3f) * 0.02f * (1.0f - gWalk);
        return limbMatrix(glm::vec3(arm, 24.0f, 0.0f), gPose.armL, out, gLegacy);
    }
    float swing = 1.05f + gPose.armL * 0.22f;
    return limbMatrix(glm::vec3(arm, 24.0f, 0.0f), swing, 0.12f, gLegacy);
}

static glm::mat4 slideItem(const glm::mat4& from, const glm::mat4& to, float k) {
    glm::mat4 m = to;
    m[3] = glm::mix(from[3], to[3], k);
    return m;
}

static float swapBlend() {
    float s = inventorySwapAmount();
    float e = s * s * (3.0f - 2.0f * s);
    return 1.0f - e;
}

static glm::vec3 torchTip(const glm::mat4& m) {
    glm::vec3 dir = glm::vec3(m[1]);
    float len = glm::length(dir);
    if (len < 0.0001f) return glm::vec3(m[3]);
    return glm::vec3(m[3]) + dir * ((10.0f / 16.0f * 0.42f) / len);
}

static glm::mat4 shownItem(const glm::mat4& rightArm, const glm::mat4& leftArm, Block block, bool mainHand) {
    glm::vec3 flame;
    glm::mat4 atRight = heldMatrixAt(rightArm, block, flame, false);
    glm::mat4 atLeft = heldMatrixAt(leftArm, block, flame, true);
    float k = swapBlend();
    return mainHand ? slideItem(atLeft, atRight, k) : slideItem(atRight, atLeft, k);
}

static glm::mat4 scaleGrip(const glm::mat4& m, float s) {
    glm::mat4 o = m;
    o[0] *= s;
    o[1] *= s;
    o[2] *= s;
    return o;
}

static HeldDraw heldFor(Block block) {
    const BlockTextures& tex = blockTextures();
    HeldDraw d;
    switch (block) {
    case Block::Air: d.kind = HeldKind::None; break;
    case Block::Torch: d.kind = HeldKind::Torch; d.top = tex.torch; d.side = tex.torchAlt; d.emission = 0.45f; d.cutout = true; break;
    case Block::TallGrass: d.kind = HeldKind::Cross; d.top = tex.tallGrass; d.cutout = true; break;
    case Block::Poppy: d.kind = HeldKind::Cross; d.top = tex.poppy; d.cutout = true; break;
    case Block::Triteleia: d.kind = HeldKind::Cross; d.top = tex.triteleia; d.cutout = true; break;
    case Block::TriteleiaRed: d.kind = HeldKind::Cross; d.top = tex.triteleiaRed; d.cutout = true; break;
    case Block::Grass: d.kind = HeldKind::Cube; d.top = tex.grassTop; d.side = tex.grassSide; d.bottom = tex.dirt; break;
    case Block::Dirt: d.kind = HeldKind::Cube; d.top = d.side = d.bottom = tex.dirt; break;
    case Block::Stone: d.kind = HeldKind::Cube; d.top = d.side = d.bottom = tex.stone; break;
    case Block::Log: d.kind = HeldKind::Cube; d.top = d.bottom = tex.logTop; d.side = tex.logSide; break;
    case Block::Leaves: d.kind = HeldKind::Cube; d.top = d.side = d.bottom = tex.leaves; d.cutout = true; break;
    case Block::Water: d.kind = HeldKind::Cube; d.color = glm::vec3(0.16f, 0.42f, 0.78f); break;
    case Block::Lava: d.kind = HeldKind::Cube; d.color = glm::vec3(0.95f, 0.34f, 0.06f); d.emission = 1.0f; break;
    case Block::Obsidian: d.kind = HeldKind::Cube; d.color = glm::vec3(0.11f, 0.07f, 0.15f); break;
    default: d.kind = HeldKind::Cube; d.color = glm::vec3(0.30f, 0.68f, 0.28f); break;
    }
    return d;
}

void characterSync(float dt, float time, bool motionLocked) {
    if (!gReady) return;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;
    gTime = time;
    PlayerMotion motion = cameraMotion();
    if (motionLocked) {
        motion.horizontalSpeed = 0.0f;
        motion.verticalSpeed = 0.0f;
        motion.sprinting = false;
        motion.swimming = false;
    }

    if (gSwing > 0.0f) {
        gSwing -= dt * gSwingRate;
        if (gSwing < 0.0f) gSwing = 0.0f;
    }
    float swingT = 1.0f - gSwing;
    gSwingVis = gSwing > 0.0f ? 0.5f * (1.0f - std::cos(swingT * PI * 2.0f)) : 0.0f;

    if (!motionLocked && motion.swimming)
        gSwim += dt * (5.4f + motion.horizontalSpeed * 0.35f);
    gPhase = (!motionLocked && motion.swimming) ? gSwim : motion.stepPhase;

    if (!motionLocked && motion.grounded && !gWasGround && gPrevVy < -7.0f) gLand = 1.0f;
    gLand *= std::exp(-dt * 8.0f);
    if (!motionLocked) {
        gWasGround = motion.grounded;
        gPrevVy = motion.verticalSpeed;
    }

    float targetWalk = 0.0f;
    if (!motionLocked && motion.swimming) targetWalk = 1.0f;
    else if (!motionLocked && !motion.flying && motion.grounded && motion.horizontalSpeed > 0.35f)
        targetWalk = motion.sprinting ? 1.0f : 0.78f;
    gWalk = damp(gWalk, targetWalk, dt, motion.horizontalSpeed > 0.35f ? 8.0f : 5.0f);

    float camYaw = (float)yaw;
    float follow = wrapDeg(camYaw - gPose.bodyYaw);
    const float maxFollow = 32.0f;
    if (follow > maxFollow) gPose.bodyYaw = camYaw - maxFollow;
    else if (follow < -maxFollow) gPose.bodyYaw = camYaw + maxFollow;
    gPose.bodyYaw = dampAngle(gPose.bodyYaw, camYaw, dt, 5.5f);
    gPose.headYaw = 0.0f;
    gPose.headPitch = (float)pitch;
    gPose.bodyPitch = damp(gPose.bodyPitch, manualClamp(-(float)pitch * 0.10f, -8.0f, 8.0f), dt, 6.0f);
    gPose.breathe = std::sin(time * 1.55f) * 0.012f;

    bool air = !motionLocked && !motion.grounded && !motion.flying && !motion.swimming;
    bool jumping = air && motion.verticalSpeed > 0.8f;
    bool falling = air && motion.verticalSpeed < -1.1f;
    float amp = motion.sprinting ? 1.15f : 0.72f;
    float wave = std::sin(gPhase);
    float swing = wave * amp * gWalk;
    float armR = 0.08f + std::sin(time * 1.2f) * 0.03f * (1.0f - gWalk);
    float armL = 0.08f + std::sin(time * 1.2f + 0.6f) * 0.03f * (1.0f - gWalk);
    float legR = 0.0f;
    float legL = 0.0f;
    if (motion.swimming && !motionLocked) {
        armR = 0.9f + wave * 0.55f;
        armL = 0.9f - wave * 0.55f;
        legR = 0.25f + wave * 0.35f;
        legL = 0.25f - wave * 0.35f;
    }
    else if (gWalk > 0.02f) {
        armR = -swing * 0.95f;
        armL = swing * 0.95f;
        legR = swing;
        legL = -swing;
    }
    if (jumping) {
        armR = -0.85f;
        armL = -0.85f;
        legR = 0.35f;
        legL = -0.15f;
    }
    else if (falling) {
        armR = 0.55f;
        armL = 0.55f;
        legR = 0.15f;
        legL = 0.15f;
    }
    float idle = 1.0f - manualClamp(gWalk, 0.0f, 1.0f);
    if (motion.swimming || jumping || falling) idle *= 0.2f;
    if (!gLegReady) {
        gPrevBodyYaw = gPose.bodyYaw;
        gLegReady = true;
    }
    float turned = wrapDeg(gPose.bodyYaw - gPrevBodyYaw);
    gPrevBodyYaw = gPose.bodyYaw;
    bool standing = gWalk < 0.08f && !motion.swimming && !jumping && !falling && !motionLocked;
    float legDamp = 12.0f;
    if (!standing) {
        gStepPhase = 0.0f;
        gStepDebt = 0.0f;
        gLegYaw -= turned;
        gLegYaw = damp(gLegYaw, 0.0f, dt, 2.4f + (1.0f - idle) * 5.0f);
        float cap = 14.0f + 24.0f * idle;
        gLegYaw = manualClamp(gLegYaw, -cap, cap);
        float step = glm::radians(gLegYaw) * (0.15f + 0.55f * idle);
        legR -= step;
        legL += step * 0.62f;
        gFootYawR = gLegYaw;
        gFootYawL = gLegYaw;
        gLegSpread = std::abs(gLegYaw) * 0.0055f;
    }
    else {
        gLegYaw = damp(gLegYaw, 0.0f, dt, 8.0f);
        gStepDebt = manualClamp(gStepDebt + turned, -150.0f, 150.0f);
        if (gStepPhase <= 0.0f && std::abs(gStepDebt) > 14.0f)
            gStepPhase = 0.001f;
        if (gStepPhase > 0.0f) {
            legDamp = 6.5f;
            gStepPhase += dt * 1.55f;
            if (gStepPhase > 1.0f) gStepPhase = 1.0f;
            float lift = 0.5f * (1.0f - std::cos(gStepPhase * PI * 2.0f));
            float dir = gStepDebt >= 0.0f ? 1.0f : -1.0f;
            float yawR = 0.0f;
            float yawL = 0.0f;
            if (gStepFoot == 0) {
                legR += lift * 0.13f;
                legL -= lift * 0.03f;
                yawR = dir * lift * 3.2f;
                yawL = -dir * lift * 1.2f;
            }
            else {
                legL += lift * 0.13f;
                legR -= lift * 0.03f;
                yawL = dir * lift * 3.2f;
                yawR = -dir * lift * 1.2f;
            }
            gFootYawR = damp(gFootYawR, yawR, dt, 5.0f);
            gFootYawL = damp(gFootYawL, yawL, dt, 5.0f);
            gLegSpread = damp(gLegSpread, lift * 0.006f, dt, 5.0f);
            if (gStepPhase >= 1.0f) {
                gStepPhase = 0.0f;
                gStepFoot ^= 1;
                gStepDebt -= dir * 24.0f;
                if (std::abs(gStepDebt) < 14.0f) gStepDebt = 0.0f;
            }
        }
        else {
            gFootYawR = damp(gFootYawR, 0.0f, dt, 4.5f);
            gFootYawL = damp(gFootYawL, 0.0f, dt, 4.5f);
            gLegSpread = damp(gLegSpread, 0.0f, dt, 4.5f);
        }
    }
    armR += gSwingVis * (gMine ? 1.45f : 0.72f);
    gPose.armR = damp(gPose.armR, armR, dt, 12.0f);
    gPose.armL = damp(gPose.armL, armL, dt, 12.0f);
    gPose.legR = damp(gPose.legR, legR, dt, legDamp);
    gPose.legL = damp(gPose.legL, legL, dt, legDamp);

    Block mainItem = placeSlotBlock(selectedPlaceSlot());
    Block offItem = inventoryOffhand();
    bool mainTorch = mainItem == Block::Torch;
    bool offTorch = offItem == Block::Torch;
    gGlow = damp(gGlow, (mainTorch || offTorch) ? 1.0f : 0.0f, dt, 8.0f);
    if (mainTorch || offTorch) {
        glm::mat4 rightArm = bodyRightArm();
        glm::mat4 leftArm = leftArmMatrix();
        glm::mat4 shown = shownItem(rightArm, leftArm, mainTorch ? mainItem : offItem, mainTorch);
        gLightPos = torchTip(shown);
    }
    if (!gHandsPrimed) {
        gSeenMain = mainItem;
        gSeenOff = offItem;
        gHandsPrimed = true;
    }
    else {
        if (mainItem != gSeenMain) {
            if (inventorySwapAmount() < 0.08f && mainItem != Block::Air) gItemGrowMain = 0.0f;
            gSeenMain = mainItem;
        }
        if (offItem != gSeenOff) {
            if (inventorySwapAmount() < 0.08f && offItem != Block::Air) gItemGrowOff = 0.0f;
            gSeenOff = offItem;
        }
    }
    gItemGrowMain = damp(gItemGrowMain, 1.0f, dt, 14.0f);
    gItemGrowOff = damp(gItemGrowOff, 1.0f, dt, 14.0f);
    float flick = 0.94f + 0.06f * std::sin(time * 11.0f) * std::sin(time * 7.13f);
    if (gGlow < 0.02f) {
        gLightColor = glm::vec3(0.0f);
        gLightRange = 0.0f;
    }
    else {
        gLightColor = glm::vec3(1.05f, 0.56f, 0.22f) * (1.65f * flick * gGlow);
        gLightRange = 13.0f;
    }
}

void characterCueAction(bool mining) {
    gSwing = 1.0f;
    gSwingRate = mining ? 2.6f : 3.6f;
    gMine = mining;
}

void characterHeldLight(glm::vec3& position, glm::vec3& color, float& range) {
    position = gLightPos;
    color = gLightColor;
    range = gLightRange;
}

static bool overlayPart(int id) {
    return id == Hat || id == Jacket || id == SleeveR || id == SleeveL || id == PantsR || id == PantsL;
}

static void drawPart(int id, const glm::mat4& world, bool clipNear) {
    Part& part = gPart[id];
    if (!part.vao || part.count <= 0) return;
    if (overlayPart(id)) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.5f, -2.0f);
    }
    glx.BindVertexArray(part.vao);
    glDrawArrays(GL_QUADS, 0, part.count);
    glx.BindVertexArray(0);
    if (overlayPart(id)) glDisable(GL_POLYGON_OFFSET_FILL);
}

static void drawPosed(int id, const glm::mat4& m, bool mirror, bool clipNear) {
    if (gPart[id].count <= 0) return;
    glPushMatrix();
    glMultMatrixf(glm::value_ptr(m));
    if (mirror) glFrontFace(GL_CW);
    drawPart(id, m, clipNear);
    if (mirror) glFrontFace(GL_CCW);
    glPopMatrix();
}

static void unbindArrays() {
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glx.ClientActiveTexture(GL_TEXTURE0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glx.ClientActiveTexture(GL_TEXTURE1);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glx.ClientActiveTexture(GL_TEXTURE2);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glx.ClientActiveTexture(GL_TEXTURE0);
    glx.BindBuffer(GL_ARRAY_BUFFER, 0);
    glFrontFace(GL_CCW);
    glDisable(GL_POLYGON_OFFSET_FILL);
}

static void bindCharacter(const DayNightLighting& light, float sky, float lamp, float fog) {
    glx.UseProgram(gProg);
    auto loc = [&](const char* name) { return glx.GetUniformLocation(gProg, name); };
    glm::vec3 sun = light.sunColor * light.sunIntensity;
    glx.Uniform1i(loc("uSkin"), 1);
    glx.Uniform1i(loc("uShadowMap"), 0);
    glx.Uniform3f(loc("uSunDir"), light.sunDir.x, light.sunDir.y, light.sunDir.z);
    glx.Uniform3f(loc("uSunColor"), sun.x, sun.y, sun.z);
    glx.Uniform3f(loc("uMoonDir"), light.moonDir.x, light.moonDir.y, light.moonDir.z);
    glx.Uniform3f(loc("uMoonColor"), light.moonColor.x, light.moonColor.y, light.moonColor.z);
    glx.Uniform3f(loc("uAmbient"), light.ambientRGB.x, light.ambientRGB.y, light.ambientRGB.z);
    glx.Uniform3f(loc("uSky"), light.skyColor.x, light.skyColor.y, light.skyColor.z);
    glm::vec3 cam = cameraPosition();
    glx.Uniform3f(loc("uCam"), cam.x, cam.y, cam.z);
    glx.Uniform1f(loc("uSunAmt"), light.sunIntensity);
    glx.Uniform1f(loc("uSkyLight"), sky);
    glx.Uniform1f(loc("uBlockLight"), lamp);
    glx.Uniform3f(loc("uHeldPos"), gLightPos.x, gLightPos.y, gLightPos.z);
    glx.Uniform3f(loc("uHeldColor"), gLightColor.x, gLightColor.y, gLightColor.z);
    glx.Uniform1f(loc("uHeldRange"), gLightRange);
    glm::mat4 shadow(1.0f);
    float shadowSize = 1024.0f;
    worldShadowParams(shadow, shadowSize);
    glx.UniformMatrix4fv(loc("uShadowMat"), 1, GL_FALSE, glm::value_ptr(shadow));
    glx.Uniform1f(loc("uShadowSize"), shadowSize);
    glx.Uniform1f(loc("uFog"), fog);
    glx.UniformMatrix4fv(loc("uViewInv"), 1, GL_FALSE, glm::value_ptr(gViewInv));
    bindWorldShadowSampler();
    glx.ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gSkin);
    glx.ActiveTexture(GL_TEXTURE0);
}

struct HeldVert {
    float px, py, pz, nx, ny, nz;
    float r, g, b, a;
    float t0x, t0y, t0z, t0w;
    float t1x, t1y, t1z, t1w;
    float t2x, t2y, t2z, t2w;
};

static unsigned gHeldVao = 0;
static unsigned gHeldVbo = 0;

static void ensureHeldMesh() {
    if (gHeldVao) return;
    glx.GenVertexArrays(1, &gHeldVao);
    glx.GenBuffers(1, &gHeldVbo);
    glx.BindVertexArray(gHeldVao);
    glx.BindBuffer(GL_ARRAY_BUFFER, gHeldVbo);
    const int stride = (int)sizeof(HeldVert);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, stride, (void*)0);
    glNormalPointer(GL_FLOAT, stride, (void*)12);
    glColorPointer(4, GL_FLOAT, stride, (void*)24);
    glx.ClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(4, GL_FLOAT, stride, (void*)40);
    glx.ClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(4, GL_FLOAT, stride, (void*)56);
    glx.ClientActiveTexture(GL_TEXTURE2);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(4, GL_FLOAT, stride, (void*)72);
    glx.ClientActiveTexture(GL_TEXTURE0);
    glx.BindVertexArray(0);
    glx.BindBuffer(GL_ARRAY_BUFFER, 0);
}

static void pushHeld(std::vector<HeldVert>& out, const glm::mat4& world,
    const glm::vec3& local, const glm::vec3& nLocal, const glm::vec3& color, float lamp,
    float sky, float emit, float u, float v, unsigned tex, bool cutout) {
    glm::vec3 p = glm::vec3(world * glm::vec4(local, 1.0f));
    glm::vec3 n = glm::mat3(world) * nLocal;
    if (glm::dot(n, n) > 0.0001f) n = glm::normalize(n);
    HeldVert h{};
    h.px = p.x; h.py = p.y; h.pz = p.z;
    h.nx = n.x; h.ny = n.y; h.nz = n.z;
    h.r = color.r; h.g = color.g; h.b = color.b; h.a = lamp;
    h.t0x = sky; h.t0y = emit; h.t0z = 0.0f; h.t0w = 1.0f;
    h.t1x = u; h.t1y = v; h.t1z = tex ? 1.0f : 0.0f; h.t1w = cutout ? 1.0f : 0.0f;
    h.t2x = p.x; h.t2y = p.y; h.t2z = p.z; h.t2w = 0.0f;
    out.push_back(h);
}

static void pushHeldQuad(std::vector<HeldVert>& out, const glm::mat4& world, const glm::vec3& n,
    const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
    unsigned tex, bool cutout, float sky, float lamp, const glm::vec3& color, float emit,
    float u0, float v0, float u1, float v1) {
    pushHeld(out, world, a, n, color, lamp, sky, emit, u0, v0, tex, cutout);
    pushHeld(out, world, b, n, color, lamp, sky, emit, u1, v0, tex, cutout);
    pushHeld(out, world, c, n, color, lamp, sky, emit, u1, v1, tex, cutout);
    pushHeld(out, world, d, n, color, lamp, sky, emit, u0, v1, tex, cutout);
}

static void drawHeldBatch(unsigned tex, const std::vector<HeldVert>& verts) {
    if (verts.empty()) return;
    ensureHeldMesh();
    glx.BindVertexArray(gHeldVao);
    glx.BindBuffer(GL_ARRAY_BUFFER, gHeldVbo);
    glx.BufferData(GL_ARRAY_BUFFER, (int)(verts.size() * sizeof(HeldVert)), verts.data(), GL_DYNAMIC_DRAW);
    glx.ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex);
    glx.ActiveTexture(GL_TEXTURE0);
    glDrawArrays(GL_QUADS, 0, (int)verts.size());
    glx.BindVertexArray(0);
}

static void loadCamera();

static void drawHeld(const glm::mat4& item, Block block, float sky, float lamp, float time) {
    HeldDraw draw = heldFor(block);
    if (draw.kind == HeldKind::None) return;
    loadCamera();
    std::vector<HeldVert> batch;
    auto flush = [&](unsigned tex) {
        drawHeldBatch(tex, batch);
        batch.clear();
    };
    if (draw.kind == HeldKind::Torch) {
        const BlockTextures& tex = blockTextures();
        unsigned flame = tex.torch;
        if (tex.torchAlt && (((int)(time * 7.0f)) & 1)) flame = tex.torchAlt;
        float h = 1.0f / 16.0f;
        float len = 10.0f / 16.0f;
        float tu0 = 7.0f / 16.0f, tu1 = 9.0f / 16.0f, tv0 = 0.0f, tv1 = 10.0f / 16.0f;
        glm::vec3 base(0.0f);
        glm::vec3 tip(0.0f, len, 0.0f);
        auto side = [&](const glm::vec3& out) {
            glm::vec3 across = glm::normalize(glm::cross(glm::vec3(0, 1, 0), out)) * h;
            glm::vec3 o = out * h;
            pushHeldQuad(batch, item, out, base - across + o, base + across + o, tip + across + o, tip - across + o,
                flame, true, sky, lamp, glm::vec3(1), 0.35f, tu0, tv0, tu1, tv1);
        };
        side(glm::vec3(1, 0, 0));
        side(glm::vec3(-1, 0, 0));
        side(glm::vec3(0, 0, 1));
        side(glm::vec3(0, 0, -1));
        pushHeldQuad(batch, item, glm::vec3(0, 1, 0),
            tip + glm::vec3(-h, 0, h), tip + glm::vec3(h, 0, h), tip + glm::vec3(h, 0, -h), tip + glm::vec3(-h, 0, -h),
            flame, true, sky, lamp, glm::vec3(1), 0.8f, tu0, tv1 - 1.0f / 16.0f, tu1, tv1);
        flush(flame);
    }
    else if (draw.kind == HeldKind::Cross) {
        pushHeldQuad(batch, item, glm::vec3(0, 1, 0), { -0.5f, -0.5f, -0.5f }, { 0.5f, -0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f }, { -0.5f, 0.5f, -0.5f },
            draw.top, true, sky, lamp, glm::vec3(1), 0, 0, 0, 1, 1);
        pushHeldQuad(batch, item, glm::vec3(0, 1, 0), { 0.5f, -0.5f, -0.5f }, { -0.5f, -0.5f, 0.5f }, { -0.5f, 0.5f, 0.5f }, { 0.5f, 0.5f, -0.5f },
            draw.top, true, sky, lamp, glm::vec3(1), 0, 0, 0, 1, 1);
        flush(draw.top);
    }
    else {
        const float s = 0.5f;
        auto face = [&](glm::vec3 n, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, unsigned tex) {
            pushHeldQuad(batch, item, n, a, b, c, d, tex, draw.cutout, sky, lamp, draw.color, draw.emission, 0, 0, 1, 1);
            flush(tex);
        };
        face({ 0, 1, 0 }, { -s, s, -s }, { -s, s, s }, { s, s, s }, { s, s, -s }, draw.top);
        face({ 0, -1, 0 }, { -s, -s, -s }, { s, -s, -s }, { s, -s, s }, { -s, -s, s }, draw.bottom);
        face({ 0, 0, 1 }, { -s, -s, s }, { s, -s, s }, { s, s, s }, { -s, s, s }, draw.side);
        face({ 0, 0, -1 }, { s, -s, -s }, { -s, -s, -s }, { -s, s, -s }, { s, s, -s }, draw.side);
        face({ 1, 0, 0 }, { s, -s, s }, { s, -s, -s }, { s, s, -s }, { s, s, s }, draw.side);
        face({ -1, 0, 0 }, { -s, -s, -s }, { -s, -s, s }, { -s, s, s }, { -s, s, -s }, draw.side);
    }
}

static void loadCamera() {
    refreshView();
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(glm::value_ptr(gView));
}

void renderPlayerCharacter(const DayNightLighting& light, float time) {
    if (!gReady) return;
    if (inventoryHud() == HudMode::Clean) return;
    gTime = time;
    float sky = 0.0f, lamp = 0.0f;
    int ix = (int)std::floor(camX);
    int iy = (int)std::floor((float)camY - 0.35f);
    int iz = (int)std::floor(camZ);
    cellLight(ix, iy, iz, sky, lamp);

    GLboolean cull = glIsEnabled(GL_CULL_FACE);
    GLboolean blend = glIsEnabled(GL_BLEND);
    glx.BindVertexArray(0);
    glx.BindBuffer(GL_ARRAY_BUFFER, 0);
    glx.ClientActiveTexture(GL_TEXTURE0);
    loadCamera();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    bindCharacter(light, sky, lamp, 1.0f);

    auto drawBody = [&](int id, const glm::mat4& m, bool mirror) {
        drawPosed(id, m, mirror, true);
    };

    glm::mat4 root = playerRoot();
    bool first = cameraFirstPerson();
    bool showBody = !first || lookingDown();
    glm::mat4 body = glm::translate(root, glm::vec3(0.0f, 18.0f, 0.0f) * kScale);
    body = glm::rotate(body, glm::radians(gPose.bodyPitch), glm::vec3(1.0f, 0.0f, 0.0f));
    if (showBody) {
        drawBody(LegR, limbMatrix(glm::vec3(-2, 12, 0), gPose.legR, -gLegSpread, false, gFootYawR), false);
        if (gLegacy) drawBody(LegR, limbMatrix(glm::vec3(2, 12, 0), gPose.legL, gLegSpread, true, gFootYawL), true);
        else {
            drawBody(LegL, limbMatrix(glm::vec3(2, 12, 0), gPose.legL, gLegSpread, false, gFootYawL), false);
            drawBody(PantsL, limbMatrix(glm::vec3(2, 12, 0), gPose.legL, gLegSpread, false, gFootYawL), false);
        }
        drawBody(PantsR, limbMatrix(glm::vec3(-2, 12, 0), gPose.legR, -gLegSpread, false, gFootYawR), false);
        drawBody(Body, body, false);
        drawBody(Jacket, body, false);
        glm::mat4 leftArm = leftArmMatrix();
        if (gLegacy) drawBody(ArmR, leftArm, true);
        else {
            drawBody(ArmL, leftArm, false);
            drawBody(SleeveL, leftArm, false);
        }
    }
    if (!first) {
        glm::mat4 head = glm::translate(root, glm::vec3(0.0f, 24.0f, 0.0f) * kScale);
        head = glm::rotate(head, glm::radians(-gPose.headPitch), glm::vec3(1.0f, 0.0f, 0.0f));
        drawBody(Head, head, false);
        drawBody(Hat, head, false);
    }

    glm::mat4 rightArm = bodyRightArm();
    glm::mat4 leftArm = leftArmMatrix();
    Block mainItem = placeSlotBlock(selectedPlaceSlot());
    Block offItem = inventoryOffhand();
    bool raised = first && !showBody;
    bool showOffArm = raised && (offItem != Block::Air || inventorySwapAmount() > 0.02f);
    drawBody(ArmR, rightArm, false);
    drawBody(SleeveR, rightArm, false);
    if (showOffArm) {
        if (gLegacy) drawBody(ArmR, leftArm, true);
        else {
            drawBody(ArmL, leftArm, false);
            drawBody(SleeveL, leftArm, false);
        }
    }
    glx.BindVertexArray(0);

    bindOpaqueShader(light, time);
    glEnable(GL_DEPTH_TEST);
    if (mainItem != Block::Air)
        drawHeld(scaleGrip(shownItem(rightArm, leftArm, mainItem, true), gItemGrowMain), mainItem, sky, lamp, time);
    if (offItem != Block::Air)
        drawHeld(scaleGrip(shownItem(rightArm, leftArm, offItem, false), gItemGrowOff), offItem, sky, lamp, time);
    glFrontFace(GL_CCW);
    glDisable(GL_POLYGON_OFFSET_FILL);
    if (!cull) glDisable(GL_CULL_FACE);
    if (!blend) glDisable(GL_BLEND);
    bindFixedPipeline();
}
