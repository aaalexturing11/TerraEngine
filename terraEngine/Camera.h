#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

extern double camX, camY, camZ;
extern double lookX, lookY, lookZ;
extern double yaw, pitch;

void cameraResetMouse();
void cameraOnMouseMove(GLFWwindow* window, double xpos, double ypos);
void cameraUpdateMovement(GLFWwindow* window, bool allowMove, float dt);
void cameraSpawnOnGround();
void cameraSetSpawn(int x, int y, int z);
void cameraSetLookEnabled(bool enabled);

enum class CameraView { First = 0, ThirdBack = 1, ThirdFront = 2 };
void cameraCycleView();
CameraView cameraView();
bool cameraFirstPerson();
void cameraRenderFrame(glm::vec3& eye, glm::vec3& focus);

glm::vec3 cameraPosition();
glm::vec3 cameraLookDirection();
glm::vec3 cameraViewDirection();
glm::vec3 cameraViewUp();
glm::vec3 cameraVelocity();
glm::vec3 cameraBobOffset();
float cameraFov();
bool cameraEyesInWater();

struct PlayerMotion {
    bool grounded = true;
    bool flying = false;
    bool swimming = false;
    bool sprinting = false;
    float horizontalSpeed = 0.0f;
    float verticalSpeed = 0.0f;
    float airTime = 0.0f;
    float stepPhase = 0.0f;
};

PlayerMotion cameraMotion();
