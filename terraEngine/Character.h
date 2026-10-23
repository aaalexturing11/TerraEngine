#pragma once

#include "Lighting.h"
#include <glm/glm.hpp>

// Classic (Steve, brazos de 4 px) es el valor por defecto.
// textures/skins/model.txt puede contener "slim" o "alex" para brazos de 3 px.
enum class SkinArmStyle { Classic = 0, Slim = 1 };

void characterSetArmStyle(SkinArmStyle style);
bool initCharacter();
void characterSync(float dt, float time, bool motionLocked);
void characterCueAction(bool mining);
void characterHeldLight(glm::vec3& position, glm::vec3& color, float& range);
void renderPlayerCharacter(const DayNightLighting& light, float time);
