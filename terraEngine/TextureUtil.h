#pragma once

#include <vector>

using TextureId = unsigned int;

TextureId loadTexture(const char* path, bool pixelated = false, bool flipY = false);
bool loadImageRGBA(const char* path, int& width, int& height, std::vector<unsigned char>& pixels, int* sourceChannels = nullptr);
TextureId uploadNearestRGBA(const unsigned char* pixels, int width, int height);
