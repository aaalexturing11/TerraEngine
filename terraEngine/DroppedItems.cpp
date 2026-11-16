#include "DroppedItems.h"

#include "Camera.h"
#include "Character.h"
#include "FluidSolver.h"
#include "Inventory.h"
#include "World.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <vector>

namespace {

constexpr int kMaxItems = 96;
constexpr float kThrowDelay = 0.14f;
constexpr float kPickupDelay = 0.55f;

struct Matter {
    float rest = 0.12f;
    float friction = 6.0f;
    float mass = 1.0f;
    float radius = 0.11f;
    float scale = 0.22f;
    bool floats = false;
};

struct Item {
    Block block = Block::Air;
    int count = 1;
    glm::vec3 pos{ 0.0f };
    glm::vec3 vel{ 0.0f };
    glm::quat rot{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 angVel{ 0.0f };
    float age = 0.0f;
    float still = 0.0f;
    bool asleep = false;
    bool wet = false;
    bool dead = false;
};

struct Pending {
    Block block = Block::Air;
    int count = 1;
    float delay = 0.0f;
    glm::vec3 look{ 0.0f, 0.0f, -1.0f };
    glm::vec3 inherit{ 0.0f };
    glm::vec3 spin{ 0.0f };
};

std::vector<Item> gItems;
std::vector<Pending> gPending;

bool solidBlock(Block b) {
    return b != Block::Air && b != Block::Water && b != Block::Torch && !isPlant(b);
}

bool overlapsSolid(const glm::vec3& p, float r) {
    int x0 = (int)std::floor(p.x - r);
    int x1 = (int)std::floor(p.x + r);
    int y0 = (int)std::floor(p.y - r);
    int y1 = (int)std::floor(p.y + r);
    int z0 = (int)std::floor(p.z - r);
    int z1 = (int)std::floor(p.z + r);
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                if (solidBlock(blockAt(x, y, z))) return true;
    return false;
}

bool pointWet(const glm::vec3& p) {
    int x = (int)std::floor(p.x);
    int y = (int)std::floor(p.y);
    int z = (int)std::floor(p.z);
    if (blockAt(x, y, z) == Block::Water) return true;
    if (waterHasSurface(x, z) && p.y < waterSurfaceY(x, z) - 0.02f) return true;
    return false;
}

Matter matterFor(Block block) {
    Matter m;
    if (isPlant(block)) {
        m.rest = 0.05f; m.friction = 7.0f; m.mass = 0.2f; m.radius = 0.07f; m.scale = 0.34f; m.floats = true;
    }
    else if (block == Block::Torch) {
        m.rest = 0.08f; m.friction = 6.5f; m.mass = 0.25f; m.radius = 0.07f; m.scale = 0.72f; m.floats = true;
    }
    else if (block == Block::Leaves || block == Block::Log) {
        m.rest = 0.10f; m.friction = 5.5f; m.mass = 0.45f; m.radius = 0.11f; m.scale = 0.22f; m.floats = true;
    }
    else if (block == Block::Stone || block == Block::Obsidian) {
        m.rest = 0.16f; m.friction = 8.0f; m.mass = 1.6f; m.radius = 0.12f; m.scale = 0.22f; m.floats = false;
    }
    else if (block == Block::Lava) {
        m.rest = 0.04f; m.friction = 9.0f; m.mass = 1.8f; m.radius = 0.11f; m.scale = 0.20f; m.floats = false;
    }
    else {
        m.rest = 0.09f; m.friction = 7.0f; m.mass = 1.0f; m.radius = 0.11f; m.scale = 0.22f; m.floats = false;
    }
    return m;
}

glm::vec3 freeSpot(const glm::vec3& look, float radius) {
    glm::vec3 eye = cameraPosition();
    glm::vec3 candidates[3] = {
        eye + look * 0.62f + glm::vec3(0.0f, -0.38f, 0.0f),
        eye + look * 0.34f + glm::vec3(0.0f, -0.15f, 0.0f),
        eye + glm::vec3(0.0f, -0.2f, 0.0f)
    };
    for (const glm::vec3& c : candidates)
        if (!overlapsSolid(c, radius)) return c;
    return candidates[2];
}

void spawnNow(const Pending& job) {
    glm::vec3 look = cameraLookDirection();
    if (glm::length(look) < 0.001f) look = job.look;
    else look = glm::normalize(look);
    glm::vec3 side = glm::cross(look, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::length(side) < 0.001f) side = glm::vec3(1.0f, 0.0f, 0.0f);
    else side = glm::normalize(side);
    Matter m = matterFor(job.block);
    Item it;
    it.block = job.block;
    it.count = job.count;
    it.pos = freeSpot(look, m.radius);
    it.vel = look * 6.1f + glm::vec3(0.0f, 2.05f, 0.0f) + cameraVelocity() * 0.9f;
    it.rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    it.angVel = side * 5.2f;
    it.wet = false;
    if ((int)gItems.size() >= kMaxItems) {
        inventoryAccept(it.block, it.count);
        return;
    }
    gItems.push_back(it);
}

void stepItem(Item& it, float dt) {
    Matter m = matterFor(it.block);
    bool wet = pointWet(it.pos);
    if (wet && !it.wet && it.vel.y < -2.5f) {
        int x = (int)std::floor(it.pos.x);
        int y = (int)std::floor(it.pos.y);
        int z = (int)std::floor(it.pos.z);
        waterImpact(x, y, z, it.vel.y, m.mass * 40.0f);
        fluidWake(it.pos.x, it.pos.z, it.vel.x, it.vel.z);
    }
    it.wet = wet;

    it.vel.y -= 28.0f * dt;
    if (wet) {
        it.vel.y += (m.floats ? 36.0f : 13.0f) * dt;
        float drag = 1.0f - std::exp(-dt * (m.floats ? 2.4f : 1.5f));
        it.vel -= it.vel * drag;
        int x = (int)std::floor(it.pos.x);
        int y = (int)std::floor(it.pos.y);
        int z = (int)std::floor(it.pos.z);
        if (it.vel.y > 0.0f && blockAt(x, y + 1, z) != Block::Water)
            it.vel.y *= std::exp(-dt * 8.0f);
        float fx = 0.0f, fz = 0.0f, down = 0.0f;
        waterFlow(x, y, z, fx, fz, down);
        it.vel.x += fx * 2.2f * dt;
        it.vel.z += fz * 2.2f * dt;
        it.vel.y -= down * 1.5f * dt;
    }

    auto hit = [&](const glm::vec3& p) { return overlapsSolid(p, m.radius); };
    glm::vec3 p = it.pos;
    p.x += it.vel.x * dt;
    if (hit(p)) {
        p.x = it.pos.x;
        it.vel.x = -it.vel.x * m.rest;
        it.angVel *= 0.6f;
    }
    p.z += it.vel.z * dt;
    if (hit(p)) {
        p.z = it.pos.z;
        it.vel.z = -it.vel.z * m.rest;
        it.angVel *= 0.6f;
    }
    bool ground = false;
    float prevY = p.y;
    p.y += it.vel.y * dt;
    if (hit(p)) {
        p.y = prevY;
        if (it.vel.y < 0.0f) {
            ground = true;
            float bounce = -it.vel.y * m.rest;
            it.vel.y = bounce < 0.55f ? 0.0f : bounce;
            float fr = std::exp(-dt * m.friction);
            it.vel.x *= fr;
            it.vel.z *= fr;
        }
        else it.vel.y = 0.0f;
    }
    it.pos = p;
    if (hit(it.pos)) {
        for (int i = 0; i < 10 && hit(it.pos); ++i) it.pos.y += 0.08f;
        it.vel.y = 0.0f;
    }

    glm::vec2 slide(it.vel.x, it.vel.z);
    float slideSp = glm::length(slide);
    if (ground && slideSp > 0.25f) {
        glm::vec3 forward = glm::vec3(slide.x, 0.0f, slide.y) / slideSp;
        it.angVel = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward) * (slideSp / m.radius);
    }
    else it.angVel *= std::exp(-dt * 1.5f);

    float av = glm::length(it.angVel);
    if (av > 12.0f) { it.angVel *= 12.0f / av; av = 12.0f; }
    if (av > 0.02f)
        it.rot = glm::normalize(glm::rotate(it.rot, av * dt, it.angVel / av));

    if (ground && glm::length(it.vel) < 0.12f && av < 0.35f) {
        it.still += dt;
        if (it.still > 0.32f) {
            it.asleep = true;
            it.vel = glm::vec3(0.0f);
            it.angVel = glm::vec3(0.0f);
        }
    }
    else it.still = 0.0f;

    it.age += dt;
    if (it.pos.y < -2.0f) {
        inventoryAccept(it.block, it.count);
        it.dead = true;
    }
}

void separateAwake() {
    for (size_t i = 0; i < gItems.size(); ++i) {
        if (gItems[i].asleep || gItems[i].dead) continue;
        float ri = matterFor(gItems[i].block).radius;
        for (size_t j = i + 1; j < gItems.size(); ++j) {
            if (gItems[j].asleep || gItems[j].dead) continue;
            float rj = matterFor(gItems[j].block).radius;
            glm::vec3 d = gItems[j].pos - gItems[i].pos;
            d.y = 0.0f;
            float dist = glm::length(d);
            float need = ri + rj;
            if (dist > 0.001f && dist < need) {
                glm::vec3 n = d / dist;
                float push = (need - dist) * 0.5f;
                gItems[i].pos -= n * push;
                gItems[j].pos += n * push;
                gItems[i].asleep = false;
                gItems[j].asleep = false;
            }
        }
    }
}

} // namespace

bool droppedItemsQueue(Block block, int count) {
    if (block == Block::Air || count <= 0) return false;
    if ((int)gItems.size() + (int)gPending.size() >= kMaxItems) return false;
    glm::vec3 look = cameraLookDirection();
    if (glm::length(look) < 0.001f) look = glm::vec3(0.0f, 0.0f, -1.0f);
    else look = glm::normalize(look);
    glm::vec3 side = glm::cross(look, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::length(side) < 0.001f) side = glm::vec3(1.0f, 0.0f, 0.0f);
    else side = glm::normalize(side);
    Pending job;
    job.block = block;
    job.count = count;
    job.delay = kThrowDelay;
    job.look = look;
    job.inherit = cameraVelocity();
    job.spin = side * 5.2f;
    gPending.push_back(job);
    return true;
}

void droppedItemsUpdate(float dt) {
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;
    for (Pending& job : gPending) job.delay -= dt;
    for (size_t i = 0; i < gPending.size();) {
        if (gPending[i].delay > 0.0f) { ++i; continue; }
        spawnNow(gPending[i]);
        gPending[i] = gPending.back();
        gPending.pop_back();
    }

    glm::vec3 eye = cameraPosition();
    float eyeH = cameraEyeHeight();
    if (eyeH < 0.4f) eyeH = 1.62f;
    glm::vec3 feet(eye.x, eye.y - eyeH, eye.z);
    float bodyTop = feet.y + eyeH + 0.28f;
    const float step = dt > 0.0f ? dt * 0.25f : 0.0f;
    const int sub = dt > 0.0f ? 4 : 0;
    for (Item& it : gItems) {
        if (it.dead) continue;
        float dx = it.pos.x - feet.x;
        float dz = it.pos.z - feet.z;
        float horiz = std::sqrt(dx * dx + dz * dz);
        if (horiz < 2.4f) it.asleep = false;
        if (it.asleep) it.age += dt;
        if (!it.asleep) {
            for (int s = 0; s < sub; ++s) stepItem(it, step);
        }
        if (it.dead || it.age < kPickupDelay || !inventoryCanAccept(it.block, 1)) continue;
        dx = it.pos.x - feet.x;
        dz = it.pos.z - feet.z;
        horiz = std::sqrt(dx * dx + dz * dz);
        float reach = 0.72f + matterFor(it.block).radius;
        bool level = it.pos.y + 0.35f >= feet.y - 0.2f && it.pos.y - 0.35f <= bodyTop;
        if (horiz <= reach && level) {
            int got = inventoryAccept(it.block, it.count);
            it.count -= got;
            if (it.count <= 0) it.dead = true;
            continue;
        }
        if (horiz < 1.35f && level && horiz > 0.001f) {
            glm::vec3 want((-dx / horiz) * 5.5f, it.vel.y, (-dz / horiz) * 5.5f);
            it.vel.x += (want.x - it.vel.x) * (1.0f - std::exp(-dt * 8.0f));
            it.vel.z += (want.z - it.vel.z) * (1.0f - std::exp(-dt * 8.0f));
            it.asleep = false;
        }
    }
    separateAwake();
    for (size_t i = 0; i < gItems.size();) {
        if (!gItems[i].dead) { ++i; continue; }
        gItems[i] = gItems.back();
        gItems.pop_back();
    }
}

void droppedItemsDraw(const DayNightLighting& light, float time, const glm::vec3& eye, const glm::vec3& look) {
    glm::vec3 fwd = look;
    if (glm::length(fwd) < 0.001f) fwd = glm::vec3(0.0f, 0.0f, -1.0f);
    else fwd = glm::normalize(fwd);
    for (const Item& it : gItems) {
        glm::vec3 to = it.pos - eye;
        float dist = glm::length(to);
        if (dist > 42.0f) continue;
        if (dist > 1.2f && glm::dot(to, fwd) < 0.0f) continue;
        Matter m = matterFor(it.block);
        glm::mat4 world = glm::translate(glm::mat4(1.0f), it.pos)
            * glm::mat4_cast(it.rot)
            * glm::scale(glm::mat4(1.0f), glm::vec3(m.scale));
        int x = (int)std::floor(it.pos.x);
        int y = (int)std::floor(it.pos.y);
        int z = (int)std::floor(it.pos.z);
        float sky = 0.0f, lamp = 0.0f;
        cellLight(x, y, z, sky, lamp);
        renderLooseItem(world, it.block, light, sky, lamp, time);
    }
}

int droppedItemsTorchLights(glm::vec3* tips, int maxCount, float maxDist) {
    if (!tips || maxCount <= 0) return 0;
    if (maxCount > 8) maxCount = 8;
    float bestD[8] = {};
    int count = 0;
    float limit = maxDist * maxDist;
    glm::vec3 eye = cameraPosition();
    for (const Item& it : gItems) {
        if (it.block != Block::Torch || it.dead) continue;
        float scale = matterFor(it.block).scale;
        glm::vec3 p = it.pos + it.rot * glm::vec3(0.0f, (10.0f / 16.0f) * scale, 0.0f);
        float d = glm::dot(p - eye, p - eye);
        if (d > limit) continue;
        int slot = count;
        if (count < maxCount) {
            bestD[count] = d;
            ++count;
        }
        else {
            int worst = 0;
            for (int i = 1; i < count; ++i)
                if (bestD[i] > bestD[worst]) worst = i;
            if (d >= bestD[worst]) continue;
            slot = worst;
            bestD[worst] = d;
        }
        tips[slot] = p;
    }
    return count;
}
