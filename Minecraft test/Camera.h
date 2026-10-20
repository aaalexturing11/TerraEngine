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
void cameraSetLookEnabled(bool enabled);

glm::vec3 cameraPosition();
glm::vec3 cameraLookDirection();
glm::vec3 cameraBobOffset();
float cameraFov();
