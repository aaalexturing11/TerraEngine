#include "Camera.h"
#include "MathUtil.h"
#include "World.h"

#include <cmath>

double camX = 16.0, camY = 16.0, camZ = 32.0;
double lookX = 0.0, lookY = 0.0, lookZ = -1.0;
double yaw = -90.0, pitch = 0.0;

static bool firstMouse = true;
static double lastX = 400.0, lastY = 400.0;
static bool lookEnabled = true;
static float stepPhase = 0.0f;
static float bobBlend = 0.0f;
static float landDip = 0.0f;
static float airTime = 0.0f;
static float viewFov = 60.0f;
static glm::vec2 bobDir(0.0f, -1.0f);
static glm::vec3 bobSmoothed(0.0f);
static glm::vec3 bobOffset(0.0f);

void cameraResetMouse() {
    firstMouse = true;
}

void cameraSetLookEnabled(bool enabled) {
    lookEnabled = enabled;
    if (!enabled) firstMouse = true;
}

void cameraOnMouseMove(GLFWwindow*, double xpos, double ypos) {
    if (!lookEnabled) return;
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

static bool overlapsWater(float x, float feet, float z) {
    const float heights[3] = { 0.2f, 0.9f, 1.5f };
    int ix = (int)std::floor(x);
    int iz = (int)std::floor(z);
    for (float h : heights) {
        if (blockAt(ix, (int)std::floor(feet + h), iz) == Block::Water) return true;
    }
    return false;
}

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
                if (b != Block::Air && b != Block::Torch && b != Block::Water && !isPlant(b)) return true;
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

    float feetNow = (float)camY - kEye;
    bool wet = !flying && overlapsWater((float)camX, feetNow, (float)camZ);
    bool headWet = false;
    if (wet) {
        int hx = (int)std::floor((float)camX);
        int hy = (int)std::floor(feetNow + 1.55f);
        int hz = (int)std::floor((float)camZ);
        headWet = blockAt(hx, hy, hz) == Block::Water;
    }

    if (space && !spaceWasDown) {
        double now = glfwGetTime();
        if (now - lastSpaceTap < 0.28) {
            flying = !flying;
            bodyVy = 0.0f;
            wet = false;
        }
        else if (!flying && wet && !headWet) {
            bodyVy = 6.4f;
            grounded = false;
        }
        else if (!flying && grounded && !wet) {
            bodyVy = 8.4f;
            grounded = false;
        }
        lastSpaceTap = now;
    }
    spaceWasDown = space;

    float speed = wet ? 2.15f : 4.3f;
    if (ctrl && w) speed = wet ? 3.3f : 7.0f;
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
        if (wet) {
            bodyVy -= 5.0f * dt;
            if (space) bodyVy += 14.0f * dt;
            float riseCap = headWet ? 3.1f : 6.6f;
            if (bodyVy > riseCap) bodyVy = riseCap;
            if (bodyVy < -2.0f) bodyVy = -2.0f;
        }
        else {
            bodyVy -= 28.0f * dt;
            if (bodyVy < -52.0f) bodyVy = -52.0f;
        }
        float dy = bodyVy * dt;
        int steps = (int)(std::fabs(dy) / 0.2f) + 1;
        float step = dy / (float)steps;
        bool hit = false;
        for (int i = 0; i < steps; ++i) {
            float next = feet + step;
            if (!flying && !wet && overlapsWater((float)camX, next, (float)camZ)) {
                if (bodyVy < -2.0f) bodyVy = -2.0f;
                feet = next;
                grounded = false;
                break;
            }
            if (hitsSolid((float)camX, next, (float)camZ)) {
                hit = true;
                break;
            }
            feet = next;
        }
        if (hit && bodyVy < 0.0f) {
            if (!wet && airTime > 0.12f && bodyVy < -7.0f) {
                landDip = (-bodyVy - 7.0f) / 14.0f;
                if (landDip > 1.0f) landDip = 1.0f;
            }
            bodyVy = 0.0f;
            grounded = true;
            airTime = 0.0f;
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
    if (!grounded && !flying) airTime += dt;
    else airTime = 0.0f;

    float moved = std::sqrt(wish.x * wish.x + wish.z * wish.z) * dt;
    bool stepping = grounded && !flying && moved > 0.002f;
    const float tau = 6.2831853f;
    if (stepping) {
        stepPhase += dt * ((speed > 6.0f) ? 12.0f : 8.2f);
        if (stepPhase > tau) stepPhase -= tau;
        glm::vec2 dir(wish.x, wish.z);
        float dl = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (dl > 0.001f) {
            dir /= dl;
            float follow = 1.0f - std::exp(-dt * 6.0f);
            bobDir += (dir - bobDir) * follow;
            float bl = std::sqrt(bobDir.x * bobDir.x + bobDir.y * bobDir.y);
            if (bl > 0.001f) bobDir /= bl;
        }
    }
    else {
        float p = stepPhase;
        if (p > tau * 0.5f) p -= tau;
        p *= std::exp(-dt * 10.0f);
        if (p < 0.0f) p += tau;
        stepPhase = p;
    }
    float targetBob = stepping ? (wet ? 0.22f : 1.0f) : 0.0f;
    float blendRate = stepping ? 6.0f : 14.0f;
    bobBlend += (targetBob - bobBlend) * (1.0f - std::exp(-dt * blendRate));
    if (bobBlend < 0.01f) bobBlend = 0.0f;
    landDip *= std::exp(-dt * 8.0f);
    float hopWave = std::sin(stepPhase);
    float hop = hopWave * hopWave * 0.04f * bobBlend;
    float sway = hopWave * 0.016f * bobBlend;
    if (!grounded && !flying && !wet) {
        float drop = -bodyVy;
        if (drop > 10.0f) drop = 10.0f;
        if (drop < 0.0f) drop = 0.0f;
        hop += drop * 0.003f;
    }
    glm::vec3 side(-bobDir.y, 0.0f, bobDir.x);
    glm::vec3 targetBobPos = side * sway + glm::vec3(0.0f, hop - landDip * 0.05f, 0.0f);
    if (!stepping && grounded && landDip < 0.02f) targetBobPos = glm::vec3(0.0f);
    bobSmoothed += (targetBobPos - bobSmoothed) * (1.0f - std::exp(-dt * 12.0f));
    if (glm::length(bobSmoothed) < 0.0012f) bobSmoothed = glm::vec3(0.0f);
    bobOffset = bobSmoothed;

    float targetFov = 60.0f;
    if (flying) targetFov = 66.0f;
    else if (!wet && speed > 6.0f && grounded) targetFov = 68.0f;
    if (!grounded && !flying && !wet && airTime > 0.12f) targetFov += 2.5f;
    viewFov += (targetFov - viewFov) * (1.0f - std::exp(-dt * 6.0f));
}

glm::vec3 cameraBobOffset() { return bobOffset; }
float cameraFov() { return viewFov; }

glm::vec3 cameraPosition() {
    return glm::vec3((float)camX, (float)camY, (float)camZ);
}

glm::vec3 cameraLookDirection() {
    return glm::vec3((float)lookX, (float)lookY, (float)lookZ);
}
