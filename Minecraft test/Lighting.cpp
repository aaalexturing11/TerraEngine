#include "Lighting.h"
#include "Config.h"
#include "MathUtil.h"

#include <cmath>

static float faceAmbientOcclusion(int bx, int by, int bz, const glm::ivec3& normal) {
    int nx = normal.x, ny = normal.y, nz = normal.z;
    int px = bx + nx, py = by + ny, pz = bz + nz;

    auto occ = [&](int ax, int ay, int az) {
        return solid(px + ax, py + ay, pz + az) ? 1 : 0;
    };

    int s1 = 0, s2 = 0, c = 0;
    if (ny != 0) {
        s1 = occ(-1, 0, 0) + occ(1, 0, 0);
        s2 = occ(0, 0, -1) + occ(0, 0, 1);
        c = occ(-1, 0, -1) + occ(1, 0, -1) + occ(-1, 0, 1) + occ(1, 0, 1);
    }
    else if (nx != 0) {
        s1 = occ(0, -1, 0) + occ(0, 1, 0);
        s2 = occ(0, 0, -1) + occ(0, 0, 1);
        c = occ(0, -1, -1) + occ(0, 1, -1) + occ(0, -1, 1) + occ(0, 1, 1);
    }
    else {
        s1 = occ(-1, 0, 0) + occ(1, 0, 0);
        s2 = occ(0, -1, 0) + occ(0, 1, 0);
        c = occ(-1, -1, 0) + occ(1, -1, 0) + occ(-1, 1, 0) + occ(1, 1, 0);
    }

    float t = (float)(s1 + s2 + c) / 8.0f;
    return manualClamp(1.0f - t * 0.28f, 0.62f, 1.0f);
}

DayNightLighting computeDayNightLighting(double timeSeconds) {
    DayNightLighting L{};

    const glm::vec3 dayColor(0.53f, 0.78f, 0.95f);
    const glm::vec3 nightColor(0.015f, 0.02f, 0.06f);
    const glm::vec3 duskColor(0.95f, 0.42f, 0.22f);

    double tMod = std::fmod(timeSeconds, (double)CYCLE_LENGTH_SEC);
    float angle = float(tMod / CYCLE_LENGTH_SEC * 2.0f * PI);

    L.sunDir = glm::normalize(glm::vec3(std::cos(angle), std::sin(angle), 0.12f * std::sin(angle * 0.35f)));
    L.moonDir = glm::normalize(-L.sunDir);

    L.dayFactor = smoothstepf(-0.10f, 0.20f, L.sunDir.y);
    float nightFactor = 1.0f - L.dayFactor;
    L.duskFactor = smoothstepf(-0.02f, 0.16f, L.sunDir.y) * (1.0f - smoothstepf(0.25f, 0.55f, L.sunDir.y));

    L.sunIntensity = L.dayFactor;
    L.moonIntensity = 0.35f * nightFactor;

    L.sunColor = glm::mix(glm::vec3(1.0f, 0.55f, 0.28f), glm::vec3(1.0f, 0.96f, 0.88f), smoothstepf(0.05f, 0.45f, L.sunDir.y));
    L.moonColor = glm::vec3(0.55f, 0.68f, 1.0f) * L.moonIntensity;

    glm::vec3 ambientDay(0.10f, 0.12f, 0.14f);
    glm::vec3 ambientNight(0.02f, 0.025f, 0.05f);
    L.ambientRGB = ambientNight * nightFactor + ambientDay * L.dayFactor;

    glm::vec3 skyBase = nightColor * nightFactor + dayColor * L.dayFactor;
    L.skyColor = glm::mix(skyBase, duskColor, L.duskFactor * 0.55f);
    return L;
}

FaceLight evaluateFace(int bx, int by, int bz, const glm::vec3& normal) {
    FaceLight f{};
    Block b = blockAt(bx, by, bz);
    if (b == Block::Lava) {
        f.albedo = glm::vec3(1.0f, 0.32f, 0.05f);
        f.emission = 1.0f;
    }
    else if (b == Block::Torch) {
        f.albedo = glm::vec3(1.0f);
        f.emission = 1.0f;
    }
    else if (b == Block::Water) {
        f.albedo = glm::vec3(0.05f, 0.32f, 0.42f);
    }
    else if (b == Block::Obsidian) {
        f.albedo = glm::vec3(0.07f, 0.05f, 0.10f);
    }
    else if (b == Block::Grass || b == Block::Dirt || b == Block::Stone || b == Block::Log || b == Block::Leaves || isPlant(b)) {
        f.albedo = glm::vec3(1.0f);
    }
    else {
        f.albedo = glm::vec3(0.30f, 0.68f, 0.28f);
    }

    if (isPlant(b) || b == Block::Torch) {
        float sky = 0.0f, lamp = 0.0f;
        cellLight(bx, by, bz, sky, lamp);
        f.gi = manualClamp(sky, 0.0f, 1.0f);
        f.blockLight = manualClamp(lamp, 0.0f, 1.0f);
        f.ao = 1.0f;
    }
    else {
        f.gi = manualClamp(sampleSkyForFace(bx, by, bz, normal.x, normal.y, normal.z), 0.0f, 1.0f);
        f.blockLight = manualClamp(sampleLampForFace(bx, by, bz, normal.x, normal.y, normal.z), 0.0f, 1.0f);
        f.ao = faceAmbientOcclusion(bx, by, bz, glm::ivec3(normal));
    }
    f.caustic = (b == Block::Grass || b == Block::Dirt || b == Block::Stone) ? causticAmount(bx, by, bz) : 0.0f;
    return f;
}
