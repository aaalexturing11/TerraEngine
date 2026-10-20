#pragma once

#include <GLFW/glfw3.h>

extern bool paused;

struct PauseMenuState {
    bool isFullscreen = false;
    int windowedPosX = 100, windowedPosY = 100;
    int windowedWidth = 800, windowedHeight = 800;
    int fpsCap = 0;
    bool vsyncEnabled = true;
};

void pauseMenuUpdateInput(GLFWwindow* window, PauseMenuState& state);
void pauseMenuRender(int width, int height, const PauseMenuState& state);
