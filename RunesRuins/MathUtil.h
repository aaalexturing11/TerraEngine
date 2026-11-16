#pragma once

inline float manualClamp(float v, float lo, float hi) {
    return (v < lo ? lo : (v > hi ? hi : v));
}

inline float smoothstepf(float a, float b, float x) {
    float t = manualClamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
