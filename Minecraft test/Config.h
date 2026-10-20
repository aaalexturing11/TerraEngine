#pragma once

constexpr int CHUNK = 16;
constexpr int HEIGHT = 256;
constexpr int SEA_LEVEL = 48;
constexpr int LOAD_RADIUS = 8;

constexpr float PI = 3.14159265358979323846f;

constexpr float DAWN_LENGTH_SEC = 90.0f;
constexpr float DAY_LENGTH_SEC = 600.0f;
constexpr float DUSK_LENGTH_SEC = 90.0f;
constexpr float NIGHT_LENGTH_SEC = 420.0f;
constexpr float CYCLE_LENGTH_SEC = DAWN_LENGTH_SEC + DAY_LENGTH_SEC + DUSK_LENGTH_SEC + NIGHT_LENGTH_SEC;
