#include "GlIncludes.h"
#include "GlExt.h"
#include <thread>
#include <chrono>
#include <cmath>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "BlockSelection.h"
#include "BlockShader.h"
#include "Camera.h"
#include "FluidSolver.h"
#include "Lighting.h"
#include "PauseMenu.h"
#include "PostFX.h"
#include "Renderer.h"
#include "TextureUtil.h"
#include "World.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 800, "Runes & Ruins", NULL, NULL);
    if (!window) {
        glfwDefaultWindowHints();
        window = glfwCreateWindow(800, 800, "Runes & Ruins", NULL, NULL);
    }
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    if (!initGlExtensions() || !initBlockShaders() || !initPostFX()) {
        MessageBoxA(nullptr, "No se pudo iniciar OpenGL 3.3 (shaders).", "Runes & Ruins", MB_OK);
        glfwTerminate();
        return -1;
    }

    glEnable(GL_NORMALIZE);

    TextureId sunTex = loadTexture("textures/sun.png");
    TextureId moonTex = loadTexture("textures/moon.png");
    BlockTextures blockTex;
    blockTex.grassTop = loadTexture("textures/blocks/grass_top.png", true, true);
    blockTex.grassSide = loadTexture("textures/blocks/grass_side.png", true, true);
    blockTex.dirt = loadTexture("textures/blocks/dirt.png", true, true);
    blockTex.stone = loadTexture("textures/blocks/stone_generic.png", true, true);
    blockTex.torch = loadTexture("textures/blocks/torch.png", true, true);
    blockTex.torchAlt = loadTexture("textures/blocks/torch2.png", true, true);
    blockTex.logSide = loadTexture("textures/blocks/oak_log_side_2.png", true, true);
    blockTex.logTop = loadTexture("textures/blocks/oak_log_top.png", true, true);
    blockTex.leaves = loadTexture("textures/blocks/oak_leaves_3.png", true, true);
    blockTex.tallGrass = loadTexture("textures/blocks/grass4.png", true, true);
    blockTex.poppy = loadTexture("textures/blocks/california_poppy.png", true, true);
    blockTex.triteleia = loadTexture("textures/blocks/triteleia.png", true, true);
    blockTex.triteleiaRed = loadTexture("textures/blocks/triteleia_recolor.png", true, true);
    setBlockTextures(blockTex);

    PauseMenuState menuState;
    glfwSwapInterval(menuState.vsyncEnabled ? 1 : 0);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    const char* glsl_version = "#version 130";
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
    ImGui::StyleColorsDark();

    initializeWorld();
    cameraSpawnOnGround();
    glfwSetCursorPosCallback(window, cameraOnMouseMove);
    glfwSetScrollCallback(window, [](GLFWwindow*, double, double yoff) {
        if (paused) return;
        int slot = selectedPlaceSlot();
        if (yoff > 0.0) slot = (slot + 9) % 10;
        else if (yoff < 0.0) slot = (slot + 1) % 10;
        selectPlaceSlot(slot);
    });
    glfwSetMouseButtonCallback(window, handleMouseClick);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    const int renderRadiusXZ = 28;
    const int renderMaxY = 80;
    double lastFrameTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        double frameStart = glfwGetTime();
        float time = (float)frameStart;
        DayNightLighting light = computeDayNightLighting(frameStart);

        glfwPollEvents();
        pollPlaceHotkeys(window);
        pauseMenuUpdateInput(window, menuState);

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (height == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        glm::vec3 camPos = cameraPosition();
        beginShadowPass(light, camPos);
        renderShadowCasters(renderRadiusXZ, renderMaxY);
        endShadowPass(width, height);

        beginScenePass(width, height);
        setup3D(width, height);
        glClearColor(light.skyColor.r, light.skyColor.g, light.skyColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        glLoadIdentity();
        glm::vec3 bob = cameraBobOffset();
        gluLookAt(camX + bob.x, camY + bob.y, camZ + bob.z,
            camX + lookX + bob.x, camY + lookY + bob.y, camZ + lookZ + bob.z, 0, 1, 0);

        glm::vec3 viewDir = cameraLookDirection();
        glDepthMask(GL_FALSE);
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        bindFixedPipeline();
        drawBillboardTextured(camPos + light.sunDir * 100.0f, viewDir, 8.0f, sunTex, 1.15f);
        drawBillboardTextured(camPos + light.moonDir * 100.0f, viewDir, 4.0f, moonTex, 1.15f);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);

        static bool eWasDown = false;
        bool eDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
        if (eDown && !eWasDown && !paused) {
            setInventoryOpen(!inventoryOpen());
            cameraResetMouse();
        }
        eWasDown = eDown;
        bool uiOpen = paused || inventoryOpen();
        cameraSetLookEnabled(!uiOpen);
        glfwSetInputMode(window, GLFW_CURSOR, uiOpen ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);

        cameraUpdateMovement(window, !uiOpen, (float)(frameStart - lastFrameTime));
        if (!uiOpen) {
            updateSelectedBlock(window);
            tickWater((float)(frameStart - lastFrameTime));
        fluidTick((float)(frameStart - lastFrameTime));
        }
        lastFrameTime = frameStart;

        bindOpaqueShader(light, time);
        renderOpaqueBlocks(renderRadiusXZ, renderMaxY);

        blitSceneForWater();
        bindWaterInputs();
        float projM[16], viewM[16];
        glGetFloatv(GL_PROJECTION_MATRIX, projM);
        glGetFloatv(GL_MODELVIEW_MATRIX, viewM);
        glm::mat4 viewProj = glm::make_mat4(projM) * glm::make_mat4(viewM);
        bindWaterShader(viewProj, cameraPosition(), light.skyColor, light.sunDir, light.sunIntensity, time);
        renderWaterBlocks(renderRadiusXZ, renderMaxY);
        bindFixedPipeline();

        if (hasSelected) {
            drawCubeOutline(selectedBlock.x + 0.5f, selectedBlock.y + 0.5f, selectedBlock.z + 0.5f, 1.0f);
        }

        glm::vec3 look = cameraLookDirection();
        glm::vec3 sun = glm::normalize(light.sunDir);
        glm::vec3 right = glm::normalize(glm::cross(look, glm::vec3(0, 1, 0)));
        glm::vec3 upv = glm::normalize(glm::cross(right, look));
        float vx = glm::dot(sun, right);
        float vy = glm::dot(sun, upv);
        float vz = glm::dot(sun, look);
        float aspect = (float)width / (float)height;
        float tanHalf = std::tan(glm::radians(30.0f));
        float denom = vz > 0.15f ? vz : 0.15f;
        float ndcX = (vx / denom) / (aspect * tanHalf);
        float ndcY = (vy / denom) / tanHalf;
        if (vz < 0.15f) {
            float len = std::sqrt(ndcX * ndcX + ndcY * ndcY);
            if (len < 0.001f) len = 1.0f;
            float push = 2.2f + (0.15f - vz) * 2.5f;
            ndcX = ndcX / len * push;
            ndcY = ndcY / len * push;
        }
        glm::vec2 sunUv = glm::vec2(ndcX, ndcY) * 0.5f + 0.5f;
        float facing = glm::clamp((vz + 0.2f) / 0.75f, 0.0f, 1.0f);
        facing = facing * facing * (3.0f - 2.0f * facing);
        float god = facing * light.sunIntensity * 0.34f;
        presentScene(width, height, light, sunUv, god);

        drawCrosshair(width, height);

        const float slot = 46.0f;
        const float gap = 4.0f;
        const float bar = 10.0f * slot + 9.0f * gap;
        unsigned icons[10] = {
            blockTex.grassTop, 0, blockTex.torch, 0, blockTex.dirt,
            blockTex.logTop, blockTex.leaves, blockTex.tallGrass, blockTex.poppy, blockTex.triteleia
        };
        ImGui::SetNextWindowPos(ImVec2(((float)width - bar) * 0.5f - 10.0f, (float)height - slot - 52.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.5f);
        ImGui::Begin("HUD", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("%s", selectedPlaceName());
        int sel = selectedPlaceSlot();
        for (int i = 0; i < 10; ++i) {
            if (i) ImGui::SameLine(0.0f, gap);
            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (icons[i]) ImGui::Image((ImTextureID)(intptr_t)icons[i], ImVec2(slot, slot), ImVec2(0, 1), ImVec2(1, 0));
            else ImGui::ColorButton("##c", i == 1 ? ImVec4(0.12f, 0.38f, 0.75f, 1.0f) : ImVec4(0.95f, 0.32f, 0.05f, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(slot, slot));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRect(p, ImVec2(p.x + slot, p.y + slot), i == sel ? IM_COL32(255, 214, 80, 255) : IM_COL32(12, 12, 12, 230), 2.0f, 0, i == sel ? 3.0f : 1.5f);
            char key[2] = { (char)(i == 9 ? '0' : '1' + i), 0 };
            dl->AddText(ImVec2(p.x + 3.0f, p.y + 1.0f), IM_COL32(255, 255, 255, 230), key);
            ImGui::PopID();
        }
        ImGui::End();

        if (inventoryOpen()) {
            struct Entry { Block block; unsigned tex; ImVec4 color; };
            Entry items[] = {
                { Block::Grass, blockTex.grassTop, ImVec4(1, 1, 1, 1) },
                { Block::Dirt, blockTex.dirt, ImVec4(1, 1, 1, 1) },
                { Block::Stone, blockTex.stone, ImVec4(1, 1, 1, 1) },
                { Block::Log, blockTex.logTop, ImVec4(1, 1, 1, 1) },
                { Block::Leaves, blockTex.leaves, ImVec4(1, 1, 1, 1) },
                { Block::Water, 0, ImVec4(0.12f, 0.38f, 0.75f, 1) },
                { Block::Lava, 0, ImVec4(0.95f, 0.32f, 0.05f, 1) },
                { Block::Obsidian, 0, ImVec4(0.12f, 0.08f, 0.16f, 1) },
                { Block::Torch, blockTex.torch, ImVec4(1, 1, 1, 1) },
                { Block::TallGrass, blockTex.tallGrass, ImVec4(1, 1, 1, 1) },
                { Block::Poppy, blockTex.poppy, ImVec4(1, 1, 1, 1) },
                { Block::Triteleia, blockTex.triteleia, ImVec4(1, 1, 1, 1) },
                { Block::TriteleiaRed, blockTex.triteleiaRed, ImVec4(1, 1, 1, 1) }
            };
            const int count = (int)(sizeof(items) / sizeof(items[0]));
            ImGui::SetNextWindowPos(ImVec2(width * 0.5f, height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowBgAlpha(0.92f);
            ImGui::Begin("Inventario", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("Elige un bloque. E o Esc cierran.");
            for (int i = 0; i < count; ++i) {
                if (i % 7 != 0) ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::PushID(i + 100);
                bool pressed = false;
                if (items[i].tex) {
                    pressed = ImGui::ImageButton("##icon", (ImTextureID)(intptr_t)items[i].tex, ImVec2(48, 48), ImVec2(0, 1), ImVec2(1, 0));
                }
                else {
                    pressed = ImGui::ColorButton("##col", items[i].color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(48, 48));
                }
                if (pressed) {
                    bool found = false;
                    for (int s = 0; s < 10; ++s) {
                        if (placeSlotBlock(s) == items[i].block) {
                            selectPlaceSlot(s);
                            found = true;
                            break;
                        }
                    }
                    if (!found) setHotbarBlock(selectedPlaceSlot(), items[i].block);
                }
                const char* name = "Bloque";
                switch (items[i].block) {
                case Block::Grass: name = "Pasto"; break;
                case Block::Dirt: name = "Tierra"; break;
                case Block::Stone: name = "Piedra"; break;
                case Block::Log: name = "Tronco"; break;
                case Block::Leaves: name = "Hojas"; break;
                case Block::Water: name = "Agua"; break;
                case Block::Lava: name = "Lava"; break;
                case Block::Obsidian: name = "Obsidiana"; break;
                case Block::Torch: name = "Antorcha"; break;
                case Block::TallGrass: name = "Pasto"; break;
                case Block::Poppy: name = "Amapola"; break;
                case Block::Triteleia: name = "Flor"; break;
                case Block::TriteleiaRed: name = "Flor rosa"; break;
                default: break;
                }
                ImGui::TextUnformatted(name);
                ImGui::PopID();
                ImGui::EndGroup();
            }
            ImGui::End();
        }

        pauseMenuRender(width, height, menuState);

        ImGui::Render();
        glViewport(0, 0, width, height);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        if (menuState.fpsCap > 0) {
            double frameEnd = glfwGetTime();
            double frameTime = frameEnd - frameStart;
            double target = 1.0 / menuState.fpsCap;
            if (frameTime < target) {
                std::this_thread::sleep_for(std::chrono::duration<double>(target - frameTime));
            }
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
