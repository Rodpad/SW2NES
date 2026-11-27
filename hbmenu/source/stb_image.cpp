// stb_image (public domain, third_party/stb): JPEG and PNG decoding for box art.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_WARNINGS
#define STBI_NEON  // the ARM NEON loops (JPEG colour conversion and upsampling): covers decode faster
#include "../third_party/stb/stb_image.h"
