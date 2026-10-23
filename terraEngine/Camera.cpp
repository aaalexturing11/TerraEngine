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

static void clampLedgeLook();

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
    pitch += dy * 0.1;
    pitch = manualClamp((float)pitch, -89.0f, 89.0f);
    float pole = (std::fabs((float)pitch) - 74.0f) / 15.0f;
    if (pole < 0.0f) pole = 0.0f;
    if (pole > 1.0f) pole = 1.0f;
    float yawScale = 1.0f - pole * pole * (3.0f - 2.0f * pole);
    yaw += dx * 0.1 * (double)yawScale;
    clampLedgeLook();
}

static float bodyVx = 0.0f;
static float bodyVy = 0.0f;
static float bodyVz = 0.0f;
static bool flying = false;
static bool grounded = false;
static bool spaceWasDown = false;
static double lastSpaceTap = -1.0;

static const float kStandEye = 1.62f;
static const float kCrouchEye = 1.26f;
static const float kStandH = 1.8f;
static const float kCrouchH = 1.49f;
static const float kRadius = 0.3f;
static float gBodyH = kStandH;
static float gEyeH = kStandEye;
static float gCrouch = 0.0f;
static float gImpact = 0.0f;
static float gViewRoll = 0.0f;
static float gSub = 0.0f;
static bool gHeadWet = false;
static float gLedgeLock = 0.0f;

enum class Act { None, Slide, Roll, Hang, Climb, Leap };
static Act gAct = Act::None;
static float gActT = 0.0f;
static glm::vec3 gActDir(0.0f, 0.0f, -1.0f);
static float gActSpeed = 0.0f;
static glm::vec3 gHang(0.0f);
static glm::vec3 gGripR(0.0f);
static glm::vec3 gGripL(0.0f);
static float gLedgeYaw = 0.0f;
static glm::vec3 gPendR(0.0f);
static glm::vec3 gPendL(0.0f);
static float gPendYaw = 0.0f;
static bool gClimbClick = false;
static bool gGrabPress = false;
static bool gSprint = false;
static glm::vec3 gLipR(0.0f);
static glm::vec3 gLipL(0.0f);
static glm::vec3 gPlantR(0.0f);
static glm::vec3 gPlantL(0.0f);

static float wrapDegCam(float d) {
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

static void writeLook() {
    float ry = glm::radians((float)yaw), rp = glm::radians((float)pitch);
    lookX = std::cos(ry) * std::cos(rp);
    lookY = std::sin(rp);
    lookZ = std::sin(ry) * std::cos(rp);
}

static void clampLedgeLook() {
    if (gAct == Act::Hang || gAct == Act::Climb) {
        float rel = wrapDegCam((float)yaw - gLedgeYaw);
        rel = manualClamp(rel, -90.0f, 90.0f);
        yaw = (double)(gLedgeYaw + rel);
        pitch = (double)manualClamp((float)pitch, -72.0f, 78.0f);
    }
    writeLook();
}

struct HangSpot {
    glm::vec3 feet{ 0.0f };
    glm::vec3 gripR{ 0.0f };
    glm::vec3 gripL{ 0.0f };
    float yaw = 0.0f;
    int bx = 0, by = 0, bz = 0;
};

static void commitHang(const HangSpot& spot) {
    gHang = spot.feet;
    gGripR = spot.gripR;
    gGripL = spot.gripL;
    gLedgeYaw = spot.yaw;
}
static glm::vec3 gLeapFrom(0.0f);
static glm::vec3 gLeapTo(0.0f);
static int gLedgeX = 0, gLedgeY = 0, gLedgeZ = 0;
static bool gClimbIgnore = false;

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
    int y1 = (int)std::floor(feet + gBodyH - 0.0001f);
    int z0 = (int)std::floor(z - kRadius);
    int z1 = (int)std::floor(z + kRadius - 0.0001f);
    for (int ix = x0; ix <= x1; ++ix)
        for (int iy = y0; iy <= y1; ++iy)
            for (int iz = z0; iz <= z1; ++iz) {
                Block b = blockAt(ix, iy, iz);
                if (gClimbIgnore && ix == gLedgeX && iy == gLedgeY && iz == gLedgeZ) continue;
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
    camY = (double)fy + (double)gEyeH;
    return true;
}

static bool groundedAt(float x, float feet, float z) {
    return hitsSolid(x, feet - 0.08f, z) && !hitsSolid(x, feet + 0.002f, z);
}

static bool blockedCell(int x, int y, int z) {
    Block b = blockAt(x, y, z);
    return b != Block::Air && b != Block::Torch && b != Block::Water && !isPlant(b);
}

static bool fits(float x, float feet, float z, float height) {
    float saved = gBodyH;
    gBodyH = height;
    bool hit = hitsSolid(x, feet, z);
    gBodyH = saved;
    return !hit;
}

static bool supportAt(float x, float feet, float z) {
    return hitsSolid(x, feet - 0.06f, z);
}

static void moveGuarded(float& x, float& z, float feet, float dx, float dz) {
    float ox = x, oz = z;
    float nx = x + dx;
    if (!hitsSolid(nx, feet, z) && supportAt(nx, feet, z)) x = nx;
    float nz = z + dz;
    if (!hitsSolid(x, feet, nz) && supportAt(x, feet, nz)) z = nz;
    if (!supportAt(x, feet, z)) {
        if (supportAt(ox, feet, z)) x = ox;
        else if (supportAt(x, feet, oz)) z = oz;
        else { x = ox; z = oz; }
    }
}

static bool lipCell(float x, float y, float z, int& bx, int& by, int& bz, float& top) {
    int ix = (int)std::floor(x);
    int iy = (int)std::floor(y);
    int iz = (int)std::floor(z);
    if (iy <= 1 || iy >= HEIGHT - 2) return false;
    if (blockedCell(ix, iy, iz) || !blockedCell(ix, iy - 1, iz)) return false;
    bx = ix;
    by = iy - 1;
    bz = iz;
    top = (float)iy;
    return true;
}

static bool hangAnchor(int bx, int by, int bz, float top, float px, float pz, HangSpot& out) {
    float cx = (float)bx + 0.5f;
    float cz = (float)bz + 0.5f;
    float dx = px - cx;
    float dz = pz - cz;
    glm::vec3 outN = std::fabs(dx) >= std::fabs(dz)
        ? glm::vec3(dx >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f)
        : glm::vec3(0.0f, 0.0f, dz >= 0.0f ? 1.0f : -1.0f);
    glm::vec3 faceDir = -outN;
    glm::vec3 handRight(-faceDir.z, 0.0f, faceDir.x);
    bool alongZ = std::fabs(outN.x) > 0.5f;
    float along = alongZ ? pz : px;
    float block0 = alongZ ? (float)bz : (float)bx;
    along = manualClamp(along, block0 + 0.30f, block0 + 0.70f);
    glm::vec3 lip = alongZ
        ? glm::vec3(outN.x > 0.0f ? (float)bx + 1.0f - 0.04f : (float)bx + 0.04f, top, along)
        : glm::vec3(along, top, outN.z > 0.0f ? (float)bz + 1.0f - 0.04f : (float)bz + 0.04f);
    const float reach = 0.456f;
    const float hangDrop = 1.756f;
    out.feet = glm::vec3(lip.x - faceDir.x * reach, top - hangDrop, lip.z - faceDir.z * reach);
    auto onTop = [&](glm::vec3 p) {
        p.x = manualClamp(p.x, (float)bx + 0.08f, (float)bx + 0.92f);
        p.z = manualClamp(p.z, (float)bz + 0.08f, (float)bz + 0.92f);
        p.y = top;
        return p;
    };
    out.gripR = onTop(lip + handRight * 0.16f);
    out.gripL = onTop(lip - handRight * 0.16f);
    out.yaw = glm::degrees(std::atan2(faceDir.z, faceDir.x));
    out.bx = bx;
    out.by = by;
    out.bz = bz;
    if (hitsSolid(out.feet.x, out.feet.y, out.feet.z)) return false;
    return true;
}

static bool findLedge(float px, float feet, float pz, const glm::vec3& fwd, int skipX, int skipY, int skipZ, glm::vec3& anchor, int& ox, int& oy, int& oz) {
    glm::vec3 side(-fwd.z, 0.0f, fwd.x);
    const float hands[] = { 1.05f, 1.40f, 1.75f, 2.10f, 2.45f, 2.80f, 3.05f };
    const float reach[] = { 0.40f, 0.72f, 1.05f };
    const float lateral[] = { 0.0f, -0.42f, 0.42f };
    for (float hand : hands) {
        for (float r : reach) {
            for (float lat : lateral) {
                int bx, by, bz;
                float top = 0.0f;
                float sx = px + fwd.x * r + side.x * lat;
                float sz = pz + fwd.z * r + side.z * lat;
                if (!lipCell(sx, feet + hand, sz, bx, by, bz, top)) continue;
                if (bx == skipX && by == skipY && bz == skipZ) continue;
                float rise = top - feet;
                if (rise < 0.90f || rise > 3.05f) continue;
                float hdx = (float)bx + 0.5f - px;
                float hdz = (float)bz + 0.5f - pz;
                if (hdx * hdx + hdz * hdz > 2.56f) continue;
                HangSpot hang;
                if (!hangAnchor(bx, by, bz, top, px, pz, hang)) continue;
                glm::vec3 to(hdx, 0.0f, hdz);
                float tl = std::sqrt(hdx * hdx + hdz * hdz);
                if (tl > 0.001f && (to.x * fwd.x + to.z * fwd.z) / tl < 0.05f) continue;
                anchor = hang.feet;
                ox = bx; oy = by; oz = bz;
                commitHang(hang);
                return true;
            }
        }
    }
    return false;
}

static bool findLedgeAhead(float px, float feet, float pz, const glm::vec3& dir, int skipX, int skipY, int skipZ, HangSpot& spot) {
    glm::vec3 d = dir;
    d.y = 0.0f;
    float len = std::sqrt(d.x * d.x + d.z * d.z);
    if (len < 0.2f) return false;
    d.x /= len; d.z /= len;
    for (float dist = 0.7f; dist <= 2.7f; dist += 0.35f) {
        float x = px + d.x * dist;
        float z = pz + d.z * dist;
        for (float hy = feet + 1.15f; hy <= feet + 2.45f; hy += 0.28f) {
            int bx, by, bz;
            float top = 0.0f;
            if (!lipCell(x, hy, z, bx, by, bz, top)) continue;
            if (bx == skipX && by == skipY && bz == skipZ) continue;
            if (!hangAnchor(bx, by, bz, top, x, z, spot)) continue;
            if (!fits(spot.feet.x, spot.feet.y, spot.feet.z, kStandH)) continue;
            return true;
        }
    }
    return false;
}

static bool easeToward(float& x, float& feet, float& z, const glm::vec3& target, float dt, float speed) {
    float dx = target.x - x, dy = target.y - feet, dz = target.z - z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 0.012f) {
        if (!hitsSolid(target.x, target.y, target.z)) {
            x = target.x; feet = target.y; z = target.z;
        }
        return true;
    }
    float step = speed * dt;
    if (step > dist) step = dist;
    int n = 4;
    float sx = dx / dist * step / (float)n;
    float sy = dy / dist * step / (float)n;
    float sz = dz / dist * step / (float)n;
    for (int i = 0; i < n; ++i) {
        float nx = x + sx, ny = feet + sy, nz = z + sz;
        if (hitsSolid(nx, ny, nz)) return false;
        x = nx; feet = ny; z = nz;
    }
    return false;
}

static float climbEase(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static glm::vec3 climbFeetAt(float u, const glm::vec3& hang, const glm::vec3& stand) {
    glm::vec3 fwd = stand - hang;
    fwd.y = 0.0f;
    float fl = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
    if (fl > 0.001f) { fwd.x /= fl; fwd.z /= fl; }
    float top = stand.y;
    glm::vec3 pull = hang + fwd * 0.10f;
    pull.y = hang.y + 0.16f;
    glm::vec3 chest = glm::mix(hang, stand, 0.28f);
    chest.y = top - 1.22f;
    glm::vec3 hip = glm::mix(hang, stand, 0.70f);
    hip.y = top - 0.42f;
    const glm::vec3 keys[5] = { hang, pull, chest, hip, stand };
    const float times[5] = { 0.0f, 0.18f, 0.40f, 0.72f, 1.0f };
    if (u <= 0.0f) return hang;
    if (u >= 1.0f) return stand;
    for (int i = 0; i < 4; ++i) {
        if (u <= times[i + 1]) {
            float t = (u - times[i]) / (times[i + 1] - times[i]);
            return glm::mix(keys[i], keys[i + 1], climbEase(t));
        }
    }
    return stand;
}

static void beginClimbHands(float top) {
    gLipR = gGripR;
    gLipL = gGripL;
    float ry = glm::radians(gLedgeYaw);
    glm::vec3 face(std::cos(ry), 0.0f, std::sin(ry));
    glm::vec3 right(-face.z, 0.0f, face.x);
    glm::vec3 mid = (gLipR + gLipL) * 0.5f;
    mid.y = top;
    auto plant = [&](float side) {
        glm::vec3 p = mid + face * 0.28f + right * side;
        p.y = top + 0.03f;
        p.x = manualClamp(p.x, (float)gLedgeX + 0.14f, (float)gLedgeX + 0.86f);
        p.z = manualClamp(p.z, (float)gLedgeZ + 0.14f, (float)gLedgeZ + 0.86f);
        return p;
    };
    gPlantR = plant(0.16f);
    gPlantL = plant(-0.16f);
}

static void poseClimbHands(float u) {
    float right = climbEase((u - 0.36f) / 0.16f);
    float left = climbEase((u - 0.54f) / 0.16f);
    gGripR = glm::mix(gLipR, gPlantR, right);
    gGripL = glm::mix(gLipL, gPlantL, left);
}

static bool arcMove(float& x, float& feet, float& z, const glm::vec3& from, const glm::vec3& to, float u0, float u1, float arc) {
    auto at = [&](float u) {
        glm::vec3 p = glm::mix(from, to, u);
        p.y += std::sin(u * PI) * arc;
        return p;
    };
    glm::vec3 a = at(u0);
    glm::vec3 b = at(u1);
    float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    int n = 4;
    for (int i = 1; i <= n; ++i) {
        float nx = a.x + dx * ((float)i / (float)n);
        float ny = a.y + dy * ((float)i / (float)n);
        float nz = a.z + dz * ((float)i / (float)n);
        if (hitsSolid(nx, ny, nz)) return false;
        x = nx; feet = ny; z = nz;
    }
    return true;
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
    gEyeH = kStandEye;
    gBodyH = kStandH;
    gCrouch = 0.0f;
    gAct = Act::None;
    gImpact = 0.0f;
    gViewRoll = 0.0f;
    camY = (double)y + 1.0 + (double)gEyeH;
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

    float feetNow = (float)camY - gEyeH;
    bool wet = !flying && overlapsWater((float)camX, feetNow, (float)camZ);
    bool headWet = false;
    if (wet) {
        int hx = (int)std::floor((float)camX);
        int hy = (int)std::floor(feetNow + 1.55f);
        int hz = (int)std::floor((float)camZ);
        headWet = blockAt(hx, hy, hz) == Block::Water;
    }
    gHeadWet = headWet;
    bool onLedge = gAct == Act::Hang || gAct == Act::Climb || gAct == Act::Leap;
    bool spaceEdge = space && !spaceWasDown;
    static bool shiftWas = false;
    bool shiftEdge = shift && !shiftWas;

    if (spaceEdge && !onLedge) {
        double now = glfwGetTime();
        bool canGrab = !flying && !wet && !grounded && gAct == Act::None && gLedgeLock <= 0.0f
            && bodyVy < 12.0f && bodyVy > -36.0f;
        glm::vec3 anchor;
        int lx = 0, ly = 0, lz = 0;
        bool grabbed = canGrab && findLedge((float)camX, feetNow, (float)camZ, flat, -9999, -9999, -9999, anchor, lx, ly, lz);
        if (grabbed) {
            gAct = Act::Hang;
            gHang = anchor;
            gLedgeX = lx; gLedgeY = ly; gLedgeZ = lz;
            gGrabPress = true;
            bodyVx = bodyVy = bodyVz = 0.0f;
            grounded = false;
        }
        else if (now - lastSpaceTap < 0.28) {
            flying = !flying;
            bodyVy = 0.0f;
            wet = false;
            gAct = Act::None;
        }
        else if (!flying && wet && !headWet) {
            bodyVy = 6.4f;
            grounded = false;
        }
        else if (!flying && grounded && !wet && gAct != Act::Slide && gAct != Act::Roll) {
            bodyVy = 8.37f;
            grounded = false;
        }
        lastSpaceTap = now;
    }
    spaceWasDown = space;

    float horiz = std::sqrt(bodyVx * bodyVx + bodyVz * bodyVz);
    static bool wWas = false;
    static double lastWEdge = -1.0;
    bool wEdge = w && !wWas;
    bool sprintOk = !wet && !flying && !shift && gCrouch < 0.35f && gAct == Act::None;
    if (wEdge && sprintOk) {
        double now = glfwGetTime();
        if (now - lastWEdge <= 0.25 && now - lastWEdge > 0.04) gSprint = true;
        lastWEdge = now;
    }
    wWas = w;
    bool sprinting = w && !wet && !flying && gCrouch < 0.4f
        && (gSprint || (ctrl && w));
    bool sprintNow = sprinting && !shift;
    if (wet || flying) {
        if (gAct != Act::None) gLedgeLock = 0.25f;
        gAct = Act::None;
        gSprint = false;
    }
    else if (shiftEdge && sprinting && gAct == Act::None && grounded && horiz > 6.0f) {
        gAct = Act::Slide;
        gActT = 0.0f;
        gActDir = glm::vec3(bodyVx / horiz, 0.0f, bodyVz / horiz);
        gActSpeed = horiz * 1.16f;
        if (gActSpeed < 7.8f) gActSpeed = 7.8f;
        if (gActSpeed > 9.0f) gActSpeed = 9.0f;
    }
    if (!w || wet || flying || shift || gCrouch > 0.45f) gSprint = false;

    bool ceiling = !fits((float)camX, feetNow, (float)camZ, kStandH);
    float crouchTarget = 0.0f;
    if (gAct == Act::Slide || gAct == Act::Roll) crouchTarget = 1.0f;
    else if (shift && !flying && !wet && !onLedge) crouchTarget = 1.0f;
    if (crouchTarget < 0.5f && ceiling) crouchTarget = 1.0f;
    float crouchRate = gAct == Act::Slide ? 14.0f : 8.0f;
    gCrouch += (crouchTarget - gCrouch) * (1.0f - std::exp(-dt * crouchRate));
    const float kSlideH = 1.02f;
    const float kSlideEye = 0.70f;
    float targetH = kStandH + (kCrouchH - kStandH) * gCrouch;
    float targetEye = kStandEye + (kCrouchEye - kStandEye) * gCrouch;
    if (gAct == Act::Slide) { targetH = kSlideH; targetEye = kSlideEye; }
    if (targetH > gBodyH + 0.04f && !fits((float)camX, feetNow, (float)camZ, targetH)) {
        targetH = gBodyH;
        targetEye = gEyeH;
    }
    float grow = 1.0f - std::exp(-dt * 10.0f);
    gBodyH += (targetH - gBodyH) * grow;
    gEyeH += (targetEye - gEyeH) * grow;

    float speed = wet ? 2.15f : 4.3f;
    if (sprintNow && !wet) speed = 7.0f;
    else if (ctrl && w && wet) speed = 3.3f;
    if (gCrouch > 0.45f && !wet && !flying && gAct != Act::Slide && gAct != Act::Roll) speed = 1.35f;
    if (flying) speed = (ctrl && w) ? 12.0f : 8.0f;

    glm::vec3 wish(0.0f);
    if (w) wish += flat;
    if (s) wish -= flat;
    if (a) wish -= right;
    if (d) wish += right;
    if (glm::length(wish) > 0.001f) wish = glm::normalize(wish) * speed;

    float feet = feetNow;
    float px = (float)camX;
    float pz = (float)camZ;
    auto place = [&]() {
        camX = px;
        camZ = pz;
        camY = (double)feet + (double)gEyeH;
    };

    if (gAct == Act::Climb) {
        gClimbIgnore = true;
        const float dur = 1.15f;
        gActT += dt / dur;
        if (gActT > 1.0f) gActT = 1.0f;
        poseClimbHands(gActT);
        glm::vec3 step = climbFeetAt(gActT, gLeapFrom, gLeapTo);
        px = step.x; feet = step.y; pz = step.z;
        if (gActT >= 1.0f) {
            px = gLeapTo.x; feet = gLeapTo.y; pz = gLeapTo.z;
            if (!fits(px, feet, pz, kStandH) && fits(px, feet, pz, kCrouchH)) {
                gCrouch = 1.0f;
                gBodyH = kCrouchH;
                gEyeH = kCrouchEye;
            }
            grounded = true;
            bodyVy = 0.0f;
            airTime = 0.0f;
            gAct = Act::None;
            gLedgeLock = 0.35f;
        }
        else grounded = false;
        gClimbIgnore = false;
        place();
    }
    else if (gAct == Act::Leap) {
        const float dur = 0.40f;
        const float arc = 0.48f;
        float prev = gActT;
        gActT += dt / dur;
        if (gActT > 1.0f) gActT = 1.0f;
        if (!arcMove(px, feet, pz, gLeapFrom, gLeapTo, prev, gActT, arc)) {
            gAct = Act::None;
            gLedgeLock = 0.28f;
            bodyVy = 1.2f;
            grounded = false;
        }
        else if (gActT >= 1.0f) {
            px = gLeapTo.x; feet = gLeapTo.y; pz = gLeapTo.z;
            gHang = gLeapTo;
            gGripR = gPendR;
            gGripL = gPendL;
            gLedgeYaw = gPendYaw;
            gAct = Act::Hang;
            grounded = false;
            bodyVx = bodyVy = bodyVz = 0.0f;
        }
        else grounded = false;
        place();
    }
    else if (gAct == Act::Hang) {
        if (!blockedCell(gLedgeX, gLedgeY, gLedgeZ)) {
            gAct = Act::None;
            gLedgeLock = 0.2f;
            bodyVy = -1.0f;
            grounded = false;
        }
        else if (shiftEdge) {
            glm::vec3 away(px - ((float)gLedgeX + 0.5f), 0.0f, pz - ((float)gLedgeZ + 0.5f));
            float al = std::sqrt(away.x * away.x + away.z * away.z);
            if (al < 0.001f) away = glm::vec3(-flat.x, 0.0f, -flat.z);
            else { away.x /= al; away.z /= al; }
            for (int i = 0; i < 4; ++i) {
                float nx = px + away.x * 0.09f;
                float nz = pz + away.z * 0.09f;
                if (hitsSolid(nx, feet, nz)) break;
                px = nx; pz = nz;
            }
            bodyVx = away.x * 2.0f;
            bodyVz = away.z * 2.0f;
            bodyVy = -0.8f;
            gAct = Act::None;
            gLedgeLock = 0.45f;
            grounded = false;
        }
        else if ((spaceEdge && !gGrabPress) || gClimbClick) {
            glm::vec3 toBlock((float)gLedgeX + 0.5f - px, 0.0f, (float)gLedgeZ + 0.5f - pz);
            float tl = std::sqrt(toBlock.x * toBlock.x + toBlock.z * toBlock.z);
            float into = tl > 0.001f ? (toBlock.x * flat.x + toBlock.z * flat.z) / tl : 1.0f;
            float top = (float)(gLedgeY + 1);
            HangSpot nextHang;
            bool other = into < 0.55f && findLedgeAhead(px, feet, pz, flat, gLedgeX, gLedgeY, gLedgeZ, nextHang);
            bool wantClimb = gClimbClick || (spaceEdge && w);
            if (wantClimb) {
                gLeapFrom = glm::vec3(px, feet, pz);
                gLeapTo = glm::vec3((float)gLedgeX + 0.5f, top, (float)gLedgeZ + 0.5f);
                beginClimbHands(top);
                gAct = Act::Climb;
                gActT = 0.0f;
            }
            else if (spaceEdge && !w && other) {
                gLeapFrom = glm::vec3(px, feet, pz);
                gLeapTo = nextHang.feet;
                gPendR = nextHang.gripR;
                gPendL = nextHang.gripL;
                gPendYaw = nextHang.yaw;
                gLedgeX = nextHang.bx; gLedgeY = nextHang.by; gLedgeZ = nextHang.bz;
                gAct = Act::Leap;
                gActT = 0.0f;
            }
        }
        else {
            easeToward(px, feet, pz, gHang, dt, 6.5f);
            bodyVx = bodyVy = bodyVz = 0.0f;
            grounded = false;
            airTime = 0.0f;
        }
        place();
    }
    else if (gAct == Act::Slide) {
        gActT += dt;
        if (glm::length(wish) > 0.2f) {
            glm::vec3 steer = glm::normalize(glm::vec3(wish.x, 0.0f, wish.z));
            gActDir = glm::normalize(gActDir * 0.84f + steer * 0.16f);
        }
        int ax = (int)std::floor(px + gActDir.x * 0.85f);
        int az = (int)std::floor(pz + gActDir.z * 0.85f);
        int ay = (int)std::floor(feet);
        float ahead = feet;
        if (blockedCell(ax, ay, az)) ahead = feet + 0.9f;
        else if (blockedCell(ax, ay - 1, az)) ahead = (float)ay;
        else if (blockedCell(ax, ay - 2, az)) ahead = (float)ay - 1.0f;
        else ahead = feet - 1.4f;
        float drop = feet - ahead;
        float friction = manualClamp(1.45f - drop * 0.85f, 0.45f, 4.8f);
        gActSpeed *= std::exp(-dt * friction);
        if (drop > 0.08f) gActSpeed += drop * 5.5f * dt;
        if (gActSpeed > 9.2f) gActSpeed = 9.2f;
        bodyVx = gActDir.x * gActSpeed;
        bodyVz = gActDir.z * gActSpeed;
        double yBefore = camY;
        float ox = px, oz = pz;
        moveHorizontal(px, pz, feet, bodyVx * dt, bodyVz * dt);
        if (px == ox && pz == oz) gActSpeed *= std::exp(-dt * 6.0f);
        camX = px; camZ = pz;
        if (camY != yBefore) feet = (float)camY - gEyeH;
        bodyVy -= 28.0f * dt;
        float next = feet + bodyVy * dt;
        if (hitsSolid(px, next, pz)) {
            if (bodyVy < 0.0f) feet = snapToSupport(px, feet, pz);
            bodyVy = 0.0f;
            grounded = groundedAt(px, feet, pz);
        }
        else {
            feet = next;
            grounded = false;
        }
        if (wet || !grounded || gActSpeed < 2.05f) gAct = Act::None;
        place();
    }
    else if (gAct == Act::Roll) {
        gActT += dt / 0.58f;
        if (gActT > 1.0f) gActT = 1.0f;
        float spd = gActSpeed * (1.0f - 0.72f * gActT);
        bodyVx = gActDir.x * spd;
        bodyVz = gActDir.z * spd;
        float ox = px, oz = pz;
        double yBefore = camY;
        moveHorizontal(px, pz, feet, bodyVx * dt, bodyVz * dt);
        camX = px; camZ = pz;
        if (camY != yBefore) feet = (float)camY - gEyeH;
        if (px == ox && pz == oz && gActT > 0.05f) gActT = 1.0f;
        bodyVy -= 28.0f * dt;
        float next = feet + bodyVy * dt;
        if (overlapsWater(px, next, pz)) {
            gAct = Act::None;
            feet = next;
            grounded = false;
        }
        else if (hitsSolid(px, next, pz)) {
            if (bodyVy < 0.0f) feet = snapToSupport(px, feet, pz);
            bodyVy = 0.0f;
            grounded = true;
        }
        else {
            feet = next;
            grounded = false;
            gAct = Act::None;
        }
        gViewRoll = std::sin(gActT * PI) * 0.30f;
        if (gActT >= 1.0f) {
            gAct = Act::None;
            grounded = true;
            bodyVy = 0.0f;
            airTime = 0.0f;
        }
        place();
    }
    else if (flying) {
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
        gSub = sub;
        bool dive = headWet && (w || s || a || d);
        if (dive) {
            glm::vec3 lookv((float)lookX, (float)lookY, (float)lookZ);
            float ll = glm::length(lookv);
            if (ll < 0.001f) lookv = glm::vec3(flat.x, 0.0f, flat.z);
            else lookv /= ll;
            glm::vec3 side = glm::cross(lookv, glm::vec3(0.0f, 1.0f, 0.0f));
            float sl = glm::length(side);
            if (sl > 0.001f) side /= sl;
            else side = right;
            glm::vec3 wish3(0.0f);
            if (w) wish3 += lookv;
            if (s) wish3 -= lookv;
            if (a) wish3 -= side;
            if (d) wish3 += side;
            if (glm::length(wish3) > 0.001f) wish3 = glm::normalize(wish3) * speed;
            float follow = 1.0f - std::exp(-dt * 2.6f);
            bodyVx += (wish3.x + fx * 0.6f - bodyVx) * follow;
            bodyVy += (wish3.y - bodyVy) * follow;
            bodyVz += (wish3.z + fz * 0.6f - bodyVz) * follow;
        }
        else {
            bodyVx += (wish.x + fx * 1.6f - bodyVx) * (1.0f - std::exp(-dt * 1.7f));
            bodyVz += (wish.z + fz * 1.6f - bodyVz) * (1.0f - std::exp(-dt * 1.7f));
            bodyVy += (-28.0f + 34.0f * sub) * dt;
            if (space) bodyVy += 16.0f * dt;
            if (shift) bodyVy -= 14.0f * dt;
            bodyVy -= down * 3.2f * dt;
        }
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
        bool guard = gCrouch > 0.55f && shift && grounded;
        double yBefore = camY;
        if (guard) moveGuarded(px, pz, feet, bodyVx * dt, bodyVz * dt);
        else moveHorizontal(px, pz, feet, bodyVx * dt, bodyVz * dt);
        camX = px;
        camZ = pz;
        if (camY != yBefore) feet = (float)camY - gEyeH;

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
                float impact = -bodyVy;
                bool intoWater = overlapsWater((float)camX, feet, (float)camZ);
                if (airTime > 0.12f && impact > 7.0f) {
                    landDip = (impact - 7.0f) / 14.0f;
                    if (landDip > 1.0f) landDip = 1.0f;
                }
                if (!intoWater && airTime > 0.28f && impact > 14.0f) {
                    glm::vec3 dir(bodyVx, 0.0f, bodyVz);
                    float hl = std::sqrt(bodyVx * bodyVx + bodyVz * bodyVz);
                    if (hl < 0.45f) dir = glm::vec3(flat.x, 0.0f, flat.z);
                    else { dir.x /= hl; dir.z /= hl; }
                    float savedH = gBodyH;
                    gBodyH = kCrouchH;
                    bool clear = !hitsSolid((float)camX + dir.x * 0.9f, feet, (float)camZ + dir.z * 0.9f);
                    gBodyH = savedH;
                    if (clear) {
                        gAct = Act::Roll;
                        gActT = 0.0f;
                        gActDir = dir;
                        gActSpeed = hl > 4.4f ? hl : 4.4f;
                        gImpact = 0.4f;
                    }
                    else gImpact = 1.0f;
                }
                else if (!intoWater && impact > 9.5f) gImpact = 0.7f;
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
    if (!wet) gSub = 0.0f;
    gLedgeLock -= dt;
    if (gLedgeLock < 0.0f) gLedgeLock = 0.0f;
    if (gAct != Act::Roll && gAct != Act::Slide) gViewRoll *= std::exp(-dt * 8.0f);
    if (gAct == Act::Slide) {
        float tilt = 0.055f;
        gViewRoll += (tilt - gViewRoll) * (1.0f - std::exp(-dt * 6.0f));
    }
    gClimbClick = false;
    gGrabPress = false;
    clampLedgeLook();
    gImpact *= std::exp(-dt * 3.0f);
    camY = (double)feet + (double)gEyeH;
    shiftWas = shift;

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
    if (gCrouch > 0.5f) targetBob *= 0.35f;
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
    float pole = (std::fabs((float)pitch) - 60.0f) / 29.0f;
    if (pole < 0.0f) pole = 0.0f;
    if (pole > 1.0f) pole = 1.0f;
    float swayScale = 1.0f - pole * pole * (3.0f - 2.0f * pole);
    lookSway = lookSwaySmoothed * swayScale;

    float targetFov = 60.0f;
    if (flying) targetFov = 66.0f;
    else if (!wet && speed > 6.0f && grounded) targetFov = 68.0f;
    if (!grounded && !flying && !wet && airTime > 0.12f) targetFov += 2.5f;
    viewFov += (targetFov - viewFov) * (1.0f - std::exp(-dt * 6.0f));

    float spdNow = std::sqrt(bodyVx * bodyVx + bodyVz * bodyVz);
    static bool gDeepWater = false;
    static float gWaterDepth = 0.0f;
    if (wet) {
        int wx = (int)std::floor((float)camX);
        int wz = (int)std::floor((float)camZ);
        float depth = waterPoolDepth(wx, wz);
        gWaterDepth += (depth - gWaterDepth) * (1.0f - std::exp(-dt * 5.0f));
        if (!gDeepWater) {
            if (gWaterDepth >= 1.72f) gDeepWater = true;
        }
        else if (gWaterDepth < 1.38f) gDeepWater = false;
    }
    else {
        gDeepWater = false;
        gWaterDepth += (0.0f - gWaterDepth) * (1.0f - std::exp(-dt * 6.0f));
    }
    MoveMode mode = MoveMode::Idle;
    if (gAct == Act::Hang) mode = MoveMode::LedgeHang;
    else if (gAct == Act::Climb) mode = MoveMode::LedgeClimb;
    else if (gAct == Act::Leap) mode = MoveMode::LedgeJump;
    else if (gAct == Act::Slide) mode = MoveMode::Slide;
    else if (gAct == Act::Roll) mode = MoveMode::Roll;
    else if (wet && gDeepWater && gHeadWet && spdNow > 0.55f) mode = MoveMode::Underwater;
    else if (wet && gDeepWater && spdNow > 0.45f) mode = MoveMode::Swim;
    else if (wet && gDeepWater) mode = MoveMode::Float;
    else if (wet && spdNow > 0.35f) mode = MoveMode::Walk;
    else if (wet) mode = MoveMode::Idle;
    else if (!grounded && !flying && bodyVy > 0.8f) mode = MoveMode::Jump;
    else if (!grounded && !flying) mode = MoveMode::Fall;
    else if (gImpact > 0.2f) mode = MoveMode::Land;
    else if (gCrouch > 0.45f) mode = MoveMode::Crouch;
    else if (!flying && grounded && speed > 6.0f && spdNow > 0.5f) mode = MoveMode::Run;
    else if (spdNow > 0.35f) mode = MoveMode::Walk;
    float action = 0.0f;
    if (gAct == Act::Roll || gAct == Act::Climb || gAct == Act::Leap) action = gActT;
    else if (gAct == Act::Slide) action = manualClamp(gActSpeed / 7.0f, 0.0f, 1.0f);
    else if (gAct == Act::Hang) action = 1.0f;

    gMotion.grounded = grounded;
    gMotion.flying = flying;
    gMotion.swimming = wet;
    gMotion.sprinting = mode == MoveMode::Run;
    gMotion.horizontalSpeed = spdNow;
    gMotion.verticalSpeed = bodyVy;
    gMotion.airTime = airTime;
    gMotion.stepPhase = stepPhase;
    gMotion.mode = mode;
    gMotion.crouch = gCrouch;
    gMotion.submerge = gSub;
    gMotion.action = action;
    gMotion.lean = gViewRoll;
    gMotion.impact = gImpact;
    gMotion.headInWater = gHeadWet;
    gMotion.gripping = gAct == Act::Hang || gAct == Act::Climb;
    gMotion.ledgeYaw = gLedgeYaw;
    gMotion.gripR = gGripR;
    gMotion.gripL = gGripL;
}

bool cameraOfferLedgeClimb() {
    if (gAct != Act::Hang) return false;
    if ((float)pitch < 18.0f) return false;
    gClimbClick = true;
    return true;
}

glm::vec3 cameraBobOffset() { return bobOffset; }

glm::vec3 cameraViewDirection() {
    glm::vec3 look((float)lookX, (float)lookY, (float)lookZ);
    glm::vec3 dir = look + lookSway;
    float len = glm::length(dir);
    if (len < 0.001f) return look;
    return dir / len;
}

glm::vec3 cameraViewUp() {
    float ry = glm::radians((float)yaw);
    float rp = glm::radians((float)pitch);
    float sp = std::sin(rp);
    float cp = std::cos(rp);
    glm::vec3 up(-std::cos(ry) * sp, cp, -std::sin(ry) * sp);
    if (std::fabs(gViewRoll) < 0.001f) return up;
    glm::vec3 look((float)lookX, (float)lookY, (float)lookZ);
    glm::vec3 side = glm::cross(look, up);
    float sl = glm::length(side);
    if (sl < 0.001f) return up;
    side /= sl;
    float c = std::cos(gViewRoll), s = std::sin(gViewRoll);
    return glm::normalize(up * c + side * s);
}

float cameraEyeHeight() { return gEyeH; }
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
        if (gAct != Act::Slide && gAct != Act::Roll && gCrouch > 0.02f) {
            float ry = glm::radians((float)yaw);
            glm::vec3 flat(std::cos(ry), 0.0f, std::sin(ry));
            float want = gCrouch * 0.46f;
            float allowed = 0.0f;
            for (float t = 0.08f; t <= want + 0.001f; t += 0.08f) {
                if (cameraBlocked(origin + flat * t)) break;
                allowed = t;
            }
            if (allowed > want) allowed = want;
            eye += flat * allowed;
        }
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
