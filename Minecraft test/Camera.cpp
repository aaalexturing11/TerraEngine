#include "Camera.h"
#include "MathUtil.h"
#include "World.h"

#include <cmath>

double camX = 16.0, camY = 16.0, camZ = 32.0;
double lookX = 0.0, lookY = 0.0, lookZ = -1.0;
double yaw = -90.0, pitch = 0.0;

static bool firstMouse = true;
static double lastX = 400.0, lastY = 400.0;

void cameraResetMouse() {
    firstMouse = true;
}

void cameraOnMouseMove(GLFWwindow*, double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }
    double dx = xpos - lastX;
    double dy = lastY - ypos;
    lastX = xpos;
    lastY = ypos;
    yaw += dx * 0.1;
    pitch += dy * 0.1;
    pitch = manualClamp((float)pitch, -89.0f, 89.0f);
    float ry = glm::radians((float)yaw), rp = glm::radians((float)pitch);
    lookX = std::cos(ry) * std::cos(rp);
    lookY = std::sin(rp);
    lookZ = std::sin(ry) * std::cos(rp);
}

static float bodyVy = 0.0f;
static bool flying = false;
static bool grounded = false;
static bool spaceWasDown = false;
static double lastSpaceTap = -1.0;

static const float kEye = 1.62f;
static const float kHeight = 1.8f;
static const float kRadius = 0.3f;

static bool hitsSolid(float x, float feet, float z) {
    int x0 = (int)std::floor(x - kRadius);
    int x1 = (int)std::floor(x + kRadius - 0.0001f);
    int y0 = (int)std::floor(feet);
    int y1 = (int)std::floor(feet + kHeight - 0.0001f);
    int z0 = (int)std::floor(z - kRadius);
    int z1 = (int)std::floor(z + kRadius - 0.0001f);
    for (int ix = x0; ix <= x1; ++ix)
        for (int iy = y0; iy <= y1; ++iy)
            for (int iz = z0; iz <= z1; ++iz) {
                Block b = blockAt(ix, iy, iz);
                if (b != Block::Air && b != Block::Torch && !isPlant(b)) return true;
            }
    return false;
}

static bool moveHorizontal(float& x, float& z, float feet, float dx, float dz) {
    float ox = x, oz = z;
    x += dx;
    if (hitsSolid(x, feet, z)) x = ox;
    z += dz;
    if (hitsSolid(x, feet, z)) z = oz;
    if (x != ox || z != oz) return true;
    if (!grounded) return false;
    float lifted = feet + 0.6f;
    if (hitsSolid(ox, lifted, oz)) return false;
    float nx = ox + dx, nz = oz + dz;
    if (hitsSolid(nx, lifted, nz)) return false;
    float fy = lifted;
    for (int i = 0; i < 6; ++i) {
        if (hitsSolid(nx, fy - 0.1f, nz)) break;
        fy -= 0.1f;
    }
    x = nx;
    z = nz;
    camY = fy + kEye;
    return true;
}

void cameraSpawnOnGround() {
    for (int x = 8; x < GRID - 8; ++x) {
        for (int z = 8; z < GRID - 8; ++z) {
            int top = -1;
            for (int y = 48; y >= 0; --y) {
                if (solid(x, y, z)) { top = y; break; }
            }
            if (top < 0) continue;
            if (solid(x, top + 1, z) || solid(x, top + 2, z)) continue;
            camX = x + 0.5;
            camZ = z + 0.5;
            camY = top + 1.0 + kEye;
            bodyVy = 0.0f;
            grounded = true;
            flying = false;
            return;
        }
    }
}

void cameraUpdateMovement(GLFWwindow* window, bool allowMove, float dt) {
    if (!allowMove) return;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;

    glm::vec3 flat((float)lookX, 0.0f, (float)lookZ);
    float flen = std::sqrt(flat.x * flat.x + flat.z * flat.z);
    if (flen < 0.001f) flat = glm::vec3(0, 0, -1);
    else flat /= flen;
    glm::vec3 right = glm::normalize(glm::cross(flat, glm::vec3(0, 1, 0)));

    bool w = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    bool s = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    bool a = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
    bool d = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
    bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    bool space = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;

    if (space && !spaceWasDown) {
        double now = glfwGetTime();
        if (now - lastSpaceTap < 0.28) {
            flying = !flying;
            bodyVy = 0.0f;
        }
        else if (!flying && grounded) {
            bodyVy = 8.4f;
            grounded = false;
        }
        lastSpaceTap = now;
    }
    spaceWasDown = space;

    float speed = 4.3f;
    if (ctrl && w) speed = 7.0f;
    if (flying) speed = (ctrl && w) ? 12.0f : 8.0f;

    glm::vec3 wish(0.0f);
    if (w) wish += flat;
    if (s) wish -= flat;
    if (a) wish -= right;
    if (d) wish += right;
    if (glm::length(wish) > 0.001f) wish = glm::normalize(wish) * speed;

    float feet = (float)camY - kEye;
    float px = (float)camX;
    float pz = (float)camZ;
    moveHorizontal(px, pz, feet, wish.x * dt, wish.z * dt);
    camX = px;
    camZ = pz;
    feet = (float)camY - kEye;

    if (flying) {
        float vertical = 0.0f;
        if (space) vertical += speed;
        if (shift) vertical -= speed;
        float next = feet + vertical * dt;
        if (!hitsSolid((float)camX, next, (float)camZ)) feet = next;
        bodyVy = 0.0f;
        grounded = false;
    }
    else {
        bodyVy -= 28.0f * dt;
        if (bodyVy < -52.0f) bodyVy = -52.0f;
        float dy = bodyVy * dt;
        int steps = (int)(std::fabs(dy) / 0.2f) + 1;
        float step = dy / (float)steps;
        bool hit = false;
        for (int i = 0; i < steps; ++i) {
            float next = feet + step;
            if (hitsSolid((float)camX, next, (float)camZ)) {
                hit = true;
                break;
            }
            feet = next;
        }
        if (hit && bodyVy < 0.0f) {
            bodyVy = 0.0f;
            grounded = true;
        }
        else if (hit && bodyVy > 0.0f) {
            bodyVy = 0.0f;
            grounded = false;
        }
        else {
            grounded = false;
        }
    }
    camY = feet + kEye;
}

glm::vec3 cameraPosition() {
    return glm::vec3((float)camX, (float)camY, (float)camZ);
}

glm::vec3 cameraLookDirection() {
    return glm::vec3((float)lookX, (float)lookY, (float)lookZ);
}
