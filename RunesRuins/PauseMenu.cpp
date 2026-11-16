#include "PauseMenu.h"
#include "BlockSelection.h"
#include "Camera.h"
#include "Inventory.h"
#include "Renderer.h"
#include "World.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>

#include "imgui.h"
#include "TextureUtil.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <direct.h>
#include <vector>

bool paused = false;
GameScreen gameScreen = GameScreen::Title;
float loadProgress = 0.0f;
double worldTimeOrigin = 0.0;

static bool escPressedLastFrame = false;
static bool enterPressedLast = false;
static bool upPressedLast = false, downPressedLast = false, zPressedLast = false;
static int menuIndex = 0;
static const int MENU_ITEMS = 7;

struct WorldEntry {
    char id[64];
    char name[64];
    unsigned seed;
    int sx, sy, sz;
    double day;
};

static std::vector<WorldEntry> gWorlds;
static WorldEntry gCurrent{};
static bool gLoadingNew = false;
static int gLoadReady = 5;
static int gLoadFrames = 0;
static char gNameBuf[64] = "Nueva mazmorra";
static char gSeedBuf[64] = "";
static unsigned gSeedSerial = 1;
static TextureId gEngineLogo = 0;
static int gLogoW = 0;
static int gLogoH = 0;
static double gSplashStart = 0.0;
static double gMenuReveal = 0.0;

static float splashSmooth(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static bool splashSkipPressed(GLFWwindow* window) {
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) return true;
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) return true;
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) return true;
    if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS) return true;
    if (glfwGetKey(window, GLFW_KEY_KP_ENTER) == GLFW_PRESS) return true;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) return true;
    for (int k = GLFW_KEY_A; k <= GLFW_KEY_Z; ++k) {
        if (glfwGetKey(window, k) == GLFW_PRESS) return true;
    }
    return false;
}

static void drawTrackedText(ImDrawList* dl, ImVec2 topCenter, const char* text, float size, float gap, ImU32 shadow, ImU32 col) {
    ImFont* font = ImGui::GetFont();
    float width = 0.0f;
    int count = 0;
    for (const char* p = text; *p; ++p) {
        char buf[2] = { *p, 0 };
        width += font->CalcTextSizeA(size, FLT_MAX, 0.0f, buf).x;
        if (p[1]) width += (*p == ' ') ? gap * 1.6f : gap;
        ++count;
    }
    (void)count;
    float x = topCenter.x - width * 0.5f;
    for (const char* p = text; *p; ++p) {
        char buf[2] = { *p, 0 };
        ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, buf);
        dl->AddText(font, size, ImVec2(x + 2.0f, topCenter.y + 2.0f), shadow, buf);
        dl->AddText(font, size, ImVec2(x, topCenter.y), col, buf);
        x += sz.x + ((*p == ' ') ? gap * 1.6f : gap);
    }
}

static void ensureWorldsDir() {
    _mkdir("worlds");
}

static unsigned hashText(const char* s) {
    unsigned h = 2166136261u;
    for (; s && *s; ++s) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    if (h == 0) h = 1;
    return h;
}

static unsigned parseOrRandomSeed(const char* text) {
    if (!text || !text[0]) {
        unsigned s = (unsigned)std::time(nullptr);
        s ^= gSeedSerial++ * 2654435761u;
        s ^= (unsigned)(clock() * 1103515245u);
        if (s == 0) s = 1;
        return s;
    }
    bool digits = true;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') { digits = false; break; }
    }
    if (digits) {
        unsigned v = 0;
        for (const char* p = text; *p; ++p)
            v = v * 10u + (unsigned)(*p - '0');
        return v == 0 ? 1u : v;
    }
    return hashText(text);
}

static void writeMeta(const WorldEntry& e) {
    ensureWorldsDir();
    char dir[160], path[200];
    std::snprintf(dir, sizeof(dir), "worlds/%s", e.id);
    _mkdir(dir);
    std::snprintf(path, sizeof(path), "worlds/%s/meta.txt", e.id);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) return;
    std::fprintf(f, "name=%s\nseed=%u\nsx=%d\nsy=%d\nsz=%d\nday=%.3f\n", e.name, e.seed, e.sx, e.sy, e.sz, e.day);
    std::fclose(f);
}

static bool readMeta(const char* id, WorldEntry& e) {
    char path[200];
    std::snprintf(path, sizeof(path), "worlds/%s/meta.txt", id);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) return false;
    std::memset(&e, 0, sizeof(e));
    std::snprintf(e.id, sizeof(e.id), "%s", id);
    std::snprintf(e.name, sizeof(e.name), "%s", id);
    e.seed = 1;
    e.sx = 0; e.sy = SEA_LEVEL; e.sz = 0;
    char line[160];
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "name=", 5) == 0) {
            std::snprintf(e.name, sizeof(e.name), "%s", line + 5);
            size_t n = std::strlen(e.name);
            while (n && (e.name[n - 1] == '\n' || e.name[n - 1] == '\r')) e.name[--n] = 0;
        }
        else if (std::strncmp(line, "seed=", 5) == 0) e.seed = (unsigned)std::strtoul(line + 5, nullptr, 10);
        else if (std::strncmp(line, "sx=", 3) == 0) e.sx = (int)std::strtol(line + 3, nullptr, 10);
        else if (std::strncmp(line, "sy=", 3) == 0) e.sy = (int)std::strtol(line + 3, nullptr, 10);
        else if (std::strncmp(line, "sz=", 3) == 0) e.sz = (int)std::strtol(line + 3, nullptr, 10);
        else if (std::strncmp(line, "day=", 4) == 0) e.day = std::strtod(line + 4, nullptr);
    }
    std::fclose(f);
    if (e.seed == 0) e.seed = 1;
    return true;
}

static void writeIndex() {
    ensureWorldsDir();
    FILE* f = nullptr;
    if (fopen_s(&f, "worlds/index.txt", "w") != 0 || !f) return;
    for (size_t i = 0; i < gWorlds.size(); ++i)
        std::fprintf(f, "%s\n", gWorlds[i].id);
    std::fclose(f);
}

static void loadWorldList() {
    gWorlds.clear();
    ensureWorldsDir();
    FILE* f = nullptr;
    if (fopen_s(&f, "worlds/index.txt", "r") != 0 || !f) return;
    char id[64];
    while (std::fgets(id, sizeof(id), f)) {
        size_t n = std::strlen(id);
        while (n && (id[n - 1] == '\n' || id[n - 1] == '\r')) id[--n] = 0;
        if (!id[0]) continue;
        WorldEntry e;
        if (readMeta(id, e)) gWorlds.push_back(e);
    }
    std::fclose(f);
}

static void beginLoad(const WorldEntry& e, bool isNew) {
    gCurrent = e;
    gLoadingNew = isNew;
    gLoadFrames = 0;
    loadProgress = 0.02f;
    worldReset();
    clearWorldMeshes();
    worldSetSeed(gCurrent.seed);
    worldBindSave(gCurrent.id);
    if (isNew || (gCurrent.sx == 0 && gCurrent.sz == 0 && gCurrent.sy == SEA_LEVEL)) {
        worldFindSpawn(gCurrent.sx, gCurrent.sy, gCurrent.sz);
    }
    cameraSetSpawn(gCurrent.sx, gCurrent.sy, gCurrent.sz);
    worldTimeOrigin = 0.0;
    gameScreen = GameScreen::Loading;
    paused = false;
    setInventoryOpen(false);
}

static ImVec2 centerWin(int width, int height, float w, float h) {
    return ImVec2(((float)width - w) * 0.5f, ((float)height - h) * 0.5f);
}

void gameMenuInit() {
    loadWorldList();
    gameScreen = GameScreen::Splash;
    paused = false;
    gSplashStart = glfwGetTime();
    gMenuReveal = gSplashStart;
    std::vector<unsigned char> pixels;
    if (loadImageRGBA("textures/images/TerraEngineLogo.png", gLogoW, gLogoH, pixels))
        gEngineLogo = uploadNearestRGBA(pixels.data(), gLogoW, gLogoH);
}

bool gameMenuInWorld() {
    return gameScreen == GameScreen::Playing;
}

void gameMenuSaveProgress() {
    if (!gCurrent.id[0] || gameScreen != GameScreen::Playing) return;
    gCurrent.sx = (int)std::floor(camX);
    gCurrent.sz = (int)std::floor(camZ);
    gCurrent.sy = (int)std::floor(camY - 1.62 - 1.0);
    double elapsed = glfwGetTime() - worldTimeOrigin;
    if (elapsed < 0.0) elapsed = 0.0;
    gCurrent.day = elapsed;
    writeMeta(gCurrent);
    for (size_t i = 0; i < gWorlds.size(); ++i) {
        if (std::strcmp(gWorlds[i].id, gCurrent.id) == 0) {
            gWorlds[i] = gCurrent;
            break;
        }
    }
    worldSaveEdits();
}

void gameMenuLeaveWorld() {
    gameMenuSaveProgress();
    worldReset();
    clearWorldMeshes();
    setInventoryOpen(false);
    paused = false;
    gameScreen = GameScreen::Title;
    loadProgress = 0.0f;
}

void gameMenuTickLoading() {
    if (gameScreen != GameScreen::Loading) return;
    float px = (float)gCurrent.sx + 0.5f;
    float pz = (float)gCurrent.sz + 0.5f;
    worldStreamStep(px, pz, gLoadReady, 1);
    int missC = worldMissingAround(px, pz, gLoadReady);
    int missM = pumpWorldMeshes(2);
    ++gLoadFrames;
    if ((gLoadFrames & 3) == 0) refreshWaterHeights();

    int total = (gLoadReady * 2 + 1) * (gLoadReady * 2 + 1);
    float gen = 1.0f - (float)missC / (float)(total > 0 ? total : 1);
    if (gen < 0.0f) gen = 0.0f;
    if (gen > 1.0f) gen = 1.0f;
    float mesh = (missC == 0) ? (missM <= 0 ? 1.0f : 0.55f) : 0.0f;
    loadProgress = gen * 0.78f + mesh * 0.22f;
    if (loadProgress < 0.04f) loadProgress = 0.04f;

    if (missC == 0 && missM <= 0) {
        refreshWaterHeights();
        if (gLoadingNew) {
            worldFixSpawn(gCurrent.sx, gCurrent.sy, gCurrent.sz);
            if (blockAt(gCurrent.sx, gCurrent.sy, gCurrent.sz) == Block::Water
                || blockAt(gCurrent.sx, gCurrent.sy + 1, gCurrent.sz) == Block::Water) {
                worldFindSpawn(gCurrent.sx, gCurrent.sy, gCurrent.sz);
                worldFixSpawn(gCurrent.sx, gCurrent.sy, gCurrent.sz);
            }
        }
        else {
            for (int n = 0; n < 8; ++n) {
                if (gCurrent.sy < 1) gCurrent.sy = 1;
                if (!solid(gCurrent.sx, gCurrent.sy + 1, gCurrent.sz)
                    && !solid(gCurrent.sx, gCurrent.sy + 2, gCurrent.sz))
                    break;
                ++gCurrent.sy;
            }
        }
        cameraSetSpawn(gCurrent.sx, gCurrent.sy, gCurrent.sz);
        writeMeta(gCurrent);
        if (gLoadingNew) {
            bool found = false;
            for (size_t i = 0; i < gWorlds.size(); ++i) {
                if (std::strcmp(gWorlds[i].id, gCurrent.id) == 0) {
                    gWorlds[i] = gCurrent;
                    found = true;
                    break;
                }
            }
            if (!found) gWorlds.push_back(gCurrent);
            writeIndex();
        }
        worldTimeOrigin = glfwGetTime() - gCurrent.day;
        loadProgress = 1.0f;
        gameScreen = GameScreen::Playing;
        paused = false;
        cameraResetMouse();
    }
}

static bool gTitleDrag = false;
static int gTitleDragX = 0;
static int gTitleDragY = 0;
static const float kTitleBarH = 30.0f;

static void titleBarDrag(GLFWwindow* window, const PauseMenuState& state) {
    if (state.isFullscreen || !gTitleDrag) return;
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) {
        gTitleDrag = false;
        return;
    }
    POINT p;
    GetCursorPos(&p);
    HWND hwnd = glfwGetWin32Window(window);
    RECT rc;
    GetWindowRect(hwnd, &rc);
    MoveWindow(hwnd, p.x - gTitleDragX, p.y - gTitleDragY, rc.right - rc.left, rc.bottom - rc.top, TRUE);
}

void titleBarRender(GLFWwindow* window, int width, int height, const PauseMenuState& state) {
    if (gameScreen == GameScreen::Splash)
        return;
    if (state.isFullscreen) {
        gTitleDrag = false;
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)width, kTitleBarH), ImGuiCond_Always);
    const bool inMenus = gameScreen != GameScreen::Playing;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, inMenus
        ? ImVec4(0.03f, 0.035f, 0.045f, 1.0f)
        : ImVec4(0.84f, 0.38f, 0.09f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.71f, 0.24f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.82f, 0.28f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##RunesRuinsTitle", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(inMenus ? "Runes & Ruins" : "RunesRuins");
    ImGui::SameLine((float)width - 36.0f);
    if (ImGui::Button("X", ImVec2(26.0f, 22.0f))) {
        gameMenuSaveProgress();
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    bool overBar = ImGui::IsWindowHovered() && !ImGui::IsItemHovered();
    if (overBar && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        double mx = 0.0, my = 0.0;
        glfwGetCursorPos(window, &mx, &my);
        gTitleDrag = true;
        gTitleDragX = (int)mx;
        gTitleDragY = (int)my;
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    titleBarDrag(window, state);
}

void pauseMenuUpdateInput(GLFWwindow* window, PauseMenuState& state) {
    if (gameScreen == GameScreen::Splash) {
        double t = glfwGetTime() - gSplashStart;
        if (t >= 4.15 || (t > 0.55 && splashSkipPressed(window))) {
            gameScreen = GameScreen::Title;
            gMenuReveal = glfwGetTime();
        }
        escPressedLastFrame = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        return;
    }

    bool escDown = (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
    if (escDown && !escPressedLastFrame) {
        if (gameScreen == GameScreen::Playing) {
            if (inventoryOpen()) {
                if (inventoryContextOpen()) inventoryCloseContext();
                else {
                    setInventoryOpen(false);
                    cameraResetMouse();
                }
            }
            else {
                paused = !paused;
                cameraResetMouse();
                if (paused) {
                    menuIndex = 0;
                    gameMenuSaveProgress();
                }
            }
        }
        else if (gameScreen == GameScreen::Worlds || gameScreen == GameScreen::Create) {
            gameScreen = GameScreen::Title;
        }
    }
    escPressedLastFrame = escDown;

    bool upDown = (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS);
    bool downDown = (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS);
    bool zDown = (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS);
    bool enterDown = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS
        || glfwGetKey(window, GLFW_KEY_KP_ENTER) == GLFW_PRESS;

    if (paused && gameScreen == GameScreen::Playing) {
        if (upDown && !upPressedLast) menuIndex = (menuIndex - 1 + MENU_ITEMS) % MENU_ITEMS;
        if (downDown && !downPressedLast) menuIndex = (menuIndex + 1) % MENU_ITEMS;

        if ((zDown && !zPressedLast) || (enterDown && !enterPressedLast)) {
            switch (menuIndex) {
            case 0:
                paused = false;
                cameraResetMouse();
                break;
            case 1:
                gameMenuLeaveWorld();
                break;
            case 2:
                gameMenuSaveProgress();
                glfwSetWindowShouldClose(window, GLFW_TRUE);
                break;
            case 3:
                if (!state.isFullscreen) {
                    glfwGetWindowPos(window, &state.windowedPosX, &state.windowedPosY);
                    glfwGetWindowSize(window, &state.windowedWidth, &state.windowedHeight);
                    GLFWmonitor* mon = glfwGetPrimaryMonitor();
                    const GLFWvidmode* mode = glfwGetVideoMode(mon);
                    glfwSetWindowMonitor(window, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
                    state.isFullscreen = true;
                }
                else {
                    glfwSetWindowMonitor(window, NULL, state.windowedPosX, state.windowedPosY,
                        state.windowedWidth, state.windowedHeight, 0);
                    state.isFullscreen = false;
                }
                break;
            case 4:
                if (state.fpsCap > 10) state.fpsCap -= 10;
                else if (state.fpsCap == 10) state.fpsCap = 0;
                break;
            case 5:
                if (state.fpsCap == 0) state.fpsCap = 10;
                else state.fpsCap += 10;
                break;
            case 6:
                state.vsyncEnabled = !state.vsyncEnabled;
                glfwMakeContextCurrent(window);
                glfwSwapInterval(state.vsyncEnabled ? 1 : 0);
                break;
            }
        }
    }

    upPressedLast = upDown;
    downPressedLast = downDown;
    zPressedLast = zDown;
    enterPressedLast = enterDown;
}

static void drawSplash(int width, int height) {
    const double t = glfwGetTime() - gSplashStart;
    const float textIn = splashSmooth((float)((t - 0.35) / 0.70));
    const float logoIn = splashSmooth((float)((t - 0.95) / 0.85));
    const float fadeOut = splashSmooth((float)((t - 3.20) / 0.75));
    const float textA = textIn * (1.0f - fadeOut);
    const float logoA = logoIn * (1.0f - fadeOut);

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)width, (float)height), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##Splash", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float cx = origin.x + (float)width * 0.5f;
    const float cy = origin.y + (float)height * 0.50f;

    float logoW = (float)width * 0.62f;
    float logoH = logoW * 0.18f;
    if (gLogoW > 0 && gLogoH > 0) {
        const float aspect = (float)gLogoW / (float)gLogoH;
        logoH = logoW / aspect;
        const float maxH = (float)height * 0.26f;
        if (logoH > maxH) {
            logoH = maxH;
            logoW = logoH * aspect;
        }
    }
    const float scale = 0.94f + 0.06f * logoIn;
    logoW *= scale;
    logoH *= scale;

    const int textAlpha = (int)(textA * 255.0f);
    const int logoAlpha = (int)(logoA * 255.0f);
    const float labelSize = (float)height * 0.028f > 18.0f ? (float)height * 0.028f : 18.0f;
    drawTrackedText(dl, ImVec2(cx, cy - logoH * 0.5f - labelSize - 28.0f), "POWERED BY",
        labelSize, 3.2f,
        IM_COL32(120, 145, 168, textAlpha),
        IM_COL32(236, 242, 248, textAlpha));

    const float rule = 150.0f * textIn * (1.0f - fadeOut);
    const float ruleY = cy - logoH * 0.5f - 16.0f;
    dl->AddLine(ImVec2(cx - rule, ruleY), ImVec2(cx + rule, ruleY), IM_COL32(170, 190, 208, textAlpha), 1.0f);

    if (gEngineLogo && logoAlpha > 0) {
        const ImVec2 a(cx - logoW * 0.5f, cy - logoH * 0.5f + 8.0f);
        const ImVec2 b(cx + logoW * 0.5f, cy + logoH * 0.5f + 8.0f);
        dl->AddImage((ImTextureID)(intptr_t)gEngineLogo, a, b, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
            IM_COL32(255, 255, 255, logoAlpha));
    }
    else if (logoAlpha > 0) {
        drawTrackedText(dl, ImVec2(cx, cy - 10.0f), "TERRA ENGINE", (float)height * 0.06f, 4.0f,
            IM_COL32(140, 165, 185, logoAlpha), IM_COL32(255, 255, 255, logoAlpha));
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

static bool menuButton(const char* id, const char* label, float width) {
    const ImVec2 size(width, 50.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##btn", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 fill = ImGui::GetColorU32(held
        ? IM_COL32(36, 46, 58, 255)
        : hovered ? IM_COL32(22, 28, 36, 255) : IM_COL32(8, 10, 14, 230));
    const ImU32 border = ImGui::GetColorU32(hovered
        ? IM_COL32(230, 238, 246, 255)
        : IM_COL32(150, 168, 186, 170));
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), fill, 2.0f);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), border, 2.0f, 0, hovered ? 2.0f : 1.0f);

    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * 1.15f;
    const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
    const ImVec2 tp(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f);
    dl->AddText(font, fontSize, ImVec2(tp.x + 2.0f, tp.y + 2.0f), ImGui::GetColorU32(IM_COL32(130, 156, 178, 200)), label);
    dl->AddText(font, fontSize, tp, ImGui::GetColorU32(IM_COL32(245, 248, 252, 255)), label);
    return clicked;
}

static void drawTitle(int width, int height) {
    const float reveal = splashSmooth((float)((glfwGetTime() - gMenuReveal) / 0.55));
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)width, (float)height), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, reveal);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::Begin("##MainMenu", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoSavedSettings);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    const float W = (float)width;
    const float H = (float)height;

    for (int i = 0; i < 36; ++i) {
        unsigned h = (unsigned)i * 1664525u + 1013904223u;
        float x = o.x + (float)(h % 1000) / 1000.0f * W;
        float y = o.y + (float)((h / 1000u) % 1000u) / 1000.0f * H * 0.78f;
        int a = 28 + (int)(h % 40);
        float s = (h & 1) ? 2.0f : 1.0f;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + s, y + s), ImGui::GetColorU32(IM_COL32(210, 224, 236, a)));
    }
    const ImU32 edge = ImGui::GetColorU32(IM_COL32(0, 0, 0, 170));
    const ImU32 mid = ImGui::GetColorU32(IM_COL32(0, 0, 0, 0));
    dl->AddRectFilledMultiColor(o, ImVec2(o.x + W, o.y + H * 0.28f), edge, edge, mid, mid);
    dl->AddRectFilledMultiColor(ImVec2(o.x, o.y + H * 0.72f), ImVec2(o.x + W, o.y + H), mid, mid, edge, edge);

    const char* title = "RUNES & RUINS";
    ImFont* font = ImGui::GetFont();
    float titleSize = H * 0.078f;
    if (titleSize < 36.0f) titleSize = 36.0f;
    if (titleSize > 78.0f) titleSize = 78.0f;
    const ImVec2 ts = font->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, title);
    const ImVec2 tp(o.x + (W - ts.x) * 0.5f, o.y + H * 0.24f);
    dl->AddText(font, titleSize, ImVec2(tp.x + 4.0f, tp.y + 4.0f), ImGui::GetColorU32(IM_COL32(132, 158, 180, 210)), title);
    dl->AddText(font, titleSize, tp, ImGui::GetColorU32(IM_COL32(248, 250, 252, 255)), title);

    const float ruleY = tp.y + ts.y + 16.0f;
    dl->AddLine(ImVec2(o.x + W * 0.5f - 90.0f, ruleY), ImVec2(o.x + W * 0.5f + 90.0f, ruleY),
        ImGui::GetColorU32(IM_COL32(176, 194, 210, 180)), 1.0f);

    const char* tag = "explora  ·  forja  ·  resiste";
    const ImVec2 tagSize = ImGui::CalcTextSize(tag);
    dl->AddText(ImVec2(o.x + (W - tagSize.x) * 0.5f, ruleY + 14.0f),
        ImGui::GetColorU32(IM_COL32(168, 182, 196, 210)), tag);

    const float bw = 320.0f;
    ImGui::SetCursorPos(ImVec2((W - bw) * 0.5f, H * 0.52f));
    if (menuButton("play", "Un jugador", bw)) {
        loadWorldList();
        gameScreen = GameScreen::Worlds;
    }
    ImGui::SetCursorPos(ImVec2((W - bw) * 0.5f, H * 0.52f + 66.0f));
    if (menuButton("quit", "Salir", bw))
        glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

static void pushPanelTheme() {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.05f, 0.06f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.64f, 0.74f, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.06f, 0.07f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.09f, 0.11f, 0.14f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.20f, 0.26f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.28f, 0.36f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.18f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.26f, 0.34f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
}

static void popPanelTheme() {
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(10);
}

static void drawWorlds(int width, int height) {
    const float w = 520.0f, h = 420.0f;
    pushPanelTheme();
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowPos(centerWin(width, height, w, h), ImGuiCond_Always);
    ImGui::Begin("Mazmorras", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("Selecciona una mazmorra");
    ImGui::Separator();
    ImGui::BeginChild("lista", ImVec2(0.0f, 280.0f), true);
    if (gWorlds.empty())
        ImGui::TextDisabled("No hay mazmorras todavia. Crea una nueva.");
    int removeAt = -1;
    for (int i = 0; i < (int)gWorlds.size(); ++i) {
        ImGui::PushID(i);
        ImGui::BeginGroup();
        if (ImGui::Selectable(gWorlds[i].name, false, 0, ImVec2(w - 140.0f, 36.0f)))
            beginLoad(gWorlds[i], false);
        ImGui::SameLine();
        ImGui::TextDisabled("%u", gWorlds[i].seed);
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) removeAt = i;
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (removeAt >= 0) {
        char path[200];
        std::snprintf(path, sizeof(path), "worlds/%s/meta.txt", gWorlds[removeAt].id);
        std::remove(path);
        gWorlds.erase(gWorlds.begin() + removeAt);
        writeIndex();
    }
    if (ImGui::Button("Crear mazmorra", ImVec2(240.0f, 34.0f))) {
        std::snprintf(gNameBuf, sizeof(gNameBuf), "Nueva mazmorra");
        gSeedBuf[0] = 0;
        gameScreen = GameScreen::Create;
    }
    ImGui::SameLine();
    if (ImGui::Button("Atras", ImVec2(120.0f, 34.0f)))
        gameScreen = GameScreen::Title;
    ImGui::End();
    popPanelTheme();
}

static void drawCreate(int width, int height) {
    const float w = 440.0f, h = 260.0f;
    pushPanelTheme();
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowPos(centerWin(width, height, w, h), ImGuiCond_Always);
    ImGui::Begin("Crear mazmorra", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("Nombre de la mazmorra");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##name", gNameBuf, sizeof(gNameBuf));
    ImGui::Spacing();
    ImGui::TextUnformatted("Semilla (vacia = aleatoria)");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##seed", gSeedBuf, sizeof(gSeedBuf));
    ImGui::TextDisabled("Cada mazmorra usa una semilla distinta. El suelo, los muros y el techo se generan solos.");
    ImGui::Spacing();
    if (ImGui::Button("Crear mazmorra", ImVec2(200.0f, 36.0f))) {
        WorldEntry e;
        std::memset(&e, 0, sizeof(e));
        std::snprintf(e.id, sizeof(e.id), "w_%u_%u", (unsigned)std::time(nullptr), gSeedSerial);
        if (gNameBuf[0]) std::snprintf(e.name, sizeof(e.name), "%s", gNameBuf);
        else std::snprintf(e.name, sizeof(e.name), "Nueva mazmorra");
        e.seed = parseOrRandomSeed(gSeedBuf);
        e.sx = 0; e.sy = SEA_LEVEL; e.sz = 0;
        beginLoad(e, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Atras", ImVec2(120.0f, 36.0f)))
        gameScreen = GameScreen::Worlds;
    ImGui::End();
    popPanelTheme();
}

static void drawLoading(int width, int height) {
    const float w = 460.0f, h = 170.0f;
    pushPanelTheme();
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowPos(centerWin(width, height, w, h), ImGuiCond_Always);
    ImGui::Begin("Carga", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    ImGui::TextUnformatted("Generando la mazmorra...");
    ImGui::Text("Semilla: %u", gCurrent.seed);
    ImGui::Spacing();
    ImGui::ProgressBar(loadProgress, ImVec2(-1.0f, 22.0f));
    ImGui::TextDisabled("Levantando muros, suelo y antorchas.");
    ImGui::End();
    popPanelTheme();
}

void pauseMenuRender(int width, int height, const PauseMenuState& state) {
    if (gameScreen == GameScreen::Splash) { drawSplash(width, height); return; }
    if (gameScreen == GameScreen::Title) { drawTitle(width, height); return; }
    if (gameScreen == GameScreen::Worlds) { drawWorlds(width, height); return; }
    if (gameScreen == GameScreen::Create) { drawCreate(width, height); return; }
    if (gameScreen == GameScreen::Loading) { drawLoading(width, height); return; }
    if (!paused) return;

    ImGui::SetNextWindowSize(ImVec2(280.0f, 300.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((width - 280) / 2.0f, (height - 300) / 2.0f), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("PauseMenu", nullptr, flags);
    ImGui::Text("Juego en pausa");
    ImGui::Spacing();
    if (ImGui::Selectable("Reanudar", menuIndex == 0)) {
        menuIndex = 0;
        paused = false;
        cameraResetMouse();
    }
    if (ImGui::Selectable("Guardar y salir al menu", menuIndex == 1)) {
        menuIndex = 1;
        gameMenuLeaveWorld();
    }
    if (ImGui::Selectable("Salir del juego", menuIndex == 2)) {
        menuIndex = 2;
        gameMenuSaveProgress();
        glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
    }
    if (menuIndex == 3) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
    if (state.isFullscreen) ImGui::Text("Modo ventana"); else ImGui::Text("Pantalla completa");
    if (menuIndex == 3) ImGui::PopStyleColor();
    if (menuIndex == 4) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
    ImGui::Text("Bajar limite de FPS");
    if (menuIndex == 4) ImGui::PopStyleColor();
    if (state.fpsCap == 0) ImGui::Text("  Limite actual: Sin limite");
    else ImGui::Text("  Limite actual: %d", state.fpsCap);
    if (menuIndex == 5) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
    ImGui::Text("Subir limite de FPS");
    if (menuIndex == 5) ImGui::PopStyleColor();
    if (menuIndex == 6) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
    if (state.vsyncEnabled) ImGui::Text("VSync (activado)"); else ImGui::Text("VSync (desactivado)");
    if (menuIndex == 6) ImGui::PopStyleColor();
    ImGui::End();
}
