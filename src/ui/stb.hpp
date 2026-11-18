// The picture reader, third_party/stb/stb_image.h, with only the formats the
// game needs and without its own file access: the game reads the file itself
// so that names in any language work on Windows.
#pragma once

#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"
