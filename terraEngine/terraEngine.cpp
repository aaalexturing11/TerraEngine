#include "GlIncludes.h"
#include "GlExt.h"
#include <thread>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "BlockSelection.h"
#include "BlockShader.h"
#include "Camera.h"
#include "Character.h"
#include "Clouds.h"
#include "FluidSolver.h"
#include "Inventory.h"
#include "ItemIcon.h"
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
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(800, 800, "TerraEngine", NULL, NULL);
    if (!window) {
        glfwDefaultWindowHints();
        window = glfwCreateWindow(800, 800, "TerraEngine", NULL, NULL);
    }
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    if (!initGlExtensions() || !initBlockShaders() || !initClouds() || !initPostFX()) {
        char box[1600];
        std::snprintf(box, sizeof(box), "No se pudo iniciar OpenGL 3.3 (shaders).\n\n%s", lastGlShaderError());
        MessageBoxA(nullptr, box, "TerraEngine", MB_OK);
        glfwTerminate();
        return -1;
    }

    glEnable(GL_NORMALIZE);

    TextureId sunTex = loadTexture("textures/sun.png");
    TextureId moonTex[8] = {
        loadTexture("textures/moon/full_moon.png"),
        loadTexture("textures/moon/waning_gibbous.png"),
        loadTexture("textures/moon/third_quarter.png"),
        loadTexture("textures/moon/waning_crescent.png"),
        loadTexture("textures/moon/new_moon.png"),
        loadTexture("textures/moon/waxing_crescent.png"),
        loadTexture("textures/moon/first_quarter.png"),
        loadTexture("textures/moon/waxing_gibbous.png")
    };
    auto clampCelestial = [](TextureId tex) {
        if (!tex) return;
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    clampCelestial(sunTex);
    for (int i = 0; i < 8; ++i) clampCelestial(moonTex[i]);
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
    itemIconsBuild();
    initCharacter();

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

    gameMenuInit();
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        ImGui_ImplGlfw_CursorPosCallback(w, x, y);
        cameraOnMouseMove(w, x, y);
    });
    glfwSetScrollCallback(window, [](GLFWwindow* w, double xoff, double yoff) {
        ImGui_ImplGlfw_ScrollCallback(w, xoff, yoff);
        if (ImGui::GetIO().WantCaptureMouse && !inventoryOpen()) return;
        if (paused || !gameMenuInWorld()) return;
        int slot = selectedPlaceSlot();
        if (yoff > 0.0) slot = (slot + kHotbarSlots - 1) % kHotbarSlots;
        else if (yoff < 0.0) slot = (slot + 1) % kHotbarSlots;
        selectPlaceSlot(slot);
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int mods) {
        ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods);
        if (ImGui::GetIO().WantCaptureMouse) return;
        handleMouseClick(w, button, action, mods);
    });
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    const int renderRadiusXZ = 140;
    const int shadowRadiusXZ = 72;
    const int renderMaxY = 140;
    double lastFrameTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        double frameStart = glfwGetTime();
        float time = (float)(gameMenuInWorld() ? (frameStart - worldTimeOrigin) : 0.0);
        DayNightLighting light = computeDayNightLighting(time);

        glfwPollEvents();
        if (gameMenuInWorld()) pollPlaceHotkeys(window);
        pauseMenuUpdateInput(window, menuState);
        if (gameScreen == GameScreen::Loading) gameMenuTickLoading();

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (height == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        const bool inWorld = gameMenuInWorld();
        static bool eWasDown = false;
        bool eDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
        if (inWorld && eDown && !eWasDown && !paused) {
            setInventoryOpen(!inventoryOpen());
            cameraResetMouse();
        }
        eWasDown = eDown;
        static bool f5WasDown = false;
        bool f5Down = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
        if (inWorld && !paused && f5Down && !f5WasDown)
            cameraCycleView();
        f5WasDown = f5Down;
        static bool fWasDown = false;
        bool fDown = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        if (inWorld && !paused && fDown && !fWasDown)
            inventorySwapOffhand();
        fWasDown = fDown;
        static bool f1WasDown = false;
        bool f1Down = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
        if (inWorld && !paused && f1Down && !f1WasDown)
            inventoryCycleHud();
        f1WasDown = f1Down;
        bool uiOpen = !inWorld || paused || inventoryOpen();
        cameraSetLookEnabled(inWorld && !uiOpen);
        glfwSetInputMode(window, GLFW_CURSOR, uiOpen ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);

        int generated = 0;
        if (inWorld) {
            static double nextWorldSave = 0.0;
            if (frameStart >= nextWorldSave) {
                gameMenuSaveProgress();
                nextWorldSave = frameStart + 8.0;
            }
            worldServiceSaves(frameStart);
            float frameDt = (float)(frameStart - lastFrameTime);
            inventoryTick(frameDt);
            cameraUpdateMovement(window, !uiOpen, frameDt);
            characterSync(frameDt, time, uiOpen);
            generated = worldStreamAround((float)camX, (float)camZ);
            setMeshBudget(1);
            fluidSetHeavy(generated == 0);
            fluidSetView((float)camX, (float)camZ);
            fluidSetClock(time);
            if (!uiOpen) {
                updateSelectedBlock(window);
                tickWater((float)(frameStart - lastFrameTime), time);
                fluidTick((float)(frameStart - lastFrameTime));
            }
        }
        lastFrameTime = frameStart;

        glm::vec3 camPos = cameraPosition();
        bool submerged = inWorld && cameraEyesInWater();
        static int shadowTick = 0;
        if (inWorld) {
            beginFrameMeshes();
            if (light.sunIntensity > 0.08f && generated == 0 && ((shadowTick++ & 1) == 0)) {
                glm::vec3 vel = cameraVelocity();
                float spd = std::sqrt(vel.x * vel.x + vel.z * vel.z);
                beginShadowPass(light, camPos, spd > 6.0f ? 512 : 1024);
                renderShadowCasters(shadowRadiusXZ, renderMaxY);
                endShadowPass(width, height);
            }
            beginScenePass(width, height);
            int sceneW = width, sceneH = height;
            scenePassSize(sceneW, sceneH);
            setup3D(sceneW, sceneH);
        }
        else {
            glx.BindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, width, height);
        }
        glm::vec3 clearCol = submerged ? glm::vec3(0.03f, 0.14f, 0.22f) : light.skyColor;
        glClearColor(clearCol.r, clearCol.g, clearCol.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (inWorld) {
            glLoadIdentity();
            glm::vec3 renderEye, renderFocus;
            cameraRenderFrame(renderEye, renderFocus);
            glm::vec3 viewUp = cameraViewUp();
            gluLookAt(renderEye.x, renderEye.y, renderEye.z, renderFocus.x, renderFocus.y, renderFocus.z, viewUp.x, viewUp.y, viewUp.z);

            glm::vec3 viewDir = cameraLookDirection();
            glDepthMask(GL_FALSE);
            glDisable(GL_LIGHTING);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            bindFixedPipeline();
            if (light.sunDir.y > -0.06f)
                drawBillboardTextured(camPos + light.sunDir * 110.0f, viewDir, 10.0f, sunTex, 1.25f);
            int phase = light.moonPhase;
            if (phase < 0) phase = 0;
            if (phase > 7) phase = 7;
            if (light.moonDir.y > -0.06f && moonTex[phase])
                drawBillboardTextured(camPos + light.moonDir * 110.0f, viewDir, 6.5f, moonTex[phase], phase == 4 ? 0.35f : 1.15f);
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }

        if (inWorld) {
        glm::vec3 heldPos(0.0f), heldCol(0.0f);
        float heldRange = 0.0f;
        characterHeldLight(heldPos, heldCol, heldRange);
        setDynamicPointLight(heldPos, heldCol, heldRange);
        bindOpaqueShader(light, time);
        renderOpaqueBlocks(renderRadiusXZ, renderMaxY);
        renderClouds(light, time);

        blitSceneForWater();
        bindWaterInputs();
        int sceneW = 1, sceneH = 1;
        scenePassSize(sceneW, sceneH);
        float sceneAspect = sceneH > 0 ? (float)sceneW / (float)sceneH : 1.0f;
        glm::vec3 eye, focus;
        cameraRenderFrame(eye, focus);
        glm::mat4 proj = glm::perspective(glm::radians(cameraFov()), sceneAspect, 0.12f, 360.0f);
        glm::mat4 view = glm::lookAt(eye, focus, cameraViewUp());
        glm::mat4 viewProj = proj * view;
        bindWaterShader(viewProj, cameraPosition(), light.skyColor, light.sunDir, light.sunIntensity, time);
        renderWaterBlocks(renderRadiusXZ, renderMaxY);
        bindFixedPipeline();
        renderPlayerCharacter(light, time);

        if (hasSelected) {
            drawSelectionOutline(selectedBlock.x, selectedBlock.y, selectedBlock.z);
        }

        glm::vec3 look = cameraLookDirection();
        glm::vec3 sun = glm::normalize(light.sunDir);
        glm::vec3 right = glm::cross(look, cameraViewUp());
        float rightLen = glm::length(right);
        if (rightLen < 0.001f) right = glm::vec3(1.0f, 0.0f, 0.0f);
        else right /= rightLen;
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
        if (submerged) god = light.sunIntensity * 0.9f;
        presentScene(width, height, light, sunUv, god, submerged, time);

        if (inventoryHud() == HudMode::Playing)
            drawCrosshair(width, height);
        if (inventoryHud() == HudMode::Playing)
            inventoryDrawHotbar(width, height, time);
        if (inventoryOpen())
            inventoryDrawScreen(width, height, time);
        }

        pauseMenuRender(width, height, menuState);
        titleBarRender(window, width, height, menuState);

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
        if (glfwWindowShouldClose(window))
            gameMenuSaveProgress();
    }

    clearWorldMeshes();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
