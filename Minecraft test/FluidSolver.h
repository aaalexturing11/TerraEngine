#pragma once

void fluidImpact(float x, float y, float z, float speed, float mass);
void fluidWake(float x, float z, float vx, float vz);
void fluidTick(float dt);
void fluidDraw();
void fluidBindRipple(int unit);
void fluidBindHeight(int unit);
void fluidBindFft(int unit);
void fluidSetView(float x, float z);
void fluidHeightOrigin(float& x, float& z);
float fluidRippleWorldSize();
