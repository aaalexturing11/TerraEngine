#include "PauseMenu.h"
#include "BlockSelection.h"
#include "Camera.h"

#include "imgui.h"

bool paused = false;

static bool escPressedLastFrame = false;
static bool upPressedLast = false, downPressedLast = false, zPressedLast = false;
static int menuIndex = 0;
static const int MENU_ITEMS = 6;

void pauseMenuUpdateInput(GLFWwindow* window, PauseMenuState& state) {
    bool escDown = (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
    if (escDown && !escPressedLastFrame) {
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
    escPressedLastFrame = escDown;

    bool upDown = (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS);
    bool downDown = (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS);
    bool zDown = (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS);

    if (paused) {
        if (upDown && !upPressedLast) menuIndex = (menuIndex - 1 + MENU_ITEMS) % MENU_ITEMS;
        if (downDown && !downPressedLast) menuIndex = (menuIndex + 1) % MENU_ITEMS;

        if (zDown && !zPressedLast) {
            switch (menuIndex) {
            case 0:
                paused = false;
                cameraResetMouse();
                break;
            case 1:
                glfwSetWindowShouldClose(window, GLFW_TRUE);
                break;
            case 2:
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
            case 3:
                if (state.fpsCap > 10) state.fpsCap -= 10;
                else if (state.fpsCap == 10) state.fpsCap = 0;
                break;
            case 4:
                if (state.fpsCap == 0) state.fpsCap = 10;
                else state.fpsCap += 10;
                break;
            case 5:
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

void pauseMenuRender(int width, int height, const PauseMenuState& state) {
    if (!paused) return;

    ImGui::SetNextWindowSize(ImVec2(260.0f, 260.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((width - 260) / 2.0f, (height - 260) / 2.0f), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("PauseMenu", nullptr, flags);
    ImGui::Text("Game Paused:");
    ImGui::Spacing();
    if (menuIndex == 0) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
    ImGui::Text("Resume");
    if (menuIndex == 0) ImGui::PopStyleColor();
    if (menuIndex == 1) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
    ImGui::Text("Exit");
    if (menuIndex == 1) ImGui::PopStyleColor();
    if (menuIndex == 2) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
    if (state.isFullscreen) ImGui::Text("Windowed"); else ImGui::Text("Fullscreen");
    if (menuIndex == 2) ImGui::PopStyleColor();
    if (menuIndex == 3) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
    ImGui::Text("Decrease FPS Cap");
    if (menuIndex == 3) ImGui::PopStyleColor();
    if (state.fpsCap == 0) ImGui::Text("  Current FPS Cap: Unlimited");
    else ImGui::Text("  Current FPS Cap: %d", state.fpsCap);
    if (menuIndex == 4) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
    ImGui::Text("Increase FPS Cap");
    if (menuIndex == 4) ImGui::PopStyleColor();
    if (menuIndex == 5) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
    if (state.vsyncEnabled) ImGui::Text("Toggle VSync (On)"); else ImGui::Text("Toggle VSync (Off)");
    if (menuIndex == 5) ImGui::PopStyleColor();
    ImGui::End();
}
