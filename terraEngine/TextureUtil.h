#pragma once

using TextureId = unsigned int;

TextureId loadTexture(const char* path, bool pixelated = false, bool flipY = false);
