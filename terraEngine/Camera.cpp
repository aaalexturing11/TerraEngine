#include "Camera.h"
#include "FluidSolver.h"
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
static glm::vec3 lookSway(0.0f);
static glm::vec3 lookSwaySmoothed(0.0f);
static float idleTime = 0.0f;
static PlayerMotion gMotion;

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

static float bodyVx = 0.0f;
static float bodyVy = 0.0f;
static float bodyVz = 0.0f;
static bool flying = false;
static bool grounded = false;
static bool spaceWasDown = false;
static double lastSpaceTap = -1.0;

static const float kEye = 1.62f;
static const float kHeight = 1.8f;
static const float kRadius = 0.3f;

static bool overlapsWater(float x, float feet, float z) {
    const float heights[3] = { 0.2f, 0.55f, 0.95f };
    int ix = (int)std::floor(x);
    int iz = (int)std::floor(z);
    int hits = 0;
    for (float h : heights) {
        int iy = (int)std::floor(feet + h);
        if (waterSwimmable(ix, iy, iz)) ++hits;
    }
    return hits >= 1 && waterPoolDepth(ix, iz) >= 0.70f;
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
    if (!grounded || (dx * dx + dz * dz) < 1.0e-8f) return false;
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
    camY = (double)fy + (double)kEye;
    return true;
}

static bool groundedAt(float x, float feet, float z) {
    return hitsSolid(x, feet - 0.08f, z) && !hitsSolid(x, feet + 0.002f, z);
}

static float snapToSupport(float x, float feet, float z) {
    float support = std::floor(feet - 0.08f) + 1.0f;
    if (!hitsSolid(x, support, z) && hitsSolid(x, support - 0.08f, z))
        return support;
    return feet;
}

void cameraSetSpawn(int x, int y, int z) {
    camX = (double)x + 0.5;
    camZ = (double)z + 0.5;
    camY = (double)y + 1.0 + (double)kEye;
    bodyVx = bodyVy = bodyVz = 0.0f;
    grounded = true;
    flying = false;
}

void cameraSpawnOnGround() {
    int x = 0, y = SEA_LEVEL, z = 0;
    worldFindSpawn(x, y, z);
    cameraSetSpawn(x, y, z);
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
            bodyVy = 8.37f;
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

    float feet = (float)((double)camY - (double)kEye);
    float px = (float)camX;
    float pz = (float)camZ;

    if (flying) {
        bodyVx = wish.x;
        bodyVz = wish.z;
        float vertical = 0.0f;
        if (space) vertical += speed;
        if (shift) vertical -= speed;
        bodyVy = vertical;
        moveHorizontal(px, pz, feet, bodyVx * dt, bodyVz * dt);
        camX = px;
        camZ = pz;
        float next = feet + bodyVy * dt;
        if (!hitsSolid((float)camX, next, (float)camZ)) feet = next;
        bodyVy = 0.0f;
        grounded = false;
        airTime = 0.0f;
    }
    else if (wet) {
        float fx, fz, down;
        waterFlow((int)std::floor(px), (int)std::floor(feet + 0.9f), (int)std::floor(pz), fx, fz, down);
        float sub = 0.0f;
        const float probe[3] = { 0.25f, 0.9f, 1.55f };
        for (float h : probe) {
            if (blockAt((int)std::floor(px), (int)std::floor(feet + h), (int)std::floor(pz)) == Block::Water)
                sub += 1.0f;
        }
        sub *= (1.0f / 3.0f);

        bodyVx += (wish.x + fx * 1.6f - bodyVx) * (1.0f - std::exp(-dt * 1.7f));
        bodyVz += (wish.z + fz * 1.6f - bodyVz) * (1.0f - std::exp(-dt * 1.7f));
        bodyVy += (-28.0f + 34.0f * sub) * dt;
        if (space) bodyVy += 16.0f * dt;
        if (shift) bodyVy -= 14.0f * dt;
        bodyVy -= down * 3.2f * dt;
        float drag = 1.0f - std::exp(-dt * (2.4f + 0.35f * std::fabs(bodyVy)));
        bodyVx *= (1.0f - drag * 0.55f);
        bodyVz *= (1.0f - drag * 0.55f);
        bodyVy *= (1.0f - drag * 0.35f);
        if (headWet && bodyVy > 2.4f) bodyVy = 2.4f;
        if (!headWet && bodyVy > 5.8f) bodyVy = 5.8f;
        if (bodyVy < -6.5f) bodyVy = -6.5f;

        moveHorizontal(px, pz, feet, bodyVx * dt, bodyVz * dt);
        camX = px;
        camZ = pz;
        fluidWake((float)camX, (float)camZ, bodyVx, bodyVz);
        float dy = bodyVy * dt;
        int steps = (int)(std::fabs(dy) / 0.2f) + 1;
        float step = dy / (float)steps;
        bool hit = false;
        for (int i = 0; i < steps; ++i) {
            float next = feet + step;
            if (hitsSolid((float)camX, next, (float)camZ)) {
                hit = true;
                if (step < 0.0f) feet = snapToSupport((float)camX, feet, (float)camZ);
                break;
            }
            feet = next;
        }
        if (hit && bodyVy <= 0.0f) {
            bodyVy = 0.0f;
            grounded = groundedAt((float)camX, feet, (float)camZ);
        }
        else {
            grounded = false;
        }
        airTime = 0.0f;
    }
    else {
        bodyVx = wish.x;
        bodyVz = wish.z;
        moveHorizontal(px, pz, feet, bodyVx * dt, bodyVz * dt);
        camX = px;
        camZ = pz;
        feet = (float)((double)camY - (double)kEye);

        if (bodyVy <= 0.0f && grounded && groundedAt((float)camX, feet, (float)camZ)) {
            feet = snapToSupport((float)camX, feet, (float)camZ);
            bodyVy = 0.0f;
            grounded = true;
            airTime = 0.0f;
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
                if (overlapsWater((float)camX, next, (float)camZ)) {
                    waterImpact((int)std::floor((float)camX), (int)std::floor(next + 0.4f), (int)std::floor((float)camZ), bodyVy, 82.0f);
                    feet = next;
                    grounded = false;
                    break;
                }
                if (hitsSolid((float)camX, next, (float)camZ)) {
                    hit = true;
                    if (step < 0.0f) {
                        float landed = snapToSupport((float)camX, next + 0.08f, (float)camZ);
                        if (!hitsSolid((float)camX, landed, (float)camZ)) feet = landed;
                    }
                    break;
                }
                feet = next;
            }
            if (hit && bodyVy <= 0.0f) {
                if (airTime > 0.12f && bodyVy < -7.0f) {
                    landDip = (-bodyVy - 7.0f) / 14.0f;
                    if (landDip > 1.0f) landDip = 1.0f;
                }
                bodyVy = 0.0f;
                grounded = true;
                airTime = 0.0f;
            }
            else if (hit) {
                bodyVy = 0.0f;
                grounded = false;
            }
            else if (bodyVy <= 0.0f && groundedAt((float)camX, feet, (float)camZ)) {
                feet = snapToSupport((float)camX, feet, (float)camZ);
                bodyVy = 0.0f;
                grounded = true;
                airTime = 0.0f;
            }
            else {
                grounded = false;
                airTime += dt;
            }
        }
    }
    camY = (double)feet + (double)kEye;

    idleTime += dt;
    float moved = std::sqrt(wish.x * wish.x + wish.z * wish.z) * dt;
    bool stepping = grounded && !flying && !wet && moved > 0.002f;
    const float tau = 6.2831853f;
    if (stepping) {
        stepPhase += dt * ((speed > 6.0f) ? 13.5f : 9.0f);
        if (stepPhase > tau) stepPhase -= tau;
        glm::vec2 dir(wish.x, wish.z);
        float dl = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (dl > 0.001f) {
            dir /= dl;
            float follow = 1.0f - std::exp(-dt * 8.0f);
            bobDir += (dir - bobDir) * follow;
            float bl = std::sqrt(bobDir.x * bobDir.x + bobDir.y * bobDir.y);
            if (bl > 0.001f) bobDir /= bl;
        }
    }
    float targetBob = stepping ? (speed > 6.0f ? 1.15f : 0.72f) : 0.0f;
    float blendRate = stepping ? 7.0f : 4.0f;
    bobBlend += (targetBob - bobBlend) * (1.0f - std::exp(-dt * blendRate));
    if (bobBlend < 0.001f) bobBlend = 0.0f;
    if (bobBlend > 1.0f) bobBlend = 1.0f;
    float still = 1.0f - bobBlend;
    float calm = wet ? 0.4f : (flying ? 0.55f : 1.0f);
    landDip *= std::exp(-dt * 8.0f);
    float hopWave = std::sin(stepPhase);
    float hop = hopWave * hopWave * 0.058f * bobBlend;
    float sway = hopWave * 0.032f * bobBlend;
    float surge = std::sin(stepPhase) * 0.02f * bobBlend;
    if (!grounded && !flying && !wet) {
        float drop = -bodyVy;
        if (drop > 10.0f) drop = 10.0f;
        if (drop < 0.0f) drop = 0.0f;
        hop += drop * 0.004f;
    }
    float breathe = std::sin(idleTime * 1.55f) * 0.016f * calm;
    float idleSide = std::sin(idleTime * 0.72f) * 0.012f * calm;
    glm::vec3 side(-bobDir.y, 0.0f, bobDir.x);
    glm::vec3 fwd(bobDir.x, 0.0f, bobDir.y);
    glm::vec3 targetBobPos = side * (sway + idleSide * still) + fwd * surge
        + glm::vec3(0.0f, hop - landDip * 0.07f + breathe * (0.35f + 0.65f * still), 0.0f);
    bobSmoothed += (targetBobPos - bobSmoothed) * (1.0f - std::exp(-dt * 14.0f));
    if (glm::dot(bobSmoothed, bobSmoothed) < 1.0e-8f) bobSmoothed = glm::vec3(0.0f);
    bobOffset = bobSmoothed;

    glm::vec3 look((float)lookX, (float)lookY, (float)lookZ);
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 lookRight = glm::cross(look, worldUp);
    float rl = glm::length(lookRight);
    if (rl > 0.001f) lookRight /= rl;
    else lookRight = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 camUp = glm::cross(lookRight, look);
    float lookYaw = std::sin(stepPhase) * 0.045f * bobBlend + std::sin(idleTime * 0.68f) * 0.016f * still * calm;
    float lookPitch = std::cos(stepPhase * 2.0f) * 0.022f * bobBlend + std::sin(idleTime * 1.12f) * 0.01f * still * calm;
    glm::vec3 targetSway = lookRight * lookYaw + camUp * lookPitch;
    lookSwaySmoothed += (targetSway - lookSwaySmoothed) * (1.0f - std::exp(-dt * 10.0f));
    lookSway = lookSwaySmoothed;

    float targetFov = 60.0f;
    if (flying) targetFov = 66.0f;
    else if (!wet && speed > 6.0f && grounded) targetFov = 68.0f;
    if (!grounded && !flying && !wet && airTime > 0.12f) targetFov += 2.5f;
    viewFov += (targetFov - viewFov) * (1.0f - std::exp(-dt * 6.0f));

    gMotion.grounded = grounded;
    gMotion.flying = flying;
    gMotion.swimming = wet;
    gMotion.sprinting = !wet && !flying && grounded && speed > 6.0f;
    gMotion.horizontalSpeed = std::sqrt(bodyVx * bodyVx + bodyVz * bodyVz);
    gMotion.verticalSpeed = bodyVy;
    gMotion.airTime = airTime;
    gMotion.stepPhase = stepPhase;
}

glm::vec3 cameraBobOffset() { return bobOffset; }

glm::vec3 cameraViewDirection() {
    glm::vec3 look((float)lookX, (float)lookY, (float)lookZ);
    glm::vec3 dir = look + lookSway;
    float len = glm::length(dir);
    if (len < 0.001f) return look;
    return dir / len;
}
float cameraFov() { return viewFov; }

bool cameraEyesInWater() {
    int ix = (int)std::floor(camX);
    int iy = (int)std::floor(camY);
    int iz = (int)std::floor(camZ);
    if (blockAt(ix, iy, iz) == Block::Water) return true;
    if (waterHasSurface(ix, iz) && (float)camY < waterSurfaceY(ix, iz) - 0.04f) return true;
    return false;
}

glm::vec3 cameraPosition() {
    return glm::vec3((float)camX, (float)camY, (float)camZ);
}

static CameraView gView = CameraView::First;

void cameraCycleView() {
    if (gView == CameraView::First) gView = CameraView::ThirdBack;
    else if (gView == CameraView::ThirdBack) gView = CameraView::ThirdFront;
    else gView = CameraView::First;
}

CameraView cameraView() { return gView; }
bool cameraFirstPerson() { return gView == CameraView::First; }

static bool cameraBlocked(const glm::vec3& p) {
    Block block = blockAt((int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z));
    return block != Block::Air && block != Block::Water && !isPlant(block);
}

void cameraRenderFrame(glm::vec3& eye, glm::vec3& focus) {
    glm::vec3 origin = cameraPosition();
    glm::vec3 dir = cameraViewDirection();
    if (gView == CameraView::First) {
        eye = origin + cameraBobOffset();
        focus = eye + dir;
        return;
    }
    focus = origin;
    float sign = gView == CameraView::ThirdBack ? -1.0f : 1.0f;
    const float reach = 4.0f;
    const float step = 0.12f;
    eye = origin;
    for (float t = 0.35f; t <= reach; t += step) {
        glm::vec3 next = origin + dir * (sign * t);
        if (cameraBlocked(next)) break;
        eye = next;
    }
}

glm::vec3 cameraLookDirection() {
    return glm::vec3((float)lookX, (float)lookY, (float)lookZ);
}

glm::vec3 cameraVelocity() {
    return glm::vec3(bodyVx, bodyVy, bodyVz);
}

PlayerMotion cameraMotion() {
    return gMotion;
}
