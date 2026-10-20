#include "PauseMenu.h"
#include "BlockSelection.h"
#include "Camera.h"
#include "Renderer.h"
#include "World.h"

#include "imgui.h"

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
static bool upPressedLast = false, downPressedLast = false, zPressedLast = false;
static int menuIndex = 0;
static const int MENU_ITEMS = 7;

struct WorldEntry {
    char id[64];
    char name[64];
    unsigned seed;
    int sx, sy, sz;
};

static std::vector<WorldEntry> gWorlds;
static WorldEntry gCurrent{};
static bool gLoadingNew = false;
static int gLoadReady = 5;
static int gLoadFrames = 0;
static char gNameBuf[64] = "Nuevo mundo";
static char gSeedBuf[64] = "";
static unsigned gSeedSerial = 1;

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
    std::fprintf(f, "name=%s\nseed=%u\nsx=%d\nsy=%d\nsz=%d\n", e.name, e.seed, e.sx, e.sy, e.sz);
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
    gameScreen = GameScreen::Title;
    paused = false;
}

bool gameMenuInWorld() {
    return gameScreen == GameScreen::Playing;
}

void gameMenuLeaveWorld() {
    if (gCurrent.id[0]) {
        gCurrent.sx = (int)std::floor(camX);
        gCurrent.sz = (int)std::floor(camZ);
        gCurrent.sy = (int)std::floor(camY - 1.62 - 1.0);
        writeMeta(gCurrent);
        for (size_t i = 0; i < gWorlds.size(); ++i) {
            if (std::strcmp(gWorlds[i].id, gCurrent.id) == 0) {
                gWorlds[i] = gCurrent;
                break;
            }
        }
    }
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
        worldFixSpawn(gCurrent.sx, gCurrent.sy, gCurrent.sz);
        if (blockAt(gCurrent.sx, gCurrent.sy, gCurrent.sz) == Block::Water
            || blockAt(gCurrent.sx, gCurrent.sy + 1, gCurrent.sz) == Block::Water) {
            worldFindSpawn(gCurrent.sx, gCurrent.sy, gCurrent.sz);
            worldFixSpawn(gCurrent.sx, gCurrent.sy, gCurrent.sz);
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
        worldTimeOrigin = glfwGetTime();
        loadProgress = 1.0f;
        gameScreen = GameScreen::Playing;
        paused = false;
        cameraResetMouse();
    }
}

void pauseMenuUpdateInput(GLFWwindow* window, PauseMenuState& state) {
    bool escDown = (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
    if (escDown && !escPressedLastFrame) {
        if (gameScreen == GameScreen::Playing) {
            if (inventoryOpen()) {
                setInventoryOpen(false);
                cameraResetMouse();
            }
            else {
                paused = !paused;
                cameraResetMouse();
                if (paused) menuIndex = 0;
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

    if (paused && gameScreen == GameScreen::Playing) {
        if (upDown && !upPressedLast) menuIndex = (menuIndex - 1 + MENU_ITEMS) % MENU_ITEMS;
        if (downDown && !downPressedLast) menuIndex = (menuIndex + 1) % MENU_ITEMS;

        if (zDown && !zPressedLast) {
            switch (menuIndex) {
            case 0:
                paused = false;
                cameraResetMouse();
                break;
            case 1:
                gameMenuLeaveWorld();
                break;
            case 2:
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
}

static void drawTitle(int width, int height) {
    const float w = 420.0f, h = 260.0f;
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowPos(centerWin(width, height, w, h), ImGuiCond_Always);
    ImGui::Begin("Title", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    ImGui::SetCursorPosX((w - ImGui::CalcTextSize("TerraEngine").x) * 0.5f);
    ImGui::TextUnformatted("TerraEngine");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    float bw = w - 40.0f;
    ImGui::SetCursorPosX(20.0f);
    if (ImGui::Button("Un jugador", ImVec2(bw, 40.0f))) {
        loadWorldList();
        gameScreen = GameScreen::Worlds;
    }
    ImGui::Spacing();
    ImGui::SetCursorPosX(20.0f);
    if (ImGui::Button("Salir", ImVec2(bw, 36.0f)))
        glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
    ImGui::End();
}

static void drawWorlds(int width, int height) {
    const float w = 520.0f, h = 420.0f;
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowPos(centerWin(width, height, w, h), ImGuiCond_Always);
    ImGui::Begin("Mundos", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("Selecciona un mundo");
    ImGui::Separator();
    ImGui::BeginChild("lista", ImVec2(0.0f, 280.0f), true);
    if (gWorlds.empty())
        ImGui::TextDisabled("No hay mundos todavia. Crea uno nuevo.");
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
    if (ImGui::Button("Crear nuevo mundo", ImVec2(240.0f, 34.0f))) {
        std::snprintf(gNameBuf, sizeof(gNameBuf), "Nuevo mundo");
        gSeedBuf[0] = 0;
        gameScreen = GameScreen::Create;
    }
    ImGui::SameLine();
    if (ImGui::Button("Atras", ImVec2(120.0f, 34.0f)))
        gameScreen = GameScreen::Title;
    ImGui::End();
}

static void drawCreate(int width, int height) {
    const float w = 440.0f, h = 260.0f;
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowPos(centerWin(width, height, w, h), ImGuiCond_Always);
    ImGui::Begin("Crear mundo", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("Nombre del mundo");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##name", gNameBuf, sizeof(gNameBuf));
    ImGui::Spacing();
    ImGui::TextUnformatted("Semilla (vacia = aleatoria)");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##seed", gSeedBuf, sizeof(gSeedBuf));
    ImGui::TextDisabled("Cada mundo nuevo usa una semilla distinta, como en Minecraft.");
    ImGui::Spacing();
    if (ImGui::Button("Crear y cargar", ImVec2(200.0f, 36.0f))) {
        WorldEntry e;
        std::memset(&e, 0, sizeof(e));
        std::snprintf(e.id, sizeof(e.id), "w_%u_%u", (unsigned)std::time(nullptr), gSeedSerial);
        if (gNameBuf[0]) std::snprintf(e.name, sizeof(e.name), "%s", gNameBuf);
        else std::snprintf(e.name, sizeof(e.name), "Nuevo mundo");
        e.seed = parseOrRandomSeed(gSeedBuf);
        e.sx = 0; e.sy = SEA_LEVEL; e.sz = 0;
        beginLoad(e, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Atras", ImVec2(120.0f, 36.0f)))
        gameScreen = GameScreen::Worlds;
    ImGui::End();
}

static void drawLoading(int width, int height) {
    const float w = 460.0f, h = 170.0f;
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowPos(centerWin(width, height, w, h), ImGuiCond_Always);
    ImGui::Begin("Carga", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    ImGui::TextUnformatted("Generando el mundo...");
    ImGui::Text("Semilla: %u", gCurrent.seed);
    ImGui::Spacing();
    ImGui::ProgressBar(loadProgress, ImVec2(-1.0f, 22.0f));
    ImGui::TextDisabled("Preparando terreno, luz y mallas.");
    ImGui::End();
}

void pauseMenuRender(int width, int height, const PauseMenuState& state) {
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
    if (menuIndex == 0) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
    ImGui::Text("Reanudar");
    if (menuIndex == 0) ImGui::PopStyleColor();
    if (menuIndex == 1) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
    ImGui::Text("Guardar y salir al menu");
    if (menuIndex == 1) ImGui::PopStyleColor();
    if (menuIndex == 2) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
    ImGui::Text("Salir del juego");
    if (menuIndex == 2) ImGui::PopStyleColor();
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
