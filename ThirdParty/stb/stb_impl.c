/* stb_image (public domain / MIT): fallback decoder for cover art JUCE cannot read
   (progressive or CMYK JPEG, 16-bit PNG...). Memory only, no stdio. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include "stb_image.h"
