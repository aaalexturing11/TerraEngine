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

static float sunAngleAt(double t) {
    const double dawn = DAWN_LENGTH_SEC;
    const double day = DAY_LENGTH_SEC;
    const double dusk = DUSK_LENGTH_SEC;
    const double night = NIGHT_LENGTH_SEC;
    if (t < dawn) {
        float u = (float)(t / dawn);
        return u * 0.12f * PI;
    }
    t -= dawn;
    if (t < day) {
        float u = (float)(t / day);
        return (0.12f + u * 0.76f) * PI;
    }
    t -= day;
    if (t < dusk) {
        float u = (float)(t / dusk);
        return (0.88f + u * 0.12f) * PI;
    }
    t -= dusk;
    float u = (float)(t / night);
    if (u > 1.0f) u = 1.0f;
    return (1.0f + u) * PI;
}

DayNightLighting computeDayNightLighting(double timeSeconds) {
    DayNightLighting L{};

    const glm::vec3 dayColor(0.34f, 0.60f, 0.93f);
    const glm::vec3 nightColor(0.015f, 0.02f, 0.06f);
    const glm::vec3 dawnColor(1.00f, 0.48f, 0.28f);
    const glm::vec3 duskColor(0.95f, 0.36f, 0.18f);

    double tMod = std::fmod(timeSeconds, (double)CYCLE_LENGTH_SEC);
    if (tMod < 0.0) tMod += (double)CYCLE_LENGTH_SEC;
    float angle = sunAngleAt(tMod);

    L.sunDir = glm::normalize(glm::vec3(std::cos(angle), std::sin(angle), 0.10f * std::sin(angle * 0.35f)));
    L.moonDir = glm::normalize(-L.sunDir);

    float sunY = L.sunDir.y;
    L.dayFactor = smoothstepf(-0.20f, 0.48f, sunY);
    float above = smoothstepf(0.0f, 0.52f, sunY);
    float buried = smoothstepf(-0.02f, -0.46f, sunY);
    float twilight = (1.0f - above) * (1.0f - buried);
    float rising = smoothstepf(-0.25f, 0.25f, std::cos(angle));
    L.dawnFactor = twilight * rising;
    L.duskFactor = twilight * (1.0f - rising);

    L.sunIntensity = smoothstepf(-0.06f, 0.42f, sunY);
    L.starFactor = smoothstepf(0.10f, -0.38f, sunY);
    L.moonIntensity = 0.40f * L.starFactor;
    L.moonPhase = (int)(timeSeconds / (double)CYCLE_LENGTH_SEC);
    if (L.moonPhase < 0) L.moonPhase = 0;
    L.moonPhase %= 8;

    glm::vec3 noonSun(1.0f, 0.96f, 0.88f);
    glm::vec3 riseSun(1.0f, 0.50f, 0.22f);
    L.sunColor = glm::mix(noonSun, riseSun, twilight);

    float moonAmt = 1.0f;
    if (L.moonPhase == 4) moonAmt = 0.08f;
    else if (L.moonPhase == 3 || L.moonPhase == 5) moonAmt = 0.45f;
    else if (L.moonPhase == 2 || L.moonPhase == 6) moonAmt = 0.70f;
    L.moonColor = glm::vec3(0.62f, 0.72f, 1.0f) * L.moonIntensity * moonAmt;

    glm::vec3 ambientDay(0.10f, 0.13f, 0.18f);
    glm::vec3 ambientNight(0.045f, 0.050f, 0.085f);
    L.ambientRGB = glm::mix(ambientNight, ambientDay, L.dayFactor);

    float dayW = smoothstepf(-0.08f, 0.62f, sunY);
    float nightW = smoothstepf(0.12f, -0.55f, sunY);
    float warmW = twilight;
    float sum = dayW + nightW + warmW;
    if (sum < 0.001f) sum = 1.0f;
    glm::vec3 warm = glm::mix(duskColor, dawnColor, rising);
    L.skyColor = (dayColor * dayW + nightColor * nightW + warm * warmW) / sum;
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
