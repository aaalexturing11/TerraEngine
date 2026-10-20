#pragma once

#include <GLFW/glfw3.h>

extern bool paused;

enum class GameScreen {
    Title,
    Worlds,
    Create,
    Loading,
    Playing
};

extern GameScreen gameScreen;
extern float loadProgress;
extern double worldTimeOrigin;

struct PauseMenuState {
    bool isFullscreen = false;
    int windowedPosX = 100, windowedPosY = 100;
    int windowedWidth = 800, windowedHeight = 800;
    int fpsCap = 0;
    bool vsyncEnabled = true;
};

void gameMenuInit();
void pauseMenuUpdateInput(GLFWwindow* window, PauseMenuState& state);
void pauseMenuRender(int width, int height, const PauseMenuState& state);
void titleBarRender(GLFWwindow* window, int width, int height, const PauseMenuState& state);
void gameMenuTickLoading();
bool gameMenuInWorld();
void gameMenuLeaveWorld();
