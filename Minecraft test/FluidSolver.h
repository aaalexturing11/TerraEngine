#pragma once

void fluidImpact(float x, float y, float z, float speed, float mass);
void fluidStream(float x, float yTop, float yBot, float z, float strength);
void fluidTick(float dt);
void fluidDraw();
void fluidBindRipple(int unit);
void fluidBindHeight(int unit);
float fluidRippleWorldSize();
